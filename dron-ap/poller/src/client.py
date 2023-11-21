#!/usr/bin/env python3
"""
WiFi Client
"""
import random
import hmac, hashlib
import os
import fcntl
import sys
import pyaes
import threading
import binascii
import subprocess
from itertools import count
from scapy.layers.eap import EAPOL
from scapy.layers.dot11 import *
from scapy.layers.l2 import LLC, SNAP
from scapy.fields import *
from scapy.arch import str2mac, get_if_raw_hwaddr
from ccmp import *

from ccmp import *

from ap import TunInterface
from fakenet import ScapyNetwork
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


### Constants

# CCMP, PSK=WPA2
eRSN = Dot11EltRSN(
    ID=48,
    len=20,
    version=1,
    mfp_required=0,
    mfp_capable=0,
    group_cipher_suite=RSNCipherSuite(cipher="CCMP-128"),
    nb_pairwise_cipher_suites=1,
    pairwise_cipher_suites=RSNCipherSuite(cipher="CCMP-128"),
    nb_akm_suites=1,
    akm_suites=AKMSuite(suite="PSK"),
)
RSN = eRSN.build()

AP_RATES = b"\x0c\x12\x18\x24\x30\x48\x60\x6c"

DOT11_MTU = 4096

DOT11_TYPE_MANAGEMENT = 0
DOT11_TYPE_CONTROL = 1
DOT11_TYPE_DATA = 2

DOT11_SUBTYPE_DATA = 0x00
DOT11_SUBTYPE_PROBE_REQ = 0x04
DOT11_SUBTYPE_AUTH_REQ = 0x0B
DOT11_SUBTYPE_ASSOC_REQ = 0x00
DOT11_SUBTYPE_REASSOC_REQ = 0x02
DOT11_SUBTYPE_QOS_DATA = 0x28

# Ripped from scapy-latest with fixes
class EAPOL_KEY(Packet):
    name = "EAPOL_KEY"
    fields_desc = [
        ByteEnumField("key_descriptor_type", 1, {1: "RC4", 2: "RSN"}),
        # Key Information
        BitField("reserved2", 0, 2),
        BitField("smk_message", 0, 1),
        BitField("encrypted_key_data", 0, 1),
        BitField("request", 0, 1),
        BitField("error", 0, 1),
        BitField("secure", 0, 1),
        BitField("has_key_mic", 1, 1),
        BitField("key_ack", 0, 1),
        BitField("install", 0, 1),
        BitField("key_index", 0, 2),
        BitEnumField("key_type", 0, 1, {0: "Group/SMK", 1: "Pairwise"}),
        BitEnumField(
            "key_descriptor_type_version",
            0,
            3,
            {1: "HMAC-MD5+ARC4", 2: "HMAC-SHA1-128+AES-128", 3: "AES-128-CMAC+AES-128"},
        ),
        #
        LenField("key_length", None, "H"),
        LongField("key_replay_counter", 0),
        XStrFixedLenField("key_nonce", b"\x00" * 32, 32),
        XStrFixedLenField("key_iv", b"\x00" * 16, 16),
        XStrFixedLenField("key_rsc", b"\x00" * 8, 8),
        XStrFixedLenField("key_id", b"\x00" * 8, 8),
        XStrFixedLenField("key_mic", b"\x00" * 16, 16),  # XXX size can be 24
        LenField("wpa_key_length", None, "H"),
        ConditionalField(
            XStrLenField(
                "key", b"\x00" * 16, length_from=lambda pkt: pkt.wpa_key_length
            ),
            lambda pkt: pkt.wpa_key_length and pkt.wpa_key_length > 0,
        ),
    ]

    def extract_padding(self, s):
        return s[: self.key_length], s[self.key_length :]

    def hashret(self):
        return struct.pack("!B", self.type) + self.payload.hashret()

    def answers(self, other):
        if (
            isinstance(other, EAPOL_KEY)
            and other.descriptor_type == self.descriptor_type
        ):
            return 1
        return 0


def if_hwaddr(iff):
    return str2mac(get_if_raw_hwaddr(iff)[1])

class Client:
    def __init__(self, ssid, psk, mac=None, mode="stdio", iface="mon1", netmode="tunnel"):
        self.mode = mode
        self.stations = {}
        self.PSK = psk
        self.cur_ssid = ssid
        self.target_bssid = None
        self.iface = iface
        if self.mode == "iface":
            mac = if_hwaddr(iface)
        if not mac:
          raise Exception("Need a mac")
        else:
          self.mac = mac
        self.channel = 1
        self.mutex = threading.Lock()
        self.sc = 0
        self.aid = 0
        self.boottime = time()
        self.hidden = False
        self.connected = 0
        self.eapol_state = 0
        self.snonce = b""
        self.anonce = b""
        self.PMK = b""
        self.PTK = b""
        self.KCK = b""
        self.KEK = b""
        self.TK  = b""
        self.MIC_AP_TO_STA = b""
        self.MIC_STA_TO_AP = b""
        self.client_iv = count()
        self.group_iv = count()

        if netmode == "tunnel":
            # use a TUN device
            self.network = TunInterface(self, name="scapycli2")
        else:
            # use a fake scapy network
            self.network = ScapyNetwork(self) #IP tbd

    def get_radiotap_header(self):
        return RadioTap()

    def get_ssid(self):
        return bytes(self.cur_ssid, "ascii")

    def next_sc(self):
        self.sc = (self.sc + 1) % 4096
        temp = self.sc
        self.mutex.acquire()
        self.mutex.release()

        return temp * 16  # Fragment number -> right 4 bits

    def next_aid(self):
        self.mutex.acquire()
        self.aid = (self.aid + 1) % 2008
        temp = self.aid
        self.mutex.release()
        return temp

    def current_timestamp(self):
        return int((time() - self.boottime) * 1000000)

    def gen_gtk(self):
        self.gtk_full = open("/dev/urandom", "rb").read(32)
        self.GTK = self.gtk_full[:16]
        self.MIC_AP_TO_GROUP = self.gtk_full[16:24]
        self.group_IV = count()

    def init_ptk(self, ptk=b"\x00"*64):
        self.PTK = PTK = ptk
        self.KCK = PTK[:16]
        self.KEK = PTK[16:32]
        self.TK  = PTK[32:48]
        self.MIC_AP_TO_STA = PTK[48:56]
        self.MIC_STA_TO_AP = PTK[56:64]
        self.client_iv = count()

    def send_eapol2(self, packet):
        eapol_key = EAPOL_KEY(packet.getlayer(EAPOL).payload.load)
        anonce = eapol_key.key_nonce
        if anonce == self.anonce:
            self.init_ptk()
            return
        self.anonce = anonce

        self.PMK = hashlib.pbkdf2_hmac('sha1', self.PSK.encode(), self.get_ssid(), 4096, 32)
        self.snonce = bytes([random.randrange(256) for i in range(32)])

        amac = bytes.fromhex(self.bssid.replace(':', ''))
        smac = bytes.fromhex(self.mac.replace(':', ''))

        ptk = customPRF512(self.PMK, amac, smac, self.anonce, self.snonce)
        self.init_ptk(ptk)

        header =  self.get_radiotap_header() \
                    / Dot11(subtype=0, FCfield='to-DS', addr1=self.bssid, addr2=self.mac, addr3=self.bssid, SC=self.next_sc()) \
                    / LLC(dsap=0xaa, ssap=0xaa, ctrl=3) \
                    / SNAP(OUI=0, code=0x888e)

        m2_packet = EAPOL(version='802.1X-2004',type='EAPOL-Key') \
                    / EAPOL_KEY(key_descriptor_type=2, key_descriptor_type_version=2, key_type=1, key_ack=0, has_key_mic=1, key_replay_counter=1, key_nonce=self.snonce, key_length=0, wpa_key_length=22, key=RSN)

        m2_packet.key_mic = hmac.new(self.KCK, m2_packet.build(), hashlib.sha1).digest()[:16]

        self.sendp(header / m2_packet, verbose=False)
        self.eapol_state = 1

    def send_eapol4(self, packet):
        #verify MIC in packet makes sense
        eapol = packet[EAPOL]
        ek = EAPOL_KEY(eapol.payload.load)

        given_mic = ek.key_mic
        to_check = eapol.build().replace(ek.key_mic, b"\x00"*len(ek.key_mic))
        computed_mic = hmac.new(self.KCK, to_check, hashlib.sha1).digest()[:16]
        if given_mic != computed_mic:
            printd("[-] Invalid MIC from AP. Dropping EAPOL key exchange message %s %s %s" % (packet.addr1, packet.addr2, packet.addr3))
            printd("%s vs %s" %(computed_mic, given_mic))
            printd(packet.show(dump=1))
            os.exit(-1)
            return

        # install GTK from packet
        unwrap = aes_unwrap(self.KEK, ek.key)
        RSN_info=Dot11EltRSN(unwrap)
        self.gtk_full = AKMSuite(RSN_info[6].info).load[2:]
        self.GTK = self.gtk_full[:16]
        self.MIC_AP_TO_GROUP = self.gtk_full[16:24]

        header = self.get_radiotap_header() \
                    / Dot11(subtype=0, FCfield='to-DS', addr1=self.bssid, addr2=self.mac, addr3=self.bssid, SC=self.next_sc()) \
                    / LLC(dsap=0xaa, ssap=0xaa, ctrl=3) \
                    / SNAP(OUI=0, code=0x888e)
        m4_packet =  EAPOL(version='802.1X-2004',type='EAPOL-Key') \
                    / EAPOL_KEY(key_descriptor_type=2, key_descriptor_type_version=2, key_type=1, key_ack=0, has_key_mic=1, key_replay_counter=2, key_length=0)

        m4_packet.key_mic = hmac.new(self.KCK, m4_packet.build(), hashlib.sha1).digest()[:16]
        self.sendp(header / m4_packet, verbose=False)
        self.connected = 4
        self.eapol_state = 2
        printd("[+] Happy, auth'd")
        sys.exit(0) #auth successful

    def do_send(self, packet):
        packet =  self.get_radiotap_header() \
                    / self.encrypt(packet, key_idx=0)
        self.sendp(packet)

    def connect(self, packet):
        # create an association requestcon
        printd("current conn to " + packet.addr2)
        self.bssid = packet.addr2
        ssid = self.get_ssid()

        if not ssid:
            printd("no ssid")
            return
        assoc_packet = self.get_radiotap_header() \
                       / Dot11(subtype=0, FCfield='to-DS', addr1=self.bssid, addr2=self.mac, addr3=self.bssid, SC=self.next_sc()) \
                       / Dot11AssoReq(cap=0x3101) \
                       / Dot11Elt(ID='SSID', info=ssid) \
                       / Dot11Elt(ID="Rates", info=AP_RATES) / RSN

        self.sendp(assoc_packet, verbose=False)

    def recv_pkt(self, packet):
        if packet.addr2 == self.mac:
            #printd("drop %s" % packet.addr1)
            #printd(packet.show(dump=1))
            return

        #if packet.addr1 != 'ff:ff:ff:ff:ff:ff':
        #    printd("got packet in %s" % packet.addr1)

        if self.connected == 0:
            if Dot11Beacon in packet:
                if self.target_bssid is not None and packet.addr3 != self.target_bssid:
                    # ignore this beacon, not the right bssid.
                    return
                #printd("send auth req")
                printd(self.target_bssid)
                bssid = packet.addr2
                auth_packet = (
                    self.get_radiotap_header()
                    / Dot11(
                        subtype=0x0B,
                        FCfield='to-DS',
                        addr1=bssid,
                        addr2=self.mac,
                        addr3=bssid,
                        SC=self.next_sc(),
                    )
                    / Dot11Auth(seqnum=0x01)
                )

                printd("Sending Authentication to %s (0x0B)..." % bssid, Level.DEBUG)
                self.connected = 1
                self.sendp(auth_packet, verbose=False)

        if self.connected == 1 and Dot11Auth in packet:
            #got auth response, send assoc
            if packet.addr2 == self.mac:
                return
            if self.target_bssid is None or packet.addr2 == self.target_bssid:
                printd('sending association request')
                self.connect(packet)
            return
        if self.connected == 1 and Dot11AssoResp in packet:
            self.connected = 2

        if self.connected > 1 and EAPOL in packet:
            da = packet[Dot11].addr1
            if packet[Dot11].FCfield != 'from-DS':
                return
            if da != self.mac:
                return
            if self.eapol_state == 0:
                self.send_eapol2(packet)
            elif self.eapol_state == 1:
                self.send_eapol4(packet)
                printd("Fully Authenticated to server", Level.DEBUG)

        if Dot11CCMP in packet and self.connected > 3:
            if packet[Dot11].FCfield != 'from-DS+protected':
                return
            decrypted = self.decrypt(packet)
            if decrypted:
                    printd("got decrypted data...")
                    printd(decrypted.show(dump=1))
                    self.network.write(decrypted) #packet from AP

    def decrypt(self, packet):
        ccmp = packet[Dot11CCMP]
        pn = ccmp_pn(ccmp)
        return self.decrypt_ccmp(packet, self.TK, self.GTK)

    def encrypt(self, packet, key_idx=0):
        key = ""
        if key_idx == 0:
            pn = next(self.client_iv)
            key = self.TK
        else:
            pn = next(self.group_iv)
            key = self.GTK
        return self.encrypt_ccmp(packet, key, pn, key_idx)

    def encrypt_ccmp(self, p, tk, pn, keyid=0, amsdu_spp=False):
        # Takes a plaintext ethernet frame and encrypt and wrap it into a Dot11/DotCCMP
        # Add the CCMP header. res0 and res1 are by default set to zero.
        SA = p[Ether].src
        DA = p[Ether].dst
        newp = Dot11(
            type="Data",
            FCfield="to-DS+protected",
            addr1=self.bssid,
            addr2=self.mac,
            addr3=DA,
            SC=self.next_sc(),
        )
        newp = newp / Dot11CCMP()

        pn_bytes = pn2bytes(pn)
        newp.PN0, newp.PN1, newp.PN2, newp.PN3, newp.PN4, newp.PN5 = pn_bytes
        newp.key_id = keyid
        newp.ext_iv = 1
        priority = 0  # ...
        ccm_nonce = ccmp_get_nonce(priority, newp.addr2, pn)
        ccm_aad = ccmp_get_aad(newp, amsdu_spp)
        header = LLC(dsap=0xAA, ssap=0xAA, ctrl=3) / SNAP(OUI=0, code=p[Ether].type)
        payload = (header / p.payload).build()  # strip ethernet. wrap with LLC
        ciphertext, tag = CCMPCrypto.run_ccmp_encrypt(tk, ccm_nonce, ccm_aad, payload)
        newp.data = ciphertext + tag
        return newp

    def decrypt_ccmp(self, p, tk, gtk, verify=True, dir='from_ap'):
        # Takes a Dot11CCMP frame and decrypts it
        keyid = p.key_id
        if keyid == 0:
            pass
        elif keyid == 1:
            tk = gtk
        else:
            raise Exception("unknown key id", key_id)

        priority = dot11_get_priority(p)
        pn = dot11_get_iv(p)

        ccm_nonce = ccmp_get_nonce(priority, p.addr2, pn)
        ccm_aad = ccmp_get_aad(p[Dot11])

        payload = p[Dot11CCMP].data
        tag = payload[-8:]
        payload = payload[:-8]
        plaintext, valid = CCMPCrypto.run_ccmp_decrypt(
            tk, ccm_nonce, ccm_aad, payload, tag
        )
        if verify and not valid:
            printd("[-] ERROR on ccmp decrypt, invalid tag")
            return None
        llc = LLC(plaintext)
        # convert into an ethernet packet.
        DA = p.addr3
        SA = p.addr2
        if dir == 'from_ap':
            DA = p.addr1
            SA = p.addr3
        return Ether(
            addr2bin(DA)
            + addr2bin(SA)
            + struct.pack(">H", llc.payload.code)
            + llc.payload.payload.build()
        )

    def run(self):
        if self.mode == "iface":
            sniff(iface=self.iface, prn=self.recv_pkt, store=0)
            return
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
        if self.mode == "stdio":
            x = packet.build()
            sys.stdout.buffer.write(struct.pack("<L", len(x)) + x)
            sys.stdout.buffer.flush()
            return

        assert self.mode == "iface"
        printd("xmit packet. %s" % packet.addr1)
        sendp(packet, iface=self.iface, verbose=False)

if __name__ == "__main__":
    #client = Client("turtlnet", "password1234", mac="66:66:66:66:66:66", mode="stdio")
    #client = Client("turtlenet", "password1234", mac="02:00:00:00:01:00", mode="iface", iface="mon1")
    import signal
    #signal.alarm(30)
    client = Client("dron-ap", os.getenv("PSK"), mac="02:00:00:00:01:00", mode="stdio", netmode="fakenet")
    client.run()
