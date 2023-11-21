import random
from typing import Optional
import enum
import time
import datetime


class PromptType(enum.IntEnum):
    ATTACK = 1
    DEFENSE = 2


class CompetitionResult(enum.IntEnum):
    WIN = 1
    LOSE = 2
    TIE = 3


def current_tick() -> Optional[int]:
    """
    Get the current tick from the game db.
    """

    # TODO: Cache the result
    # TODO: Implement it
    return 1

FRUITS = [
    "Apple", "Apricot", "Avocado", "Banana", "Blackberry", "Blueberry", "Boysenberry", "Breadfruit",
    "Cantaloupe", "Cherry", "Chico", "Clementine", "Cloudberry", "Coconut", "Cranberry", "Date",
    "Dragonfruit", "Durian", "Elderberry", "Feijoa", "Fig", "Gooseberry", "Grape", "Grapefruit",
    "Guava", "Honeydew", "Huckleberry", "Jabuticaba", "Jackfruit", "Jambolan", "Kiwi", "Kumquat",
    "Lemon", "Lime", "Lychee", "Mango", "Mangosteen", "Mulberry", "Nance", "Nectarine", "Orange",
    "Papaya", "Passionfruit", "Peach", "Pear", "Persimmon", "Pineapple", "Plum", "Pomegranate",
    "Pomelo", "Prune", "Raspberry", "Rambutan", "Redcurrant", "Salak", "Salal berry", "Sapote",
    "Satsuma", "Soursop", "Star apple", "Star fruit", "Strawberry", "Surinam cherry", "Tamarillo",
    "Tamarind", "Tangerine", "Ugli fruit", "Watermelon", "White currant", "Yellow passionfruit",
    "Ackee", "Bael", "Bilberry", "Black sapote", "Blackcurrant", "Blood orange", "Buddha's hand",
    "Cempedak", "Cherimoya", "Crowberry", "Cupuacu", "Damson", "Dewberry", "Elderberry", "Finger lime",
    "Goji berry", "Gooseberry", "Guinep", "Jujube", "Juneberry", "Langsat", "Longan", "Loquat",
    "Morus", "Olive", "Pitahaya", "Pluot", "Pulasan", "Quince", "Rose apple", "Santol", "Sea buckthorn",
    "Serviceberry", "Snake fruit", "Sycamore", "Tangelo", "Wood apple"
]


def generate_random_secret(seed: int) -> str:
    r = random.Random(seed)
    secret = [ ]
    for _ in range(6):
        secret.append(r.choice(FRUITS).lower())
    return " ".join(secret)


def generate_secret(t: datetime.datetime) -> str:
    secret = generate_random_secret(int(time.mktime(t.timetuple())) // 300 * 300)
    return secret
