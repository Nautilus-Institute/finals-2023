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
import os
from time import sleep


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

def naut_print(value):
    print(value, file=sys.stderr)

def print_team(value):
    print(value)
    naut_print(value)

def random_string():
 return b"echolocation"

xfun_packet = (
            RadioTap()
            / Dot11(type=0,
                subtype=13,
                addr1=b"\x02\x00\x00\x00\x00\x00",
                addr2=b"\x02\x00\x00\x00\x01\x00",
                addr3=b"\x02\x00\x00\x00\x01\x00"
            ))

class UAVClient:
    def __init__(self, mac=None, mode="stdio"):
        self.mode = mode
        if self.mode == "iface":
            mac = if_hwaddr(iface)
            raise Exception("Not supported")
        if not mac:
          raise Exception("Need a mac")
        else:
          self.mac = mac
        self.channel = 1
        self.mutex = threading.Lock()
        self.callback = None

        self.Q = [self.test_position, self.test_uptime, self.test_attest, self.test_model_info,
            self.test_dmai, self.test_dmao, self.psk_reset]

    def get_radiotap_header(self):
        return RadioTap()

    def next(self):
        if len(self.Q) == 0:
            naut_print("[+] Success")
            sys.exit(0)
            return
        n = self.Q[0]
        self.Q = self.Q[1:]
        n()

    def test_position(self):
        def reply(self, p):
            data = p[Dot11].load[126:]
            if b'45.' not in data or b'30.' not in data:
                print_team("positioning failure")
                sys.exit(-1)
            self.next()
        self.callback = reply
        self.sendp(xfun_packet / b"\x17\x00\x00\x00")

    def test_uptime(self):
        def reply(self, p):
            data = p[Dot11].load[126:]
            if b'Uptime' not in data:
                print_team("uptime failure")
                sys.exit(-1)
            self.next()
        self.callback = reply
        self.sendp(xfun_packet / b"\x17\x00\x01\x00")

    def test_attest(self):
        self.next()

    def test_model_info(self):
        def reply(self, p):
            data = p[Dot11].load[34:]
            patterns = [b"blyatcopter", b"vladblade", b"Red Star Linux"]
            for x in patterns:
              if x not in data:
                print_team("model info failure")
                sys.exit(-1)
            self.next()
        self.callback = reply
        self.sendp(xfun_packet / b"\x17\x00\x03\x00")

    def test_dmai(self):
        x = random_string()
        inlen = len(x) + 8
        outlen = inlen

        def reply(self, p):
            data = p[Dot11].load[34:]
            if x not in data:
                print_team("dmai info failure")
                sys.exit(-1)
            self.next()
        self.callback = reply
        self.sendp(xfun_packet / (b"\x17\x00\x05\x00" + struct.pack("<LL", inlen, outlen) + x ))

    def test_dmao(self):
        pos_type = random.randint(0, 18)
        value = 0x1337babe0000 | random.randint(0, 2**31)

        self.counter = 0
        def reply(self, p):
            self.counter += 1
            if self.counter == 1:
                # wait for 2nd reply to verify value
                return
            data = p[Dot11].load[34-8:]
            if struct.unpack("<Q", data[:8])[0] != value:
                print_team("dmao failure")
                sys.exit(-1)
            self.next()
        self.callback = reply
        # set
        self.sendp(xfun_packet / (b"\x17\x00\x06\x00" + struct.pack("<QQ", pos_type, value) ))
        # verify
        self.sendp(xfun_packet / (b"\x17\x00\x06\x00" + struct.pack("<QQ", pos_type, value) ))

    def psk_reset(self):
        self.sendp(xfun_packet / (b"\x17\x00\x08\x00" + b"\x00"*16))
        self.sendp(xfun_packet / (b"\x17\x00\x10\x00" + b"\x00"*4))
        # this will disconnect us. but now we need flag
        self.next()

    def recv_pkt(self, packet):
        #        if packet.addr2 == self.mac:
        if packet[Dot11].addr1 != '02:00:00:00:01:00':
            return
        #printd("good got a packet")
        #print("packet came in", packet, packet[Dot11].subtype)
        if packet[Dot11].subtype == 13:
            #print("got recv pkt", packet)
            self.callback(self, packet)

    def run(self):
        time.sleep(3)
        self.next()
        assert self.mode == "stdio"
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
                  self.recv_pkt(p)
                  qdata = qdata[4 + wanted:]

    def sendp(self, packet, verbose=False):
        assert self.mode == "stdio"
        x = packet.build()
        sys.stdout.buffer.write(struct.pack("<L", len(x)) + x)
        sys.stdout.buffer.flush()
        return

if __name__ == "__main__":
    import signal
    signal.alarm(10)
    client = UAVClient(mac="02:00:00:00:01:00", mode="stdio")
    client.run()
