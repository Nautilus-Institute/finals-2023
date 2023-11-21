#!/usr/bin/env python3
"""
WiFi Client SHIM
"""
import random
import hmac, hashlib
import os
import fcntl
import sys
import threading
import binascii
import subprocess
from itertools import count
from scapy.layers.eap import EAPOL
from scapy.layers.dot11 import *
from scapy.layers.l2 import LLC, SNAP
from scapy.fields import *
from scapy.arch import str2mac, get_if_raw_hwaddr

from time import time, sleep

class Level:
    CRITICAL = 0
    WARNING = 1
    INFO = 2
    DEBUG = 3
    BLOAT = 4

VERBOSITY = Level.BLOAT

def printd(string, level=Level.INFO):
    if VERBOSITY >= level:
        print(string, file=sys.stderr)

def if_hwaddr(iff):
    return str2mac(get_if_raw_hwaddr(iff)[1])

class Shim:
    def __init__(self, mac=None, mode="stdio", iface="mon1", netmode="tunnel"):
        self.mode = mode
        self.iface = iface
        if self.mode == "iface":
            mac = if_hwaddr(iface)
        if not mac:
          raise Exception("Need a mac")
        else:
          self.mac = mac
        self.channel = 1
        self.mutex = threading.Lock()

    def get_radiotap_header(self):
        return RadioTap()


    def do_send(self, packet):
        packet =  self.get_radiotap_header() \
                    / self.encrypt(packet, key_idx=0)
        self.sendp(packet)

    def recv_pkt(self, packet):
        if packet.addr2 == self.mac: #packet[Dot11].FCfield != 'from-DS':
            return
        self.sendp(packet)

    def recv_pkt_two(self, packet):
        #mon0 needs a packet going to the station with from-ds set
        if packet.addr1 != self.mac:
            if 'from-DS' not in packet[Dot11].FCfield:
              return
        if packet[Dot11].subtype != 13:
          return
        #printd("mon0 sending to network %s %s" % (packet.addr1, packet.addr2))
        self.sendp(packet)

    def run(self):
        recv_pkt = self.recv_pkt

        class Sniffer(threading.Thread):
          def __init__(self, iface):
            threading.Thread.__init__(self)
            self.daemon = True
            self.interval = 0.1
            self.iface=iface

          def run(self):
            sniff(iface=self.iface, prn=recv_pkt, store=0)

        recv_pkt2 = self.recv_pkt_two


        class SnifferDron(threading.Thread):
          def __init__(self, iface):
            threading.Thread.__init__(self)
            self.daemon = True
            self.interval = 0.1
            self.iface=iface

          def run(self):
            sniff(iface=self.iface, prn=recv_pkt2, store=0)

        assert self.mode == "stdio"
        x = Sniffer(self.iface)
        x.start()


        x = SnifferDron("mon0")
        x.start()

        os.set_blocking(sys.stdin.fileno(), False)
        qdata = b""
        while True:
          sleep(0.01)
          data = sys.stdin.buffer.read(65536)
          if data:
              qdata += data
          if len(qdata) > 4:
              wanted = struct.unpack("<L", qdata[:4])[0]
              if len(qdata) + 4 >= wanted:
                  p = RadioTap(qdata[4:4 + wanted])
                  #send network packet to iface
                  #if p.addr1 != 'ff:ff:ff:ff:ff:ff':
                  #  printd("incoming packet %s %s" % (p.addr1, p.addr2))
                  sendp(p, iface=self.iface, verbose=False)
                  qdata = qdata[4 + wanted:]

    def sendp(self, packet, verbose=False):
        if self.mode == "stdio":
            #if True: # packet.addr1 != 'ff:ff:ff:ff:ff:ff':
            #  printd("txmit packet src %s dst %s" % (packet.addr2, packet.addr1))
            x = packet.build()
            sys.stdout.buffer.write(struct.pack("<L", len(x)) + x)
            sys.stdout.buffer.flush()
            return

        assert self.mode == "iface"
        sendp(packet, iface=self.iface, verbose=False)

if __name__ == "__main__":
    #client = Shim(mac="66:66:66:66:66:66", mode="stdio")
    client = Shim(mac="02:00:00:00:01:00", mode="stdio", iface="mon1")
    client.run()
