#!/bin/bash
set -e

#HOST
#PORT

# run shim-remote on HOST:PORT

real_flag=$(cat /flag)
new_psk=$(echo -ne $real_flag | shasum -a 256 | head -c 40)
export PSK=${new_psk}

# run UAV checks
socat exec:/src/poller-script.py tcp-connect:$HOST:$PORT

socat exec:/src/client.py tcp-connect:$HOST:$PORT	

echo "[+] Wow. WiFi ROFLCopter, Good poll!"
exit 0
