import hashlib


def get_suggested_password() -> str:
    with open("/flag", "rb") as f:
        data = f.read().strip(b"\n")
    m = hashlib.sha256(data).hexdigest()
    return m[10:20]


def sanitize(s: str) -> str:
    # no code execution!
    return "".join([ch for ch in s if ch not in "\"'.()"])
