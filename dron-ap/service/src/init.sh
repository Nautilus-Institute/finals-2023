#!/bin/bash
set -e
insmod /lib/modules/blyat.ko

ip link set dev wlan1 down
#iw dev wlan1 set channel 1

ip link set dev wlan0 down
#iw dev wlan0 set channel 1

iw dev wlan1 interface add mon1 type monitor
iw dev wlan0 interface add mon0 type monitor

# bump mtu to handle radiotap overhead.
ip link set dev wlan1 mtu 2200
ip link set dev mon1 mtu 2200
ip link set dev wlan0 mtu 2200
ip link set dev mon0 mtu 2200

ip link set dev wlan1 up
ip link set dev mon1 up

ip link set dev wlan0 up
ip link set dev mon0 up


# set ip address for AP
ip addr add dev wlan0 fc00::1/64

new_psk=$(echo -ne "$(cat /flag)" | shasum -a 256 | head -c 40)

cat > /etc/psk << EOF
00:00:00:00:00:01 defcon2023-wifi
00:00:00:00:00:00 ${new_psk}
EOF

`which sshd`

hostapd -B /radios/h.conf 2>&1 > /dev/null


#/blyat-py/run-shim.sh

# run wifi shim on stdio
/blyat-py/shim.py
