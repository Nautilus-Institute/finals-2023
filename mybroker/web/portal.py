import os

from flask import Flask
from flask_sqlalchemy import SQLAlchemy


basedir = os.path.abspath(os.path.dirname(__file__))

app = Flask(__name__)
app.secret_key = b"MHmWVmjc8WAyrbgdajTH"

if "FLASK_DEBUG" in os.environ:
    print("[+] Using an ephemeral memory database in DEBUG mode.")
    app.config["SQLALCHEMY_DATABASE_URI"] = os.environ["DATABASE"] if "DATABASE" in os.environ else "sqlite:///:memory:"
else:
    app.config['SQLALCHEMY_DATABASE_URI'] = 'sqlite:///' + (os.environ["DATABASE"] if "DATABASE" in os.environ else os.path.join(basedir, 'database.db'))

db = SQLAlchemy(app)


def init_test_data():
    from .model import User

    print("[+] Adding test data.")

    # register users
    u = User(username="test", password="test_password", realname="Test User", email="test@test.com")
    db.session.add(u)
    db.session.commit()
