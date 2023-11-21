import os

from . import view, api

if "APP_INIT" in os.environ:
    from .portal import app, db, init_test_data

    with app.app_context():
        db.create_all()

if "FLASK_DEBUG" in os.environ:
    from .portal import app, db, init_test_data

    with app.app_context():
        db.create_all()
        init_test_data()
