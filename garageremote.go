package main

import (
	"fmt"
	"net/http"
	"sync"
	"github.com/bvarner/go-rpigpio"
	"log"
)

var mutex *sync.Mutex
var pin *rpi.Pin

func buttonHandler(w http.ResponseWriter, r *http.Request) {
	mutex.Lock();
	defer mutex.Unlock();
	// Trigger the pin.
	pin.Write(rpi.LOW);
	time.sleep(time.Second * 1)
	pin.Write(rpi.HIGH);
}

func main() {
	mutex = &sync.Mutex{}
	
	// Set the output pin to high, output.
	mutex.Lock();
	pin, err := rpi.OpenWriteDefault(4, rpi.HIGH)
	if err != nil {
		log.Fatal("Unable to open GPIO pin: ", err)
	}
	mutex.Unlock();

	// Serve the static file system from 'esc'
	http.Handle("/", http.FileServer(FS(false)))

	// Setup the button handler to toggle the pin.
	http.HandleFunc("/press", buttonHandler)
	
	err = http.ListenAndServe(":80", nil)
	if err != nil {
		log.Fatal("Failed start server on port 80: ", err)
	}
}
