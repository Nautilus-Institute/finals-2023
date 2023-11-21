"""
This is a self-contained, bare-bones access point using Scapy.

It can:
1) Respond to Probe requests
2) Allow a station to associate with WPA2 + CCMP
3) Send traffic to/from a TAP tunnel device

It has no protocol security -- use at your own risk.

This is built with snippets from several sources:
1) pyaes https://github.com/ricmoo/pyaes
2) libwifi https://github.com/vanhoefm/libwifi
3) https://github.com/rpp0/scapy-fakeap
4) hostapd's testing suite
"""
import sys
import random
import hmac, hashlib
import os
import fcntl

from itertools import count
import threading
import binascii
import subprocess

from scapy.layers.eap import EAPOL
from scapy.layers.dot11 import *
from scapy.layers.l2 import LLC, SNAP
from scapy.fields import *
from scapy.arch import str2mac, get_if_raw_hwaddr

from fakenet import ScapyNetwork
from ccmp import *
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
country = Dot11Elt(ID='Country', info=(b"\x55\x53\x00\x01\x0b\x17"))
ht_caps = Dot11EltHTCapabilities()
ht_info = Dot11Elt(ID=61, info=(b"\x0b\x08\x05\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"))
extcaps = Dot11Elt(ID='ExtendedCapatibilities', info=(b"\x01\x10\x08\x00\x00\x00\x00\x00"))



#AP_RATES = b"\x0c\x12\x18\x24\x30\x48\x60\x6c"
AP_RATES = b"\x82\x84\x0b\x16\x96"

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


IFNAMSIZ = 16
IFF_TUN = 0x0001
IFF_TAP = 0x0002
IFF_NO_PI = 0x1000
TUNSETIFF = 0x400454CA


def if_hwaddr(iff):
    return str2mac(get_if_raw_hwaddr(iff)[1])

def set_ip_address(dev, ip):
    if subprocess.call(["ip", "addr", "add", ip, "dev", dev]):
        printd("Failed to assign IP address %s to %s." % (ip, dev), Level.CRITICAL)

    if subprocess.call(["ip", "route", "add", "10.10.10.0/24", "dev", dev]): #tbd parse ip and fix subnet
        printd("Failed to assign IP route 10.10.10.0/24 to %s." % (dev), Level.CRITICAL)

def set_if_up(dev):
    if subprocess.call(["ip", "link", "set", "dev", dev, "up"]):
        printd("Failed to bring device %s up." % dev, Level.CRITICAL)

def set_if_addr(dev, addr):
    if subprocess.call(["ip", "link", "set", "dev", dev, "addr", addr]):
        printd("Failed to set device %s add to %s." % (dev, addr), Level.CRITICAL)


class TunInterface(threading.Thread):
    def __init__(self, bss, ip=None, name="scapyap"):
        threading.Thread.__init__(self)

        if len(name) > IFNAMSIZ:
            raise Exception("Tun interface name cannot be larger than " + str(IFNAMSIZ))

        self.name = name
        self.daemon = True
        self.bss = bss
        self.ip = ip

        # Virtual interface
        self.fd = os.open("/dev/net/tun", os.O_RDWR)
        ifr_flags = IFF_TAP | IFF_NO_PI  # Tun device without packet information
        ifreq = struct.pack("16sH", name.encode('ascii'), ifr_flags)
        fcntl.ioctl(self.fd, TUNSETIFF, ifreq)  # Syscall to create interface

        set_if_up(name)
        #update addr
        set_if_addr(name, self.bss.mac)
        # Assign IP and bring interface up
        if self.ip:
          set_ip_address(name, self.ip)

        printd(
            "Created TUN interface %s at %s. Bind it to your services if needed."
            % (name, self.ip)
        )

    def write(self, pkt):
        os.write(self.fd, pkt.build())

    def read(self):
        try:
            raw_packet = os.read(self.fd, DOT11_MTU)
            return raw_packet
        except Exception as e:
            printd(e)

    def close(self):
        os.close(self.fd)

    def run(self):
        while True:
            raw_packet = self.read()
            sta = Ether(raw_packet).dst
            self.bss.ap.tun_data_incoming(self.bss, sta, raw_packet)


class Station:
    def __init__(self, mac):
        self.mac = mac
        self.associated = False

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

class BSS:
    def __init__(self, ap, ssid, mac, psk, ip="10.10.10.1/24", mode="tunnel" ):
        self.ap = ap
        self.ssid = ssid
        self.mac = mac
        self.PSK = psk
        self.ip =  ip
        self.sc = 0
        self.aid = 0
        self.stations = {}
        self.GTK = b""
        self.group_IV = count()
        self.mutex = threading.Lock()
        if mode == "tunnel":
            # use a TUN device
            self.network = TunInterface(self, ip="10.10.10.1")
        else:
            # use a fake scapy network
            self.network = ScapyNetwork(self, ip=ip)

    def next_sc(self):
        self.mutex.acquire()
        self.sc = (self.sc + 1) % 4096
        temp = self.sc
        self.mutex.release()

        return temp * 16  # Fragment number -> right 4 bits

    def next_aid(self):
        self.mutex.acquire()
        self.aid = (self.aid + 1) % 2008
        temp = self.aid
        self.mutex.release()
        return temp

    def gen_gtk(self):
        self.gtk_full = open("/dev/urandom", "rb").read(32)
        self.GTK = self.gtk_full[:16]
        self.MIC_AP_TO_GROUP = self.gtk_full[16:24]
        self.group_IV = count()


class AP:
    def __init__(self, ssid, psk, mac=None, mode="stdio", iface="wlan0"):
        self.iface = iface
        self.mode = mode
        if self.mode == "iface":
            mac = if_hwaddr(iface)
        if not mac:
          raise Exception("Need a mac")
        else:
          self.mac = mac
        self.channel = 1
        self.boottime = time()

        self.bssids = {mac: BSS(self, ssid, mac, psk, "10.10.0.1/24")}
        self.beaconTransmitter = self.BeaconTransmitter(self)

    def ssids(self):
        return [bss[x].ssid for x in self.bssids]

    def get_radiotap_header(self):
        return RadioTap()

    def get_ssid(self, mac):
        if mac not in self.bssids:
            return None
        return self.bssids[mac].ssid

    def current_timestamp(self):
        return int((time() - self.boottime) * 1000000)

    def tun_data_incoming(self, bss, sta, incoming):
        p = Ether(incoming)
        self.enc_send(bss, sta, p)

    def recv_pkt(self, packet):
        try:
            if len(packet.notdecoded[8:9]) > 0:  # Driver sent radiotap header flags
                # This means it doesn't drop packets with a bad FCS itself
                flags = ord(packet.notdecoded[8:9])
                if flags & 64 != 0:  # BAD_FCS flag is set
                    # Print a warning if we haven't already discovered this MAC
                    if not packet.addr2 is None:
                        printd(
                            "Dropping corrupt packet from %s" % packet.addr2,
                            Level.BLOAT,
                        )
                    # Drop this packet
                    return

            if EAPOL in packet:
                # send message 3
                self.create_eapol_3(packet)
            elif Dot11CCMP in packet:
                if packet[Dot11].FCfield == "to-DS+protected":
                    sta = packet[Dot11].addr2
                    bssid = packet[Dot11].addr1
                    if bssid not in self.bssids:
                        printd("[-] Invalid bssid destination for packet")
                        return
                    decrypted = self.decrypt(bssid, sta, packet)
                    if decrypted:
                        # make sure that the ethernet src matches the station,
                        # otherwise block
                        if sta != decrypted[Ether].src:
                            printd("[-] Invalid mac address for packet")
                            return
                        #self.tunnel.write(decrypted)
                        #printd("write to %s from %s" % (bssid, sta))
                        self.bssids[bssid].network.write(decrypted) #packet from a client
                    else:
                        printd("failed to decrypt %s to %s" % (sta, bssid))
                    return

            # Management
            if packet.type == DOT11_TYPE_MANAGEMENT:
                if packet.subtype == DOT11_SUBTYPE_PROBE_REQ:  # Probe request
                    if Dot11Elt in packet:
                        ssid = packet[Dot11Elt].info

                        printd(
                            "Probe request for SSID %s by MAC %s"
                            % (ssid, packet.addr2),
                            Level.DEBUG,
                        )

                        if Dot11Elt in packet and packet[Dot11Elt].len == 0:
                            # for empty return primary ssid
                            self.dot11_probe_resp(self.mac, packet.addr2, self.bssids[self.mac].ssid)
                        else:
                            # otherwise return match
                            for x in self.bssids:
                                # otherwise only respond to a match
                                if self.bssids[x].ssid == ssid:
                                    self.dot11_probe_resp(x, packet.addr2, ssid)
                                    break
                elif packet.subtype == DOT11_SUBTYPE_AUTH_REQ:  # Authentication
                    bssid = packet.addr1
                    if bssid in self.bssids:  # We are the receivers
                        self.bssids[bssid].sc = -1 # Reset sequence number
                        self.dot11_auth(bssid, packet.addr2)
                elif (
                    packet.subtype == DOT11_SUBTYPE_ASSOC_REQ
                    or packet.subtype == DOT11_SUBTYPE_REASSOC_REQ
                ):
                    if packet.addr1 in self.bssids:
                        self.dot11_assoc_resp(packet, packet.addr2, packet.subtype)
        except SyntaxError as err:
            printd("Unknown error at monitor interface: %s" % repr(err))

    def dot11_probe_resp(self, bssid, source, ssid):
        printd("send probe response to " +  source)
        probe_response_packet = (
            self.get_radiotap_header()
            / Dot11(
                subtype=5,
                addr1=source,
                addr2=bssid,
                addr3=bssid,
                SC=self.bssids[bssid].next_sc(),
            )
            / Dot11ProbeResp(
                timestamp=self.current_timestamp(), beacon_interval=0x0064, cap=0x3101
            )
            / Dot11Elt(ID="SSID", info=ssid)
            / Dot11Elt(ID="Rates", info=AP_RATES)
            / Dot11Elt(ID="DSset", info=chr(self.channel))
            / country / Dot11EltHTCapabilities()
        )

        # If we are an RSN network, add RSN data to response
        probe_response_packet = probe_response_packet / RSN

        self.sendp(probe_response_packet, verbose=False)

    def dot11_auth(self, bssid, receiver):
        bss = self.bssids[bssid]
        auth_packet = (
            self.get_radiotap_header()
            / Dot11(
                subtype=0x0B,
                addr1=receiver,
                addr2=bssid,
                addr3=bssid,
                SC=bss.next_sc(),
            )
            / Dot11Auth(seqnum=0x02)
        )

        printd("Sending Authentication  from %s to %s (0x0B)..." % (receiver, bssid), Level.DEBUG)
        self.sendp(auth_packet, verbose=False)

    def create_eapol_3(self, message_2):
        bssid = message_2.getlayer(Dot11).addr1
        sta = message_2.getlayer(Dot11).addr2

        if sta in self.bssids:
            return

        if bssid not in self.bssids:
            return

        bss = self.bssids[bssid]

        if sta not in bss.stations:
            printd("bss %s does not know station  %s" % (bss, sta))
            return

        if not bss.stations[sta].eapol_ready:
            printd("station %s not eapol ready" % sta)
            return

        eapol_key = EAPOL_KEY(message_2.getlayer(EAPOL).payload.load)

        snonce = eapol_key.key_nonce

        amac = bytes.fromhex(bssid.replace(":", ""))
        smac = bytes.fromhex(sta.replace(":", ""))

        stat = bss.stations[sta]
        stat.PMK = PMK = hashlib.pbkdf2_hmac(
            "sha1", bss.PSK.encode(), bss.ssid.encode(), 4096, 32
        )
        # UM do we need to sort here
        stat.PTK = PTK = customPRF512(PMK, amac, smac, stat.ANONCE, snonce)
        stat.KCK = PTK[:16]
        stat.KEK = PTK[16:32]
        stat.TK = PTK[32:48]
        stat.MIC_AP_TO_STA = PTK[48:56]
        stat.MIC_STA_TO_AP = PTK[56:64]
        stat.client_iv = count()

        #verify message 2 key mic matches before proceeding
        #verify MIC in packet makes sense
        in_eapol = message_2[EAPOL]
        ek = EAPOL_KEY(in_eapol.payload.load)
        given_mic = ek.key_mic
        to_check = in_eapol.build().replace(ek.key_mic, b"\x00"*len(ek.key_mic))
        computed_mic = hmac.new(stat.KCK, to_check, hashlib.sha1).digest()[:16]
        if given_mic != computed_mic:
            printd("[-] Invalid MIC from STA. Dropping EAPOL key exchange message and station")
            printd("my bssid " + bssid)
            printd('my psk ' + bss.PSK)
            printd('amac ' + bssid)
            printd('smac ' + sta)
            printd(b'KCK ' + binascii.hexlify(stat.KCK))
            printd(b'PMK' + binascii.hexlify(stat.PMK))
            printd(b'TK' + binascii.hexlify(stat.PTK))
            printd(b'given mic' + binascii.hexlify(given_mic))
            printd(b'computed mic' + binascii.hexlify(computed_mic))
            deauth =    self.get_radiotap_header() \
                        / Dot11(
                            addr1=sta,
                            addr2=bssid,
                            addr3=bssid
                        ) \
                        / Dot11Deauth(reason=1)
            # relax auth failure
            #self.sendp(deauth, verbose=False)
            #del bss.stations[sta]
            return

        bss.stations[sta].eapol_ready = False

        if bss.GTK == b"":
            bss.gen_gtk()

        stat.KEY_IV = bytes([0 for i in range(16)])

        gtk_kde = b"".join(
            [
                chb(0xDD),
                chb(len(bss.GTK) + 6),
                b"\x00\x0f\xac",
                b"\x01\x00\x00",
                bss.GTK,
                b"\xdd\x00",
            ]
        )
        plain = pad_key_data(RSN + gtk_kde)
        keydata = aes_wrap(stat.KEK, plain)

        ek = EAPOL(version="802.1X-2004", type="EAPOL-Key") / EAPOL_KEY(
            key_descriptor_type=2,
            key_descriptor_type_version=2,
            install=1,
            key_type=1,
            key_ack=1,
            has_key_mic=1,
            secure=1,
            encrypted_key_data=1,
            key_replay_counter=2,
            key_nonce=stat.ANONCE,
            key_mic=(b"\x00" * 16),
            key_length=16,
            key=keydata,
            wpa_key_length=len(keydata),
        )

        ek.key_mic = hmac.new(stat.KCK, ek.build(), hashlib.sha1).digest()[:16]

        m3_packet = (
            self.get_radiotap_header()
            / Dot11(
                subtype=0,
                FCfield="from-DS",
                addr1=sta,
                addr2=bssid,
                addr3=bssid,
                SC=bss.next_sc(),
            )
            / LLC(dsap=0xAA, ssap=0xAA, ctrl=3)
            / SNAP(OUI=0, code=0x888E)
            / ek
        )

        self.sendp(m3_packet, verbose=False)
        stat.associated = True
        printd("[+] New associated station %s for bssid %s" % (sta, bssid))

        bss.stations[sta] = stat

    def create_message_1(self, bssid, sta):
        if sta in self.bssids:
            return

        if bssid not in self.bssids:
            return

        bss = self.bssids[bssid]

        if sta not in bss.stations:
            return

        stat = bss.stations[sta]
        stat.ANONCE = anonce = bytes([random.randrange(256) for i in range(32)])
        m1_packet = (
            self.get_radiotap_header()
            / Dot11(
                subtype=0,
                FCfield="from-DS",
                addr1=sta,
                addr2=bssid,
                addr3=bssid,
                SC=bss.next_sc(),
            )
            / LLC(dsap=0xAA, ssap=0xAA, ctrl=3)
            / SNAP(OUI=0, code=0x888E)
            / EAPOL(version="802.1X-2004", type="EAPOL-Key")
            / EAPOL_KEY(
                key_descriptor_type=2,
                key_descriptor_type_version=2,
                key_type=1,
                key_ack=1,
                has_key_mic=0,
                key_replay_counter=1,
                key_nonce=anonce,
                key_length=16,
            )
        )
        stat.eapol_ready = True
        printd("sent eapol m1 " + sta)
        self.sendp(m1_packet, verbose=False)
        bss.stations[sta] = stat

    def dot11_assoc_resp(self, packet, sta, reassoc):
        bssid = packet.addr1
        bss = self.bssids[bssid]
        if sta not in bss.stations:
            bss.stations[sta] = Station(sta)

        response_subtype = 0x01
        if reassoc == 0x02:
            response_subtype = 0x03
        self.eapol_ready = True
        assoc_packet = (
            self.get_radiotap_header()
            / Dot11(
                subtype=response_subtype,
                addr1=sta,
                addr2=bssid,
                addr3=bssid,
                SC=bss.next_sc(),
            )
            / Dot11AssoResp(cap=0x3101, status=0, AID=bss.next_aid())
            / Dot11EltRates(rates=[130, 132, 139, 150, 12, 18, 24, 36])
            / Dot11Elt(ID="DSset", info=chr(self.channel))
            / country / Dot11EltHTCapabilities()
        )

        printd("Sending Association Response (0x01)...")
        self.sendp(assoc_packet, verbose=False)
        self.create_message_1(bssid, sta)

    def decrypt(self, bssid, sta, packet):
        if bssid not in self.bssids:
            return
        bss = self.bssids[bssid]
        ccmp = packet[Dot11CCMP]
        pn = ccmp_pn(ccmp)
        if sta not in bss.stations:
            printd("[-] Unknown station %s" % sta)
            deauth =    self.get_radiotap_header() \
                        / Dot11(
                            addr1=sta,
                            addr2=bssid,
                            addr3=bssid
                        ) \
                        / Dot11Deauth(reason=9)
            self.sendp(deauth, verbose=False)
            return None
        station = bss.stations[sta]
        return self.decrypt_ccmp(packet, station.TK, bss.GTK)

    def encrypt(self, bss, sta, packet, key_idx):
        key = ""
        if key_idx == 0:
            pn = next(bss.stations[sta].client_iv)
            key = bss.stations[sta].TK
        else:
            pn = next(bss.group_IV)
            key = bss.GTK
        if len(key) == 0: return
        return self.encrypt_ccmp(bss, sta, packet, key, pn, key_idx)

    def enc_send(self, bss, sta, packet):
        key_idx = 0
        if is_multicast(sta) or is_broadcast(sta):
            printd('sending broadcast/multicast')
            key_idx = 1
        elif sta not in bss.stations or not bss.stations[sta].associated:
            printd("[-] Invalid station %s for enc_send" % sta)
            return
        x = self.get_radiotap_header()
        y = self.encrypt(bss, sta, packet, key_idx)
        if not y:
            return
            raise Exception("wtfbbq")
        new_packet = x / y
        #printd(new_packet.show(dump=1))
        self.sendp(new_packet, verbose=False)

    def encrypt_ccmp(self, bss, sta, p, tk, pn, keyid=0, amsdu_spp=False):
        # Takes a plaintext ethernet frame and encrypt and wrap it into a Dot11/DotCCMP
        # Add the CCMP header. res0 and res1 are by default set to zero.
        SA = p[Ether].src
        DA = p[Ether].dst
        newp = Dot11(
            type="Data",
            FCfield="from-DS+protected",
            addr1=sta,
            addr2=bss.mac,
            addr3=SA,
            SC=bss.next_sc(),
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
        payload = (header / p.payload).build()
        ciphertext, tag = CCMPCrypto.run_ccmp_encrypt(tk, ccm_nonce, ccm_aad, payload)
        newp.data = ciphertext + tag
        return newp

    def decrypt_ccmp(self, p, tk, gtk, verify=True, dir='to_ap'):
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
        # decrypting TO-AP. addr3/addr2.  if doing FROM-AP need to do addr1/addr3
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

    def dot11_beacon(self, bssid, ssid):
        # Create beacon packet
        beacon_packet = (
            self.get_radiotap_header()
            / Dot11(
                subtype=8, addr1="ff:ff:ff:ff:ff:ff", addr2=bssid, addr3=bssid
            )
            / Dot11Beacon(cap=0x3101)
            / Dot11Elt(ID="SSID", info=ssid)
            / Dot11EltRates(rates=[130, 132, 139, 150, 12, 18, 24, 36])
            / Dot11Elt(ID="DSset", info=chr(self.channel))
        )

        beacon_packet = beacon_packet / RSN

        # Update timestamp
        beacon_packet[Dot11Beacon].timestamp = self.current_timestamp()

        # Send
        self.sendp(beacon_packet, verbose=False)

    class BeaconTransmitter(threading.Thread):
        def __init__(self, ap):
            threading.Thread.__init__(self)
            self.ap = ap
            self.daemon = True
            self.interval = 0.1

        def run(self):
            while True:
                for bssid in self.ap.bssids.keys():
                    bss = self.ap.bssids[bssid]
                    self.ap.dot11_beacon(bss.mac, bss.ssid)
                # Sleep
                sleep(self.interval)

    def run(self):
        self.beaconTransmitter.start()
        for x in self.bssids:
            self.bssids[x].network.start()

        # in iface node, an interface in monitor mode is used
        # in stdio node, I/O is done via stdin and stdout.
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
        sendp(packet, iface=self.iface, verbose=False)


if __name__ == "__main__":
    ap = AP("turtlenet", "password1234", mac="02:00:00:00:00:00", mode="iface", iface="mon0")
    #ap = AP("turtlenet", "password1234", mac="44:44:44:00:00:00", mode="stdio")
    ap.run()
