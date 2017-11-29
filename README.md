# pigaragedoor
Control a Garage Door with a raspberry pi and a relay board.

Using the hardware setup described at,
http://www.instructables.com/id/Raspberry-Pi-Garage-Door-Opener/

This golang project produces a single binary that will host a very simple HTML web-app (with iphone icon meta)
with a button and an endpoint you can HTTP GET to trigger the GPIO pin.

This project is the actual source code for the golang binary.

I've included bitbake recipes for building this as part of an embedded linux image in my 
https://github.com/bvarner/meta-bvarner-embedded bitbake layer.

## Embedded Images
For raspberrypi (model a, b, B+, zero, zero-w) yocto images built from my meta-bvarner-embedded layer are available
under the 'releases' of this repository.

If you'd rather build your own directions are [in my bitbake layer repo|https://github.com/bvarner/meta-bvarner-embedded#the-fast-way-quick-n-dirty-script]