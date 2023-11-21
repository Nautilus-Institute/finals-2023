# Dron-AP

Challenge Author: adc (alex@supernetworks.org)

Dron-AP is a wifi based kernel module for exploitation in A&D during Defcon 2023's CTF Finals 

The code is a modified mac_hwsim which hooks frames to handle a custom "blyat UAV" action code. 

Hostapd is running a fully function Access Point (https://github.com/Nautilus-Institute/finals-2023/blob/main/dron-ap/service/src/init.sh#L39),
and teams could send wifi frames to trigger the vulnerabilities in the custom code. 

The wifi frames are tunneled over a TCP proxy, so teams could treat this challenge like an ordinary ctf task.

There's also another low-hanging vulnerability outside the kernel module, that teams may not have found during the CTF. 
