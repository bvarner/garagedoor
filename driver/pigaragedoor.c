#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/pwm.h>
#include <linux/uaccess.h>

struct pigaragedoor_data {
	u8			statemask;

	struct device 		*dev;
	struct cdev		cdev;
	dev_t			cdev_num;
	struct gpio_desc	*gpiod_relay_a;
	struct gpio_desc	*gpiod_relay_b;
		
	struct gpio_desc	*gpiod_switch_a;
	struct gpio_desc	*gpiod_switch_b;
	int			irq_switch_a;
	int			irq_switch_b;
	
	u8			inputs;
	bool			input_dirty;
};

#define WC_DEVICE_NAME "pigaragedoor"
#define WC_CLASS "garagedoor-class"
struct class *garagedoor_class;

static DECLARE_WAIT_QUEUE_HEAD(garagedoor_rq);

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)  \
  (byte & 0x80 ? '1' : '0'), \
  (byte & 0x40 ? '1' : '0'), \
  (byte & 0x20 ? '1' : '0'), \
  (byte & 0x10 ? '1' : '0'), \
  (byte & 0x08 ? '1' : '0'), \
  (byte & 0x04 ? '1' : '0'), \
  (byte & 0x02 ? '1' : '0'), \
  (byte & 0x01 ? '1' : '0') 

static int pigaragedoor_set_state(struct pigaragedoor_data *garagedoor) {
	pr_info("  %s\n", __FUNCTION__);
	gpiod_set_value(garagedoor->gpiod_pwr_en, garagedoor->pwr_en);
	
	pr_info("    statemask: "BYTE_TO_BINARY_PATTERN"\n", BYTE_TO_BINARY(garagedoor->statemask));
	gpiod_set_value(garagedoor->gpiod_relay_a,   (garagedoor->statemask & 0x80) ? 1 : 0);
	pr_info("        relay_a:%d", (garagedoor->statemask & 0x80) ? 1 : 0);
	gpiod_set_value(garagedoor->gpiod_relay_b,   (garagedoor->statemask & 0x40) ? 1 : 0);
	pr_info("        relay_b:%d", (garagedoor->statemask & 0x40) ? 1 : 0);
	
	return 0;
};

static int pigaragedoor_read_state(struct pigaragedoor_data *garagedoor) {
	u8 val = 0x00;
	
	// gpiod_get_value() is safe from upper-half IRQ handlers, as it's a memory I/O instruction in the gpio controller. No need to split to a work queue or a upper / bottom half IRQ handler.
	val |= (gpiod_get_value(garagedoor->gpiod_switch_a) > 0 ? 1 : 0) << 1;
	val |= (gpiod_get_value(garagedoor->gpiod_switch_b) > 0 ? 1 : 0);
	
	garagedoor->inputs = val;
	
	//pr_info("     state: "BYTE_TO_BINARY_PATTERN"\n", BYTE_TO_BINARY(garagedoor->inputs));
	
	return 0;
}


static irqreturn_t pigaragedoor_handle_irq(int irq, void *dev_id) {
	struct pigaragedoor_data *pigaragedoor = dev_id;
	
	pigaragedoor_read_state(pigaragedoor);
	pigaragedoor->input_dirty = true;
	wake_up_interruptible(&garagedoor_rq); // Notify pollers that data is ready.
	
	return IRQ_HANDLED;
}



// Expose sysfs attributes


static ssize_t pigaragedoor_show_switch_a(struct device *dev, struct device_attribute *attr, char *buf) {
	struct pigaragedoor_data *pigaragedoor_data = dev_get_drvdata(dev);
	int len;
	
	len = sprintf(buf, "%s\n", (pigaragedoor_data->inputs & 0x01) ? "closed" : "open");
	if (len <= 0) {
		dev_err(dev, "pigaragedoor: Invalid sprintf len: %d\n", len);
	}
	return len;
}
static DEVICE_ATTR(button, S_IRUGO, pigaragedoor_show_switch_a, NULL);


static ssize_t pigaragedoor_show_switch_b(struct device *dev, struct device_attribute *attr, char *buf) {
	struct pigaragedoor_data *pigaragedoor_data = dev_get_drvdata(dev);
	int len;
	
	len = sprintf(buf, "%s\n", (pigaragedoor_data->inputs & 0x02) ? "closed" : "open");
	if (len <= 0) {
		dev_err(dev, "pigaragedoor: Invalid sprintf len: %d\n", len);
	}
	return len;
}
static DEVICE_ATTR(button, S_IRUGO, pigaragedoor_show_switch_b, NULL);


static ssize_t pigaragedoor_show_statemask(struct device *dev, struct device_attribute *attr, char *buf) {
	struct pigaragedoor_data *pigaragedoor_data = dev_get_drvdata(dev);
	int len;

	len = sprintf(buf, "%d\n", pigaragedoor_data->statemask);
	if (len <= 0) {
		dev_err(dev, "pigaragedoor: Invalid sprintf len: %d\n", len);
	}
	return len;
}



static ssize_t pigaragedoor_store_statemask(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
	struct pigaragedoor_data *pigaragedoor_data = dev_get_drvdata(dev);
	
	kstrtou8(buf, 10, &pigaragedoor_data->statemask);
	pigaragedoor_set_state(pigaragedoor_data);
	
	return count;
}

static DEVICE_ATTR(statemask, S_IRUGO | S_IWUSR, pigaragedoor_show_statemask, pigaragedoor_store_statemask);



static struct attribute *pigaragedoor_attrs[] = {
	&dev_attr_switch_a.attr,
	&dev_attr_switch_b.attr,
	&dev_attr_statemask.attr,
	
	NULL
};
ATTRIBUTE_GROUPS(pigaragedoor);




static int pigaragedoor_open(struct inode *inode, struct file *filp) {
	unsigned int maj = imajor(inode);
	unsigned int min = iminor(inode);
	
	struct pigaragedoor_data *pigaragedoor_data = NULL;
	pigaragedoor_data = container_of(inode->i_cdev, struct pigaragedoor_data, cdev);
	
	//TODO: Validate the major, based on the allocation of the major number from probe.
	if (min < 0) {
		pr_err("device not found\n");
		return -ENODEV;
	}
	
	filp->private_data = pigaragedoor_data;
	
	pr_info("  pigaragedoor opened.\n");
	return 0;
}


static int pigaragedoor_release(struct inode *inode, struct file *filp) {
	struct pigaragedoor_data *pigaragedoor_data = NULL;
	pigaragedoor_data = container_of(inode->i_cdev, struct pigaragedoor_data, cdev);
	
	pr_info("  pigaragedoor closed.\n");
	
	return 0;
}

ssize_t pigaragedoor_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {
	struct pigaragedoor_data *pigaragedoor_data = filp->private_data;
	ssize_t retval = 0;
	int i;
	
	pr_info("  pigaragedoor write %d bytes\n", count);
	for (i = 0; i < count; i++) {
		if (copy_from_user(&pigaragedoor_data->statemask, buf + i, 1) < 0) {
			pr_err("pigaragedoor: write failed\n");
			return -EFAULT;
		}
		
		pigaragedoor_set_state(pigaragedoor_data);
	}
	*f_pos += count;
	return count;
}

ssize_t pigaragedoor_read(struct file *filp, char __user *buf, size_t count, loff_t *pos) {
	struct pigaragedoor_data *pigaragedoor_data = filp->private_data;
	size_t readlen = 2;
	
	// If the input hasn't changed, tell it to try again later.
	if (pigaragedoor_data->input_dirty == false) {
		return -EAGAIN;
	}
	
	// If we want fewer than 2 bytes, only copy what they ask for.
	if (count < readlen) {
		readlen = count;
	}
	// Note: We never have an EOF.
	
	// compose 2 bytes, statemask & input state.
	u16 state = pigaragedoor_data->statemask << 8 | pigaragedoor_data->inputs;
	
	if (copy_to_user(buf, &state, readlen) != 0) {
		return -EIO;
	}
	
	// Adjust the kernel position pointer to track the number of bytes copied for this fd.
	*pos += readlen;
	
	// Mark that there's no data left before returning to userspace.
	pigaragedoor_data->input_dirty = false;
	
	return readlen;
}

static unsigned int pigaragedoor_poll(struct file *file, poll_table *wait) {
	struct pigaragedoor_data *pigaragedoor_data = file->private_data;
	unsigned int reval_mask = 0;
	
	poll_wait(file, &garagedoor_rq, wait);
	
	if (pigaragedoor_data->input_dirty) {
		reval_mask |= (POLLIN | POLLRDNORM);
	}
	
	return reval_mask;
}

static const struct file_operations wc_fops = {
	.owner = THIS_MODULE,
	.write = pigaragedoor_write,
	.read = pigaragedoor_read,
	.open = pigaragedoor_open,
	.release = pigaragedoor_release,
	.poll = pigaragedoor_poll,
};




static int pigaragedoor_probe(struct platform_device *pdev) {
	struct device *dev = &pdev->dev;
	struct pigaragedoor_data *pigaragedoor_data;

	int ret = 0;
	
	pr_alert(" %s\n", __FUNCTION__);
	
	// create the driver data...
	pigaragedoor_data = kzalloc(sizeof(struct pigaragedoor_data), GFP_KERNEL);
	if (!pigaragedoor_data) {
		return -ENOMEM;
	}
	pigaragedoor_data->dev = dev;
	pigaragedoor_data->input_dirty = false;
	
	
	// Obtain GPIOs from the device tree binding.
	pigaragedoor_data->gpiod_relay_a = devm_gpiod_get(dev, "relay_a", GPIOD_OUT_HIGH);
	if (IS_ERR(pigaragedoor_data->gpiod_relay_a)) {
		dev_err(dev, "failed to get relay_a-gpiod: err=%ld\n", PTR_ERR(pigaragedoor_data->gpiod_relay_a));
		return PTR_ERR(pigaragedoor_data->gpiod_relay_a);
	}
	
	pigaragedoor_data->gpiod_relay_b = devm_gpiod_get_index(dev, "relay_b", 0, GPIOD_OUT_HIGH);
	if (IS_ERR(pigaragedoor_data->gpiod_relay_b)) {
		dev_err(dev, "failed to get relay_b-gpiod: err=%ld\n", PTR_ERR(pigaragedoor_data->gpiod_relay_b));
		return PTR_ERR(pigaragedoor_data->gpiod_relay_b);
	}
	

	pigaragedoor_data->gpiod_switch_a = devm_gpiod_get_index(dev, "switch_a", 0, GPIOD_IN);
	if (IS_ERR(pigaragedoor_data->gpiod_switch_a)) {
		dev_err(dev, "failed to get switch_a-gpiod: err=%ld\n", PTR_ERR(pigaragedoor_data->gpiod_switch_a));
		return PTR_ERR(pigaragedoor_data->gpiod_switch_a);
	}
	pigaragedoor_data->irq_switch_a = gpiod_to_irq(pigaragedoor_data->gpiod_switch_a);
	if (pigaragedoor_data->irq_switch_a < 0) {
		dev_err(dev, "failed to get IRQ for switch_a: err=%d\n", pigaragedoor_data->irq_switch_a);
		return pigaragedoor_data->irq_switch_a;
	}
	devm_request_irq(dev, pigaragedoor_data->irq_switch_a, pigaragedoor_handle_irq, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "pigaragedoor_switch_a", pigaragedoor_data);
	
	
	pigaragedoor_data->gpiod_switch_b = devm_gpiod_get_index(dev, "switch_b", 0, GPIOD_IN);
	if (IS_ERR(pigaragedoor_data->gpiod_switch_b)) {
		dev_err(dev, "failed to get switch_b-gpiod: err=%ld\n", PTR_ERR(pigaragedoor_data->gpiod_switch_b));
		return PTR_ERR(pigaragedoor_data->gpiod_switch_b);
	}
	pigaragedoor_data->irq_switch_b = gpiod_to_irq(pigaragedoor_data->gpiod_switch_b);
	if (pigaragedoor_data->irq_switch_b < 0) {
		dev_err(dev, "failed to get IRQ for switch_b: err=%d\n", pigaragedoor_data->irq_switch_b);
		return pigaragedoor_data->irq_switch_b;
	}
	devm_request_irq(dev, pigaragedoor_data->irq_switch_b, pigaragedoor_handle_irq, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "pigaragedoor_switch_b", pigaragedoor_data);
	

	// Initial GPIO states
	pigaragedoor_data->statemask = 0x00;
	pigaragedoor_data->inputs = 0x00;
	
	platform_set_drvdata(pdev, pigaragedoor_data);
	
	// sync initial state
	pigaragedoor_set_state(pigaragedoor_data);
	pigaragedoor_read_state(pigaragedoor_data);
	
	// Associate the attribute groups.
	ret = sysfs_create_group(&pdev->dev.kobj, &pigaragedoor_group);
	if (ret) {
		dev_err(dev, "sysfs creation failed\n");
		return ret;
	}
	
	// Character Device Support
	alloc_chrdev_region(&(pigaragedoor_data->cdev_num), 0, 1, WC_DEVICE_NAME);
	garagedoor_class = class_create(THIS_MODULE, WC_CLASS);

	cdev_init(&(pigaragedoor_data->cdev), &wc_fops);
	pigaragedoor_data->cdev.owner = THIS_MODULE;
	pigaragedoor_data->cdev_num = MKDEV(MAJOR(pigaragedoor_data->cdev_num), MINOR(pigaragedoor_data->cdev_num));
	cdev_add(&(pigaragedoor_data->cdev), pigaragedoor_data->cdev_num, 1);
	
	// Create the device node /dev/pigaragedoor
	device_create(garagedoor_class,
			NULL, // no parent
			pigaragedoor_data->cdev_num,
			NULL, // no additional data
			WC_DEVICE_NAME "%d", 0);
	
	return ret;
};


static int pigaragedoor_remove(struct platform_device *pdev) {
	struct pigaragedoor_data *pigaragedoor_data = platform_get_drvdata(pdev);

	device_destroy(garagedoor_class, pigaragedoor_data->cdev_num);
	class_destroy(garagedoor_class);
	
	sysfs_remove_group(&pdev->dev.kobj, &pigaragedoor_group);
	
	unregister_chrdev_region(pigaragedoor_data->cdev_num, 1);

	kfree(pigaragedoor_data);
	
	
	return 0;
};

static const struct of_device_id of_pigaragedoor_match[] = {
	{ .compatible = "garagedoor,pigaragedoor", },
	{},
};

MODULE_DEVICE_TABLE(of, of_pigaragedoor_match);


static struct platform_driver pigaragedoor_driver = {
	.probe		= pigaragedoor_probe,
	.remove		= pigaragedoor_remove,
	.driver 	= {
		.name 	= "pigaragedoor",
		.owner 	= THIS_MODULE,
		.of_match_table = of_pigaragedoor_match,
	},
};

module_platform_driver(pigaragedoor_driver);

MODULE_AUTHOR("bryan@varnernet.com");
MODULE_DESCRIPTION("pigaragedoor Platform Driver.");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pigaragedoor");
