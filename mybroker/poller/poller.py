from typing import Optional, Tuple, List
import os
import string
import sys
import random
import hashlib
import time

import requests



import logging
import contextlib
try:
    from http.client import HTTPConnection # py3
except ImportError:
    from httplib import HTTPConnection # py2

def debug_requests_on():
    '''Switches on logging of the requests module.'''
    HTTPConnection.debuglevel = 1

    logging.basicConfig()
    logging.getLogger().setLevel(logging.DEBUG)
    requests_log = logging.getLogger("requests.packages.urllib3")
    requests_log.setLevel(logging.DEBUG)
    requests_log.propagate = True

# debug_requests_on()


def _new_conn(self):
    sock = old_new_conn(self)
    time.sleep(3)
    return sock

old_new_conn = requests.packages.urllib3.connection.HTTPConnection._new_conn
requests.packages.urllib3.connection.HTTPConnection._new_conn = _new_conn


DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 11342
DEFAULT_SEED = 5
DEFAULT_FLAG = "flag{jFfMITrIcktwvI09u125:aEwmdS3LHTed4haTLCWo}"


def _randstr(rnd, n: int) -> str:
    charset = string.digits + string.ascii_letters
    return "".join([rnd.choice(charset) for _ in range(n)])


class House:
    def __init__(self, address, bedrooms, bathrooms, price, picture: Optional[bytes]):
        self.address = address
        self.bedrooms = bedrooms
        self.bathrooms = bathrooms
        self.price = price
        self.picture = picture

    @classmethod
    def gen(cls, rnd) -> "House":
        address = _randstr(rnd, 20)
        bedrooms = rnd.randint(0, 5)
        bathrooms = rnd.randint(0, 5)
        price = rnd.randint(200000, 1200000)
        has_pic = rnd.randint(0, 1) == 1
        if has_pic:
            picture = _randstr(rnd, 16384).encode("ascii")
        else:
            picture = None
        return House(address, bedrooms, bathrooms, price, picture)


class MybrokerClient:
    def __init__(self, base_url, seed=1, flag="flag{FLAG}"):
        self.base_url = base_url
        self.seed = seed
        self.flag = flag

        self.rnd = random.Random()
        self.rnd.seed(self.seed)

        self.users = [ ]
        self.houses: List[House] = [ ]
        self.current_user: Optional[Tuple[str, str]] = None
        self.session = requests.Session()

    def sequence_0(self) -> bool:
        r = self.check_register_page()
        if not r:
            return False
        r = self.register_new_user()
        if not r:
            r = self.login(*self.users[0])
            if not r:
                print("Failed to log in")
                return False
        for _ in range(5):
            self.houses.append(House.gen(self.rnd))

        for house in self.houses:
            r = self.add_house(house.address, house.bedrooms, house.bathrooms, house.price, house.picture)
            if not r:
                print("Failed to add a new house")
                return False

        # check all houses
        inventory = self.get_inventory()
        for house in self.houses:
            if house.address not in inventory:
                print("Failed to find a new house in the inventory")
                return False
            # check if the picture URL exists
            if house.picture is not None:
                data = inventory[inventory.index(house.address) : ]
                if "/get_picture?path=" not in data:
                    print("Failed to find a picture path")
                    return False
                pic_url = data[data.index("/get_picture?path=") : ]
                pic_url = pic_url[:pic_url.index("\"")]
                if "path=" not in pic_url:
                    print("\"path\" is not in the picture URL")
                    return False
                if "&key=" not in pic_url:
                    print("\"key\" is not in the picture URL")
                    return False
                if "-" not in pic_url:
                    print("Unexpected path format in the picture URL")
                    return False
                pic = self.download_picture(pic_url)
                if pic != house.picture:
                    print("Unexpected picture downloaded from the server")
                    return False

        # TODO: Remove all houses

        # TODO: Check that houses have been removed

        return True

    def get_suggested_password(self) -> str:
        m = hashlib.sha256(self.flag.strip("\n").encode("ascii")).hexdigest()
        return m[10:20]

    def randstr(self, n: int) -> str:
        charset = string.digits + string.ascii_letters
        return "".join([self.rnd.choice(charset) for _ in range(n)])

    def check_register_page(self) -> bool:
        r = self.session.get(f"{self.base_url}/register")
        if r.status_code != 200:
            print("Failed to load the register page.")
            return False
        if self.get_suggested_password() not in r.text:
            print("Cannot find the suggested password")
            return False
        return True

    def check_active_user(self) -> bool:
        if self.current_user is not None:
            r = self.session.get(f"{self.base_url}/")
            if f"<li>{self.current_user[0]}</li>" not in r.text:
                print("Active user list is probably wrong")
                return False
        return True

    def register_new_user(self) -> bool:
        username = self.randstr(8)
        password = self.randstr(10)
        email = f"{username}@gmail.com"
        realname = self.randstr(20)

        self.users.append((username, password))

        r = self.session.post(
            f"{self.base_url}/register",
            data={
                "username": username,
                "password": password,
                "email": email,
                "realname": realname,
            },
            allow_redirects=False,
        )
        return r.status_code == 302

    def login(self, username, password) -> bool:
        r = self.session.post(
            f"{self.base_url}/login",
            data={
                "username": username,
                "password": password,
            },
            allow_redirects=False,
        )
        if r.status_code == 302:
            self.current_user = (username, password)
            return True
        return False

    def add_house(self, house_address: str, bathrooms: int, bedrooms: int, price: int, picture: Optional[bytes]) -> bool:
        r = self.session.post(
            f"{self.base_url}/add_house",
            data={
                "address": house_address,
                "bathrooms": bathrooms,
                "bedrooms": bedrooms,
                "price": price,
            },
            files={"pic": picture},
            allow_redirects=False
        )
        if r.status_code == 302:
            return True
        return False

    def get_inventory(self) -> str:
        r = self.session.get(
            f"{self.base_url}/inventory"
        )
        return r.text

    def download_picture(self, pic_url: str) -> Optional[bytes]:
        r = self.session.get(
            f"{self.base_url}{pic_url}",
        )
        if r.status_code != 200:
            return None
        return r.content


def main():
    # load parameters
    host = os.environ.get("HOST", DEFAULT_HOST)
    port = os.environ.get("PORT", DEFAULT_PORT)
    seed = int(os.environ.get("SEED", DEFAULT_SEED))  # it's actually the private tick
    if os.path.isfile("/flag"):
        with open("/flag", "r") as f:
            flag = f.read()
    else:
        flag = os.environ.get("FLAG", DEFAULT_FLAG)

    client = MybrokerClient(f"http://{host}:{port}/", seed, flag)
    try:
        r = client.sequence_0()
    except (TimeoutError, requests.Timeout) as ex:
        print("Connection timeout!")
        sys.exit(-2)
    except (ConnectionError, requests.ConnectionError) as ex:
        print("Connection error")
        sys.exit(-3)
    if r:
        sys.exit(0)
    sys.exit(-1)


if __name__ == "__main__":
    main()
