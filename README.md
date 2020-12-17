# pigaragedoor
Control a Garage Door with a raspberry pi and a relay board.

Using the hardware setup described in this [Raspberry Pi Garage Door Opener instructable](http://www.instructables.com/id/Raspberry-Pi-Garage-Door-Opener/)

This golang project produces a single binary that will host a very simple HTML web-app (with iphone icon meta)
with a button and an endpoint you can HTTP GET to trigger the GPIO pin.

This project is the actual source code for the golang binary, which can be cross-compiled.

## SD Card Image

I maintain a bitbake layer that can be used to produce a purpose-built embedded linux image.
If you're new to building an embedded linux project, this is a great one to start with!

The [Quick Build Instructions](https://github.com/bvarner/meta-varnerized/blob/main/meta-garagedoor/README.MD#quick-build-instructions-ubuntu-2010) in my bitbake layer should set you on the right path.
