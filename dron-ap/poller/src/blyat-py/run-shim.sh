#!/bin/sh
# hostapd is already running on wlan0

# we run a packet shim from port 4444 to the wlan1 interface

while true; do socat exec:/src/blyat-py/shim.py tcp6-listen:4444,reuseaddr; done
