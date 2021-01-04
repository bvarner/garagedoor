package main

import (
	"flag"
	"github.com/GeertJohan/go.rice"
	"github.com/nathan-osman/go-rpigpio"
	"log"
	"net/http"
	"os"
	"sync"
	"time"
)

var mutex *sync.Mutex
var pin *rpi.Pin

// Configured for Active Low.
const ON = rpi.LOW
const OFF = rpi.HIGH

func redirectTLS(w http.ResponseWriter, r *http.Request) {
	http.Redirect(w, r, "https://" + r.Host + r.RequestURI, http.StatusMovedPermanently)
}

func buttonHandler(w http.ResponseWriter, r *http.Request) {
	mutex.Lock()
	defer mutex.Unlock()
	// Trigger the pin
	err := pin.Write(ON)
	if err == nil {
		time.Sleep(time.Second * 1)
		err = pin.Write(OFF)
	}

	if err != nil {
		w.WriteHeader(http.StatusInternalServerError)
		w.Write([]byte(err.Error()))
	} else {
		http.Redirect(w, r, "/", http.StatusFound)
	}
}

func main() {
	var err error
	mutex = &sync.Mutex{}
	
	// Open the output pin and set it high.
	mutex.Lock()
	pin, err = rpi.OpenPin(4, rpi.OUT)
	if err != nil {
		log.Fatal("Unable to open GPIO pin: ", err)
	}
	defer pin.Close()
	pin.Write(OFF)
	mutex.Unlock()

	box := rice.MustFindBox("static")
	http.Handle("/", http.FileServer(box.HTTPBox()))

	// Setup the button handler to toggle the pin.
	http.HandleFunc("/press", buttonHandler)

	cert := flag.String("cert", "/etc/ssl/certs/pigaragedoor.pem", "The certificate for this server.")
	certkey := flag.String("key", "/etc/ssl/certs/pigaragedoor-key.pem", "The key for the server cert.")

	_, certerr := os.Stat(*cert)
	_, keyerr := os.Stat(*certkey)

	if certerr == nil && keyerr == nil {
		log.Println("Configuring for SSL...")
		go func() {
			log.Fatal(http.ListenAndServe(":80", http.HandlerFunc(redirectTLS)))
		} ()
		log.Fatal(http.ListenAndServeTLS(":443", *cert, *certkey, nil))
	} else {
		log.Println("SSL Configuration not found, falling back to HTTP only.")
		log.Fatal(http.ListenAndServe(":80", nil))
	}
}
