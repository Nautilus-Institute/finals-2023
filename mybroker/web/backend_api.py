from typing import Any, Optional, Tuple
import json
import socket
import struct
import pickle

import requests


INVENTORY_BACKEND_URL = "http://[::1]:8900/"
FILEMAN_BACKEND_URL = "http://[::1]:8903/"
KVSTORE_BACKEND_HOST = "::1"
KVSTORE_BACKEND_PORT = 8902
FILEUP_BACKEND_HOST = "::1"
FILEUP_BACKEND_PORT = 8906
INTEGRITY_BACKEND_HOST = "::1"
INTEGRITY_BACKEND_PORT = 8909


def list_inventory() -> list[str]:
    try:
        r = requests.get(INVENTORY_BACKEND_URL + "api/v1/inventory/list")
    except Exception:
        return [ ]
    if r.status_code == 200:
        data = json.loads(r.content)
        return data
    else:
        return [ ]


def add_house(**kwargs) -> bool:
    try:
        r = requests.post(
            INVENTORY_BACKEND_URL + "api/v1/inventory/new",
            json=dict(kwargs),
        )
        return r.status_code == 200
    except Exception:
        return False


def remove_house(method, idx) -> bool:
    try:
        r = requests.post(
            INVENTORY_BACKEND_URL + f"api/v1/inventory/{method}/{idx}",
        )
        return r.status_code == 200
    except Exception:
        return False


def kv_store(key: str, value: Any) -> bool:
    result = False
    sock = socket.socket(socket.AF_INET6, socket.SOCK_STREAM, 0)
    try:
        sock.connect((KVSTORE_BACKEND_HOST, KVSTORE_BACKEND_PORT, 0, 0))
    except Exception as ex:
        return False
    if isinstance(value, str):
        sock.send(b"store_value\n")
        sock.send(key.encode("utf-8") + b"\n")
        sock.send(b"string\n")
        sock.send(value.encode("utf-8") + b"\n")
        r = sock.recv(1024)
        if b"saved." in r:
            result = True

    sock.close()
    return result


def kv_load(key: str, typ) -> Optional[Any]:
    result = None
    sock = socket.socket(socket.AF_INET6, socket.SOCK_STREAM, 0)
    try:
        sock.connect((KVSTORE_BACKEND_HOST, KVSTORE_BACKEND_PORT, 0, 0))
    except Exception:
        return None
    if typ is str:
        sock.send(b"load_value\n")
        sock.send(key.encode("utf-8") + b"\n")
        sock.send(b"string\n")
        r = sock.recv(1024)
        if b"No such key." in r:
            result = None
        else:
            # handle more types of data using Python's greatness
            try:
                result = pickle.loads(r)
                return str(result)  # ensure type matches
            except Exception:
                pass

            try:
                result = eval(r.decode("utf-8"))
                return str(result)  # ensure type matches
            except Exception:
                pass

            result = r.decode("utf-8").strip("\n")

    sock.close()
    return result


def add_house_picture(key: str, content: bytes) -> str:

    def _send_message(msg: bytes):
        sock.send(struct.pack("<I", len(msg)) + msg)

    def _recv_message():
        dt = sock.recv(4)
        if len(dt) != 4:
            return b"FAILED"
        length = struct.unpack("<I", dt)[0]
        return sock.recv(length)

    sock = socket.socket(socket.AF_INET6, socket.SOCK_STREAM, 0)
    try:
        sock.connect((FILEUP_BACKEND_HOST, FILEUP_BACKEND_PORT, 0, 0))
    except Exception:
        return ""
    _send_message(b"store_file")
    _send_message(key.encode("utf-8"))
    _send_message(content)
    local_file_name = _recv_message()
    r = _recv_message()
    if b"SAVED" in r:
        return local_file_name.decode("utf-8")
    # failed...
    return ""


def get_house_picture(file_path: str) -> bytes:
    r = requests.get(FILEMAN_BACKEND_URL + file_path)
    if r.status_code != 200:
        return b""
    return r.content


def obfuscate_param(param: str) -> Optional[Tuple[str, str]]:
    sock = socket.socket(socket.AF_INET6, socket.SOCK_STREAM, 0)
    try:
        sock.connect((INTEGRITY_BACKEND_HOST, INTEGRITY_BACKEND_PORT, 0, 0))
    except Exception:
        return ("FAILED", "FAILED")
    sock.send(json.dumps([0, param]).encode("ascii") + b"\n\n")
    r = sock.recv(4096).decode("ascii")
    try:
        lst = json.loads(r)
    except json.JSONDecodeError:
        return "FAILED", "FAILED"
    return (lst[0], lst[1]) if len(lst) == 2 else ("FAILED", "FAILED")


def deobfuscate_param(o0: str, o1: str) -> Optional[str]:
    sock = socket.socket(socket.AF_INET6, socket.SOCK_STREAM, 0)
    try:
        sock.connect((INTEGRITY_BACKEND_HOST, INTEGRITY_BACKEND_PORT, 0, 0))
    except Exception:
        return None
    sock.send(json.dumps([1, o0, o1]).encode("ascii") + b"\n\n")
    r = sock.recv(4096).decode("ascii")
    try:
        lst = json.loads(r)
    except json.JSONDecodeError:
        return None
    return lst[1] if len(lst) == 2 and lst[0] == "SUCCEED" else None
