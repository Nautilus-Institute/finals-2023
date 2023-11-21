#!/usr/bin/env python3
from scapy.layers.eap import EAPOL
from scapy.layers.dot11 import *
from scapy.layers.l2 import LLC, SNAP
from scapy.fields import *
from scapy.arch import str2mac, get_if_raw_hwaddr
import sys
import struct
import random
import threading

xfun_packet = (
            RadioTap()
            / Dot11(type=0,
                subtype=13,
                addr1=b"\x02\x00\x00\x00\x00\x00",
                addr2=b"\x02\x00\x00\x00\x01\x00",
                addr3=b"\x02\x00\x00\x00\x01\x00"
            ))

callback = None

def random_string():
 return b"echolocation"

def naut_print(value):
    print(value, file=sys.stderr)

def print_team(value):
    print(value)
    naut_print(value)

def next():
    global Q
    if len(Q) == 0:
        sys.exit(0)
        return
    n = Q[0]
    Q = Q[1:]
    n()

def test_position():
    def reply(p):
        data = p[Dot11].load[126:]
        if b'45.' not in data or b'30.' not in data:
            print_team("positioning failure")
            sys.exit(-1)
        next()
    global callback
    callback = reply
    sendp(xfun_packet / b"\x17\x00\x00\x00", iface="mon1", verbose=False)



def test_uptime():
    def reply(p):
        data = p[Dot11].load[126:]
        if b'Uptime' not in data:
            print_team("uptime failure")
            sys.exit(-1)
        next()
    global callback
    callback = reply
    sendp(xfun_packet / b"\x17\x00\x01\x00", iface="mon1", verbose=False)


def test_attest():
    next()

def test_model_info():
    def reply(p):
        data = p[Dot11].load[34:]
        patterns = [b"blyatcopter", b"vladblade", b"Red Star Linux"]
        for x in patterns:
          if x not in data:
            print_team("model info failure")
            sys.exit(-1)
        next()
    global callback
    callback = reply
    sendp(xfun_packet / b"\x17\x00\x03\x00", iface="mon1", verbose=False)


def test_dmai():
    x = random_string()
    inlen = len(x) + 8
    outlen = inlen

    def reply(p):
        data = p[Dot11].load[34:]
        if x not in data:
            print_team("dmai info failure")
            sys.exit(-1)
        next()
    global callback
    callback = reply
    sendp(xfun_packet / (b"\x17\x00\x05\x00" + struct.pack("<LL", inlen, outlen) + x ), iface="mon1", verbose=False)

global counter
counter = 0
def test_dmao():
    pos_type = random.randint(0, 18)
    value = 0x1337babe0000 | random.randint(0, 2**31)

    global counter
    counter = 0
    def reply(p):
        global counter
        counter += 1
        if counter == 1:
            # wait for 2nd reply to verify value
            return
        data = p[Dot11].load[34-8:]
        if struct.unpack("<Q", data[:8])[0] != value:
            print_team("dmao failure")
            sys.exit(-1)
        next()
    global callback
    callback = reply
    # set
    sendp(xfun_packet / (b"\x17\x00\x06\x00" + struct.pack("<QQ", pos_type, value) ), iface="mon1", verbose=False)
    # verify
    sendp(xfun_packet / (b"\x17\x00\x06\x00" + struct.pack("<QQ", pos_type, value) ), iface="mon1", verbose=False)

def psk_reset():
    sendp(xfun_packet / (b"\x17\x00\x08\x00" + b"\x00"*16), iface="mon1", verbose=False)
    sendp(xfun_packet / (b"\x17\x00\x10\x00" + b"\x00"*4), iface="mon1", verbose=False)
    # this will disconnect us. but now we need flag
    next()

Q = [test_position, test_uptime, test_attest, test_model_info,
    test_dmai, test_dmao, psk_reset]

def recv_pkt(packet):
    global callback
    if packet[Dot11].addr1 != '02:00:00:00:01:00':
        return
    #print("packet came in", packet, packet[Dot11].subtype)
    if packet[Dot11].subtype == 13:
        #print("got recv pkt", packet)
        callback(packet)

class Sniffer(threading.Thread):
    def __init__(self):
        threading.Thread.__init__(self)

    def run(self):
        #print("sniffer running now.")
        sniff(iface="mon0", prn=recv_pkt, store=0)

s = Sniffer()
s.start()

import time
time.sleep(1)
next()
