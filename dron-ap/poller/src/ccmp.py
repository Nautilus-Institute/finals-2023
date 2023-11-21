import hashlib, hmac
from scapy.fields import *
from scapy.layers.dot11 import *
import binascii
import pyaes

def pad_key_data(plain):
    pad_len = len(plain) % 8
    if pad_len:
        plain += b"\xdd" * (8 - pad_len)
    return plain


#### Helpers from maty van hoef's libwifi
def ccmp_pn(pn):
    return pn.PN0 + (pn.PN1 << 8) + (pn.PN2 << 16) + (pn.PN3 << 24)


def addr2bin(addr):
    return binascii.a2b_hex(addr.replace(":", ""))


def ccmp_get_nonce(priority, addr, pn):
    """
    CCMP nonce = 1 byte priority, 6 byte sender addr, 6 byte PN.
    """
    return struct.pack("B", priority) + addr2bin(addr) + pn2bin(pn)


def ccmp_get_aad(p, amsdu_spp=False):
    # FC field with masked values
    fc = raw(p)[:2]
    # data mask
    fc = struct.pack("<BB", fc[0] & 0x8F, fc[1] & 0xC7)

    # Sequence number is masked, but fragment number is included
    sc = struct.pack("<H", p.SC & 0xF)

    addr1 = addr2bin(p.addr1)
    addr2 = addr2bin(p.addr2)
    addr3 = addr2bin(p.addr3)
    aad = fc + addr1 + addr2 + addr3 + sc
    if Dot11QoS in p:
        if not amsdu_spp:
            # Everything except the TID is masked
            aad += struct.pack("<H", p[Dot11QoS].TID)
        else:
            # TODO: Mask unrelated fields
            aad += raw(p[Dot11QoS])[:2]
    return aad


def pn2bytes(pn):
    pn_bytes = [0] * 6
    for i in range(6):
        pn_bytes[i] = pn & 0xFF
        pn >>= 8
    return pn_bytes


def pn2bin(pn):
    return struct.pack(">Q", pn)[2:]


def dot11_get_seqnum(p):
    return p.SC >> 4


def dot11_is_encrypted_data(p):
    # All these different cases are explicitly tested to handle older scapy versions
    return (
        (p.FCfield & 0x40)
        or Dot11CCMP in p
        or Dot11TKIP in p
        or Dot11WEP in p
        or Dot11Encrypted in p
    )

def payload_to_iv(payload):
    iv0 = payload[0]
    iv1 = payload[1]
    wepdata = payload[4:8]

    # FIXME: Only CCMP is supported (TKIP uses a different IV structure)
    return orb(iv0) + (orb(iv1) << 8) + (struct.unpack(">I", wepdata)[0] << 16)


def dot11_get_priority(p):
    if not Dot11QoS in p:
        return 0
    return p[Dot11QoS].TID


def dot11_get_iv(p):
    # The simple and default case
    if Dot11CCMP in p:
        payload = raw(p[Dot11CCMP])
        return payload_to_iv(payload)
    # Scapy uses Dot11Encrypted if it couldn't determine how the frame was encrypted. Assume CCMP.
    elif Dot11Encrypted in p:
        payload = raw(p[Dot11Encrypted])
        return payload_to_iv(payload)
    # Couldn't determine the IV
    return None


def is_broadcast(ether):
    return ether == "ff:ff:ff:ff:ff:ff"

def is_multicast(ether):
    return int(ether[0:2], 16) & 0x1 == 1

### CCMP wrapper. See RFC 3610
class CCMPCrypto:
    @staticmethod
    def cbc_mac(key, plaintext, aad, nonce, iv=b"\x00" * 16, mac_len=8):
        assert len(key) == len(iv) == 16  # aes-128
        assert len(nonce) == 13
        iv = int.from_bytes(iv, byteorder="big")
        assert len(aad) < (2**16 - 2**8)

        q = L = 2
        Mp = (mac_len - 2) // 2
        assert q == L
        has_aad = len(aad) > 0
        flags = 64 * has_aad + 8 * Mp + (q - 1)
        b_0 = struct.pack("B", flags) + nonce + struct.pack(">H", len(plaintext))
        assert len(b_0) == 16

        a = struct.pack(">H", len(aad)) + aad
        if len(a) % 16 != 0:
            a += b"\x00" * (16 - len(a) % 16)
        blocks = b_0 + a
        blocks += plaintext

        if len(blocks) % 16 != 0:
            blocks += b"\x00" * (16 - len(blocks) % 16)

        encrypt = pyaes.AESModeOfOperationECB(key).encrypt
        prev = iv
        for i in range(0, len(blocks), 16):
            inblock = int.from_bytes(blocks[i : i + 16], byteorder="big")
            outblock = encrypt(int.to_bytes(inblock ^ prev, length=16, byteorder="big"))
            prev = int.from_bytes(outblock, byteorder="big")

        # xor tag with E(0) construction using nonce in CTR mode
        xn = struct.pack("B", q - 1) + nonce + b"\x00" * L
        ctr_nonce = int.from_bytes(xn, byteorder="big")
        xctr = pyaes.AESModeOfOperationCTR(
            key, counter=pyaes.Counter(ctr_nonce)
        ).encrypt
        xs0 = xctr(b"\x00" * 16)
        s_0 = int.from_bytes(xs0, byteorder="big")

        return int.to_bytes(s_0 ^ prev, length=16, byteorder="big")[:mac_len]

    @staticmethod
    def ctr_encrypt(key, nonce, plaintext, q=2, L=2):
        xn = struct.pack("B", q - 1) + nonce + b"\x00" * L
        ctr_nonce = int.from_bytes(xn, byteorder="big")
        xctr = pyaes.AESModeOfOperationCTR(key, counter=pyaes.Counter(ctr_nonce))
        # start ctr
        _ = xctr.encrypt(b"\x00" * 16)
        return xctr.encrypt(plaintext)

    @staticmethod
    def run_ccmp_encrypt(key, nonce, aad, plaintext):
        tag = CCMPCrypto.cbc_mac(key, plaintext, aad, nonce)
        encrypted = CCMPCrypto.ctr_encrypt(key, nonce, plaintext)
        return encrypted, tag

    @staticmethod
    def run_ccmp_decrypt(key, nonce, aad, ciphertext, known_tag):
        valid = False
        # ctr encrypt/decrypt is symmetric
        plaintext = CCMPCrypto.ctr_encrypt(key, nonce, ciphertext)
        tag = CCMPCrypto.cbc_mac(key, plaintext, aad, nonce)
        # constant time compare validity of tag
        valid = hmac.compare_digest(tag, known_tag)
        return plaintext, valid

    @staticmethod
    def test():
        k = b"k" * 16
        a = b"a" * 22
        n = b"n" * 13
        p = b"P" * 128
        cipher, tag = CCMPCrypto.run_ccmp_encrypt(k, n, a, p)
        p2, verified = CCMPCrypto.run_ccmp_decrypt(k, n, a, cipher, tag)
        assert p == p2
        assert verified
        return True

def aes_wrap(kek, plain):
    n = len(plain) // 8
    a = 0xA6A6A6A6A6A6A6A6
    enc = pyaes.AESModeOfOperationECB(kek).encrypt
    r = [plain[i * 8 : (i + 1) * 8] for i in range(0, n)]
    for j in range(6):
        for i in range(1, n + 1):
            b = enc(struct.pack(">Q", a) + r[i - 1])
            a = struct.unpack(">Q", b[:8])[0] ^ (n * j + i)
            r[i - 1] = b[8:]
    return struct.pack(">Q", a) + b"".join(r)

def aes_unwrap(kek, wrapped):
    n = (len(wrapped) // 8) - 1
    #NOTE: R[0] is never accessed, left in for consistency with RFC indices
    r = [None] + [wrapped[i * 8:i * 8 + 8] for i in range(1, n + 1)]
    a = struct.unpack(">Q", wrapped[:8])[0]
    decrypt = pyaes.AESModeOfOperationECB(kek).decrypt
    for j in range(5, -1, -1):  #counting down
        for i in range(n, 0, -1):  #(n, n-1, ..., 1)
            ciphertext = struct.pack(">Q", a ^ (n * j + i)) + r[i]
            B = decrypt(ciphertext)
            a = struct.unpack(">Q", B[:8])[0]
            r[i] = B[8:]
    assert(a == 0xA6A6A6A6A6A6A6A6)
    return b"".join(r[1:])

def customPRF512(key, amac, smac, anonce, snonce):
    """Source https://stackoverflow.com/questions/12018920/"""
    A = b"Pairwise key expansion"
    B = b"".join(sorted([amac, smac]) + sorted([anonce, snonce]))
    num_bytes = 64
    R = b""
    for i in range((num_bytes * 8 + 159) // 160):
        R += hmac.new(key, A + chb(0x00) + B + chb(i), hashlib.sha1).digest()
    return R[:num_bytes]
