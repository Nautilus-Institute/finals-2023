import os.path
import urllib.parse

from flask import render_template, request, session, redirect, abort, Response

from .portal import app, db
from .model import User
from .backend_api import list_inventory, add_house, kv_load, kv_store, add_house_picture, \
    remove_house, get_house_picture, obfuscate_param, deobfuscate_param
from .api import get_suggested_password, sanitize


def address_to_key(addr: str) -> str:
    key = addr.replace(" ", "_")
    key = key.replace(".", "")
    key = key.replace("/", "")
    key = key.replace("\n", "")
    key = key.replace("\t", "")
    key = key.lower()[:64]
    return key


@app.route("/login", methods=["POST"])
def login():
    if "username" in session:
        return render_template("error.html", msg="You cannot log in without logging out first.")

    username = request.form.get("username")
    password = request.form.get("password")

    # TODO: Encrypted password
    u = db.session.query(User).filter_by(username=username, password=password).first()

    if u is not None:
        session["username"] = username
        session["is_admin"] = False if u.is_admin == 0 else True
        return redirect("/")

    else:
        return render_template("error.html", msg="Incorrect user name or password.")


@app.route("/logout", methods=["GET"])
def logout():
    if "username" in session:
        del session["username"]
    return redirect("/")


@app.route("/register", methods=["GET", "POST"])
def register():
    match request.method:
        case "GET":
            return render_template(
                "register.html",
                suggested_password=get_suggested_password(),
            )
        case "POST":
            username = request.form.get("username")
            password = request.form.get("password")
            email = request.form.get("email")
            realname = request.form.get("realname")

            # sanitize the username
            username = sanitize(username)

            # ensure the uniqueness of users
            existing_users = db.session.query(User).filter_by(username=username).count()
            if existing_users != 0:
                return render_template("error.html", msg=f"User \"{username}\" already exists.")

            u = User(username=username, password=password, email=email, realname=realname)
            db.session.add(u)
            db.session.commit()

            # auto login
            session["username"] = username
            session["is_admin"] = 0

            return redirect("/")
        case other:
            pass


@app.route("/", methods=["GET"])
def index():
    username = session.get("username", None)
    is_admin = session.get("is_admin", False)

    if username:
        kv_store("active_users", username)
    active_users = [kv_load("active_users", str)]

    return render_template(
        "index.html",
        username=username,
        is_admin=is_admin,
        active_users=active_users,
    )


@app.route("/inventory", methods=["GET"])
def inventory_index():
    inventory = list_inventory()
    inventory_with_picture = []
    if inventory:
        for house in inventory:
            d = dict(house)
            pic_path = kv_load(address_to_key(d["address"]), str)
            if pic_path is not None:
                o0, o1 = obfuscate_param(pic_path)
                pic_path = urllib.parse.quote(f"{o0}-{o1}", safe="")
                d["pic_path"] = "/get_picture?path=" + pic_path + "&key=" + address_to_key(d["address"])
            else:
                d["pic_path"] = ""
            inventory_with_picture.append(d)
    return render_template("inventory.html", inventory=inventory_with_picture)


@app.route("/add_house", methods=["GET", "POST"])
def add_house_view():
    if request.method == "GET":
        return render_template("add_house.html")
    elif request.method == "POST":
        kwargs = dict(request.form.items())
        if "submit" in kwargs:
            del kwargs["submit"]

        if "pic" in request.files:
            f = request.files["pic"]
            if f.filename:
                content = f.stream.read()
                pic_name = add_house_picture(address_to_key(kwargs.get("address")), content)
                if not pic_name:
                    return render_template("error.html", msg="Failed to upload picture.")
                # save the picture name
                r = kv_store(address_to_key(kwargs.get("address")), pic_name)
                if not r:
                    return render_template("error.html", msg="Failed to store picture name.")

        r = add_house(**kwargs)
        if not r:
            return render_template("error.html", msg="Backend failure.")
        return redirect("/inventory")


@app.route("/remove_house", methods=["GET"])
def remove_house_view():
    method = request.args.get("method", "delete")
    try:
        idx = request.args["id"]
    except KeyError:
        abort(400)
        return

    remove_house(method, idx)
    return redirect("/inventory")


@app.route("/get_picture", methods=["GET"])
def get_picture():
    try:
        key = request.args["key"]
    except KeyError:
        abort(400)
        return

    pic_path_ = kv_load(key, str)

    pic_path = request.args.get("path", "")
    if not pic_path:
        abort(404)
        return

    if pic_path.count("-") != 1:
        abort(400)
        return
    o0, o1 = pic_path.split("-")
    pic_path = deobfuscate_param(o0, o1)
    if pic_path is None:
        abort(400)
        return

    if pic_path_ != pic_path:
        print("Something is wrong?? Maybe this is an attack. Do not serve the file!")
        # abort(401)

    content = get_house_picture(pic_path)
    if not content:
        abort(404)

    return content


@app.route("/mini.css", methods=["GET"])
def mini_css():
    base_dir = os.path.dirname(os.path.realpath(__file__))
    with open(base_dir + "/css/mini.css", "r") as f:
        return Response(f.read(), mimetype="text/css")