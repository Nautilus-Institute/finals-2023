# Abstract fake IP network.
import threading
from scapy.layers.dhcp import *
from scapy.layers.inet import *
from scapy.layers.l2 import ARP

class ScapyNetwork(threading.Thread):
    def __init__(self, bss, ip="10.10.10.1/24"):
        threading.Thread.__init__(self)
        self.bss = bss

        if "/" in ip:
            parts = ip.split("/")
            if parts[1] != "24":
                raise Exception("other net class not implemented")
            self.ip = parts[0]

        # hack, ipaddress.ip_network is a lot of extra code w/out benefit
        self.subnet = '.'.join(self.ip.split('.')[:3])
        self.txq = []
        self.macs = []
        self.data_ready = threading.Condition()


    def write(self, packet):
        self.data_ready.acquire()
        self.txq.append(packet)
        self.data_ready.notify()
        self.data_ready.release()

    def transmit(self, deth, packet):
        self.bss.ap.tun_data_incoming(self.bss, deth, packet)

    def input(self, incoming):
        #ip to mac address map
        m = {}
        m[self.ip] = self.bss.mac
        for i in range(len(self.macs)):
            m["%s.%d" % (self.subnet, i+1)] = self.macs[i]

        if DHCP in incoming:
            #handle a dhcp packet
            if incoming[UDP].dport == 67:
                if incoming[BOOTP].op == 1:
                    req_type = next(opt[1] for opt in incoming[DHCP].options if isinstance(opt, tuple) and opt[0] == 'message-type')
                    if req_type == 1:
                        self.reply_dhcp_offer(incoming)
                    elif req_type == 3:
                        self.reply_dhcp_ack(incoming)
        elif ARP in incoming:
                if incoming[ARP].sprintf("%ARP.op%") != 'who-has':
                    return
                if incoming.pdst in m:
                    if incoming.pdst in m:
                        d = m[incoming.pdst]
                    else:
                        return
                    reply = ARP(op=2, hwsrc=d, psrc=incoming.pdst, hwdst=incoming.src, pdst=incoming.psrc)
                    go = Ether(dst=incoming.src, src=self.bss.mac) / reply
                    self.transmit(incoming.src, go.build())
        elif ICMP in incoming:
            if incoming[ICMP].type == 8:
                sender_ip = self.ip

                eth_ip_header = Ether(src=self.bss.mac, dst=incoming[Ether].src) \
                   / IP(dst=incoming[IP].src, src=sender_ip) \

                icmp = None
                if incoming[IP].dst not in m:
                    #unknown dst. drop and reply w/ icmp fail
                    icmp = ICMP(type=3, code=1) / incoming.payload.build()[:64]
                else:
                    icmp = ICMP(type=0, seq=incoming[ICMP].seq, id=incoming[ICMP].id) \
                            / incoming[ICMP].load

                    # update sender IP
                    eth_ip_header[IP].src = incoming[IP].dst
                    #choose mac addr of host
                    eth_ip_header[Ether].src = m[incoming[IP].dst]

                reply_packet = eth_ip_header \
                   / icmp
                self.transmit(incoming.src, reply_packet.build())
        elif UDP in incoming:
            eth_ip_header = Ether(src=self.bss.mac, dst=incoming[Ether].src) \
                    / IP(dst=incoming[IP].src, src=self.ip)
            icmp = ICMP(type=3, code=2) / incoming.payload.build()[:64]
            reply_packet = eth_ip_header / icmp
            self.transmit(incoming.src, reply_packet.build())
        elif TCP in incoming:
            # reject TCP
            eth_ip_header = Ether(src=self.bss.mac, dst=incoming[Ether].src) \
                    / IP(dst=incoming[IP].src, src=self.ip)
            icmp = ICMP(type=3, code=2) / incoming.payload.build()[:64]
            reply_packet = eth_ip_header / icmp
            self.transmit(incoming.src, reply_packet.build())
        else:
            printd("smtg else")
            printd(incoming.show(dump=1))


    def reply_dhcp_offer(self, incoming):
        # generate an IP
        if incoming.src not in self.macs:
            self.macs.append(incoming.src)
        dest_ip = "%s.%d" % (self.subnet, 1 + len(self.macs))

        deth = incoming.src
        smac = bytes.fromhex(deth.replace(':', ''))
        broadcast = "%s.255"%self.subnet
        gateway = server = self.ip
        netmask = "255.255.255.0"

        packet = Ether(dst='ff:ff:ff:ff:ff:ff', src=self.bss.mac, type=0x800) \
                 / IP(dst="255.255.255.255", src=self.ip) \
                 / UDP(sport=67, dport=68) \
                 / BOOTP(op=2, htype=1, yiaddr=dest_ip, siaddr=self.ip, chaddr=smac, xid=incoming[BOOTP].xid) \
                 / DHCP(options=[("message-type", "offer"), ("server_id", server), ("broadcast_address", broadcast), ("router", gateway), ("subnet_mask", netmask)])

        printd("send dhcp offer to " + deth)
        self.transmit(deth, packet.build())

    def reply_dhcp_ack(self, incoming):
        # generate an IP
        if incoming.src not in self.macs:
            self.macs.append(incoming.src)
        dest_ip = "%s.%d" % (self.subnet, 2 + self.macs.index(incoming.src))

        deth = incoming.src
        smac = bytes.fromhex(deth.replace(':', ''))
        broadcast = "%s.255"%self.subnet
        gateway = server = self.ip
        netmask = "255.255.255.0"

        packet = Ether(dst='ff:ff:ff:ff:ff:ff', src=self.bss.mac, type=0x800) \
                 / IP(dst="255.255.255.255", src=self.ip) \
                 / UDP(sport=67, dport=68) \
                 / BOOTP(op=2, htype=1, yiaddr=dest_ip, siaddr=self.ip, chaddr=smac, xid=incoming[BOOTP].xid) \
                 / DHCP(options=[("message-type", "ack"), ("server_id", server), ("broadcast_address", broadcast), ("lease_time", 1337), ("router", gateway), ("subnet_mask", netmask)])
        printd("send dhcp ack to " + deth)
        self.transmit(deth, packet.build())

    def run(self):
        self.data_ready.acquire()
        counter = 0
        while True:
            for incoming in self.txq:
                self.input(incoming)
            self.txq = []
            counter += 1
            self.data_ready.wait()
        self.data_ready.release()
