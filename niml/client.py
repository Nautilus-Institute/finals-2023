#!/usr/bin/env python3

import json
import sys

from textual.app import App, ComposeResult
from textual.reactive import reactive
from textual.widgets import *
from textual.widget import Widget
from textual.containers import Container, Horizontal, Vertical
from urllib.parse import urlparse, parse_qs

from pwn import *

context.log_level = 'debug'

PORT = 59997
#PORT = 10001

p = None

logf = open("/tmp/log.txt", "w")
def lprint(*args, **kwargs):
    print(*args, **kwargs, file=logf)
    logf.flush()

def get_response(ignore_status=True):
    while 1:
        l = p.readline()
        lprint(l)
        l = l.decode("utf-8")
        if l == "\n" or l=="\r\n":
            continue
        if l == "":
            break
        try:
            j = json.loads(l)
            lprint(j)
            if len(j) == 0:
                continue
            if ignore_status and 'status' in j:
                continue
            lprint("Got message:", json.dumps(j, indent=2))
            break
        except json.JSONDecodeError as e:
            lprint("Got invalid message:\n`", l, "`")
            j = {'body':[l],'css':''}
            break

    return j

def send_request(**kwargs):
    s = json.dumps(kwargs)
    s_i = json.dumps(kwargs, indent=2)

    lprint("Sending message:", s_i)
    p.sendline(s.encode('utf-8'))
    p.sendline(b'{}')

#r = get_response()
#print(r)

class Tag(Vertical):
    DEFAULT_CSS = """
    Tag {
        height: auto;
    }
    """
class Row(Horizontal):
    DEFAULT_CSS = """
    Tag {
        height: auto;
    }
    """

class Head(Static):
    DEFAULT_CSS = """
    Head {
        color: $text;
        background: $accent-darken-2;
        border: wide $background;
        content-align: center middle;
        padding: 1;
        text-style: bold;
        color: $text;
    }
    """
class Page(Widget):
    DEFAULT_CSS = """
    """
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.forms = {}

    def _compose_page(self, page):
        for el in page['body']:
            lprint(el)
            for r in self.render_element(el):
                lprint(r)
                yield r

    def render_page(self, page):
        self.forms.clear()
        self.remove_children()
        self.mount_all(self._compose_page(page))

    def render_element(self, el):
        if isinstance(el, list) or isinstance(el, tuple):
            for e in el:
                yield from self.render_element(e)
            return

        lprint("rendering element", el)
        if el is None:
            return

        if isinstance(el, str):
            yield Static(el)
            return
        
        el_ty = el.get('element')
        lprint("element type", el_ty)
        if el_ty == 'button':
            form = el.get('form')
            name = el.get('action', '_')
            if form:
                self.forms[name] = self.forms.get(form, {})
            yield Button(el['body'], name=name)
        elif el_ty == 'tag':
            lprint("rendering tag......", el)
            #self.render_element(el.get('body'))
            yield Tag(*list(
                self.render_element(el.get('body'))
            ), id=el.get('id', None))
        elif el_ty == 'row':
            lprint("rendering row......", el)
            #self.render_element(el.get('body'))
            yield Row(*list(
                self.render_element(el.get('body'))
            ), id=el.get('id', None))
        elif el_ty == 'head':
            yield Head(
                el.get('body',''),
                id=el.get('id', None)
            )
        elif el_ty == 'input':
            form = el.get('form')
            v = Input(
                name=el['name'],
                value=el.get('value', ''),
                placeholder=el.get('placeholder', ''),
                password = el.get('password', False),
            )
            if form:
                self.forms[form] = self.forms.get(form, {})
                self.forms[form][el['name']] = v
            yield v

class Browser(App):
    """A Textual app to manage stopwatches."""

    DEFAULT_CSS = """
    #__INTERNAL_URLBAR {
        margin: 0;
        padding: 1;
        border: none;
        background: $background;
    }
    """

    def __init__(self, uri, **kwargs):
        super().__init__(**kwargs)
        self.host = None
        self.launch_uri = uri
        self.current_uri = None
        self.base_stylesheet = self.stylesheet

    def open_connection(self, domain):
        global p
        lprint("Connecting to", domain)
        #domain = uri.netloc
        if self.host is not None:
            d,c = self.host
            if d == domain:
                return c
            c.close()
        c = remote(domain,PORT)
        self.host = (domain, c)
        p = c
        loading_status = get_response(False)
        lprint(loading_status)
        ok_status = get_response(False)
        lprint(ok_status)

    def navigate(self, uri, args=None):
        if args is None:
            args = {}
        uri = urlparse(uri)
        if uri.scheme == '':
            uri = uri._replace(scheme=self.current_uri.scheme)
        if uri.netloc == '':
            uri = uri._replace(netloc=self.current_uri.netloc)
        self.open_connection(uri.netloc)
        lprint("Navigating to", uri.geturl())

        self.current_uri = uri

        self.query_one('#__INTERNAL_URLBAR', Input).value = uri.geturl()

        pargs = {
            k:(v[0] if len(v) == 1 else v)
            for k,v in parse_qs(uri.query).items()
        }
        args.update(pargs)
        
        send_request(
            method="VIEW",
            path=uri.path,
            args=args
        )
        r = get_response()

        nav = r.get('navigate')
        if nav:
            self.navigate(nav['uri'], args=nav.get('args'))
            return

        self.render_page(r)

    def render_page(self, r):
        self.stylesheet = self.stylesheet.copy()
        if 'css' in r:
            self.stylesheet.add_source(r['css'])
        self.stylesheet.reparse()
        self.stylesheet.update(self)

        self.query_one(Page).render_page(r)
    

    BINDINGS = [("d", "toggle_dark", "Toggle dark mode")]
    def on_button_pressed(self, event: Button.Pressed) -> None:
        lprint("button pressed", event)
        page = self.query_one(Page)

        args = []

        name = event.button.name

        form = page.forms.get(name, None)
        if form:
            form = {k:v.value for k,v in form.items()}
            args = [form]


        send_request(
            method="ACT",
            action=name,
            args=args,
        )
        r = get_response()
        nav = r.get('navigate')
        if nav:
            self.navigate(nav['uri'], nav.get('args'))
            return
        page.render_page(r)
        #self.current_page = r
        #lprint(self.current_page)
        #self.action_toggle_dark()

    def on_input_submitted(self, event: Input.Submitted) -> None:
        lprint("input submitted", event)
        if event.input.name == "__INTERNAL_URLBAR":
            uri = event.value
            self.navigate(uri)
            return
    
    def on_input_changed(self, event: Input.Changed) -> None:
        lprint("input changed", event)
        if event.input.name == "__INTERNAL_URLBAR":
            return
        '''
        send_request(
            method="ACT",
            action=event.text,
            value=event.text,
        )
        r = get_response()
        self.query_one(Page).contents = r
        ''' 
        #self.current_page = r
        #lprint(self.current_page)

    def on_mount(self):
        app.navigate(self.launch_uri)
        pass



    def compose(self) -> ComposeResult:
        yield Header(True)
        yield Input(id="__INTERNAL_URLBAR", name="__INTERNAL_URLBAR", placeholder="about:blank")
        yield Page()
        '''
        dict(
            path="/index.php",
            title="index",
            body=[
                "hello world",
                {"element": "button", "action": "FOOBAR", "body":"foobar"}
            ]
        )
        '''


    def action_toggle_dark(self) -> None:
        """An action to toggle dark mode."""
        self.dark = not self.dark

if __name__ == "__main__":
    launch = 'http://127.0.0.1/index.php'
    if len(sys.argv) > 1:
        launch = sys.argv[1]
    app = Browser(launch)
    #app.navigate('/index.php')
    #p.interactive()
    app.run()

#https://textual.textualize.io/tutorial/#css-basics
