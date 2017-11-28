package main

//go:generate esc -o static.go -prefix "static/" static

import (
	"net/http"
	"sync"
	"github.com/nathan-osman/go-rpigpio"	
	"time"
	"log"
)

var mutex *sync.Mutex
var pin *rpi.Pin

func buttonHandler(w http.ResponseWriter, r *http.Request) {
	mutex.Lock();
	defer mutex.Unlock();
	// Trigger the pin.
	pin.Write(rpi.LOW);
	time.Sleep(time.Second * 1)
	pin.Write(rpi.HIGH);
	http.Redirect(w, r, "/", http.StatusFound)
}

func main() {
	mutex = &sync.Mutex{}
	
	// Open the output pin and set it high.
	mutex.Lock()
	var err error
	pin, err = rpi.OpenPin(4, rpi.OUT)
	if err != nil {
		log.Fatal("Unable to open GPIO pin: ", err)
	}
	pin.Write(rpi.HIGH);
	defer pin.Close()
	mutex.Unlock()

	// Serve the static file system from 'esc'
	http.Handle("/", http.FileServer(FS(false)))

	// Setup the button handler to toggle the pin.
	http.HandleFunc("/press", buttonHandler)
	
	err = http.ListenAndServe(":80", nil)
	if err != nil {
		log.Fatal("Failed start server on port 80: ", err)
	}
}
