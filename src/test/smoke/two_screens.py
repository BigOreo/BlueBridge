#!/usr/bin/env python3
#
# InputLeap -- mouse and keyboard sharing utility
# Copyright (C) InputLeap contributors
#
# This package is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# found in the file LICENSE that should have accompanied this file.
#
# This package is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

"""Two-screen smoke test.

Runs a real server and client, each on its own virtual X display, connected
over TLS on localhost, and checks what a person would check by hand:

  * the client connects to the server,
  * moving the server's mouse off the right edge moves the client's pointer,
  * keys typed on the server arrive on the client,
  * text copied on the server can be pasted on the client,
  * moving back across the edge returns control to the server,
  * stopping the server disconnects the client.

Needs Xvfb, xclip, openssl and python-xlib. Usage:

    python3 src/test/smoke/two_screens.py --bin-dir build/bin
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time

from Xlib import X, XK, display
from Xlib.ext import xtest

WIDTH, HEIGHT = 1024, 768
SERVER_DISPLAY = ":91"
CLIENT_DISPLAY = ":92"
CLIPBOARD_TEXT = "copied on the server"

CONFIG = """section: screens
    server:
    client:
end
section: links
    server:
        right = client
    client:
        left = server
end
"""


class SmokeTestFailure(Exception):
    pass


def wait_for(description, condition, timeout=15.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if condition():
            return
        time.sleep(0.1)
    raise SmokeTestFailure("timed out waiting for " + description)


def read_file(path):
    try:
        with open(path, errors="replace") as f:
            return f.read()
    except FileNotFoundError:
        return ""


def make_profile(path):
    """Creates a profile directory with a TLS certificate and returns the
    certificate's fingerprint in the trusted fingerprint file format."""
    ssl_dir = os.path.join(path, "SSL")
    os.makedirs(os.path.join(ssl_dir, "Fingerprints"))
    pem = os.path.join(ssl_dir, "InputLeap.pem")
    subprocess.run(["openssl", "req", "-x509", "-nodes", "-newkey", "rsa:2048",
                    "-days", "1", "-subj", "/CN=InputLeap",
                    "-keyout", pem, "-out", pem + ".crt"],
                   check=True, capture_output=True)
    with open(pem, "a") as out, open(pem + ".crt") as crt:
        out.write(crt.read())
    os.remove(pem + ".crt")
    result = subprocess.run(["openssl", "x509", "-in", pem, "-noout", "-fingerprint", "-sha256"],
                            check=True, capture_output=True, text=True)
    digest = result.stdout.strip().split("=", 1)[1].replace(":", "").lower()
    return "v2:sha256:" + digest


def trust(profile, filename, fingerprint):
    with open(os.path.join(profile, "SSL", "Fingerprints", filename), "w") as f:
        f.write(fingerprint + "\n")


class KeyProbe:
    """A full-screen window on the client display that records the keys it
    receives, standing in for the app a person would type into."""

    def __init__(self, name):
        self.display = display.Display(name)
        screen = self.display.screen()
        self.root = screen.root
        self.window = self.root.create_window(
            0, 0, WIDTH, HEIGHT, 0, screen.root_depth,
            event_mask=X.KeyPressMask)
        self.window.map()
        self.display.sync()
        self.keys = ""

    def focus(self):
        self.window.set_input_focus(X.RevertToParent, X.CurrentTime)
        self.display.sync()

    def pointer(self):
        p = self.root.query_pointer()
        return p.root_x, p.root_y

    def poll(self):
        while self.display.pending_events():
            event = self.display.next_event()
            if event.type == X.KeyPress:
                keysym = self.display.keycode_to_keysym(event.detail, 0)
                self.keys += XK.keysym_to_string(keysym) or ""
        return self.keys


class Input:
    """Fake mouse and keyboard on the server display."""

    def __init__(self, name):
        self.display = display.Display(name)

    def move_to(self, x, y):
        xtest.fake_input(self.display, X.MotionNotify, x=x, y=y)
        self.display.sync()
        time.sleep(0.05)

    def type(self, text):
        for ch in text:
            keycode = self.display.keysym_to_keycode(XK.string_to_keysym(ch))
            xtest.fake_input(self.display, X.KeyPress, keycode)
            xtest.fake_input(self.display, X.KeyRelease, keycode)
            self.display.sync()
            time.sleep(0.05)


def start_xvfb(name, work):
    log = open(os.path.join(work, "xvfb" + name.replace(":", "-") + ".log"), "w")
    proc = subprocess.Popen(["Xvfb", name, "-screen", "0", "%dx%dx24" % (WIDTH, HEIGHT),
                             "-nolisten", "tcp"], stdout=log, stderr=log)

    def ready():
        try:
            display.Display(name).close()
            return True
        except Exception:
            return False

    wait_for("display " + name, ready)
    return proc


def run(bin_dir, work, port):
    procs = []
    displays = []
    server_log = os.path.join(work, "server.log")
    client_log = os.path.join(work, "client.log")

    def step(message):
        print("--", message, flush=True)

    try:
        procs.append(start_xvfb(SERVER_DISPLAY, work))
        procs.append(start_xvfb(CLIENT_DISPLAY, work))

        server_profile = os.path.join(work, "server-profile")
        client_profile = os.path.join(work, "client-profile")
        server_fingerprint = make_profile(server_profile)
        client_fingerprint = make_profile(client_profile)
        trust(server_profile, "TrustedClients.txt", client_fingerprint)
        trust(client_profile, "TrustedServers.txt", server_fingerprint)

        config = os.path.join(work, "server.conf")
        with open(config, "w") as f:
            f.write(CONFIG)

        probe = KeyProbe(CLIENT_DISPLAY)
        displays.append(probe.display)
        probe.focus()

        # The server owns the clipboard text before the pointer crosses over,
        # the way a person copies something and then moves to the other screen.
        env = dict(os.environ, DISPLAY=SERVER_DISPLAY)
        procs.append(subprocess.Popen(["xclip", "-selection", "clipboard", "-loops", "0"],
                                      stdin=subprocess.PIPE, stderr=subprocess.DEVNULL, env=env))
        procs[-1].stdin.write(CLIPBOARD_TEXT.encode())
        procs[-1].stdin.close()

        step("starting the server and the client")
        address = "127.0.0.1:%d" % port
        server = subprocess.Popen([os.path.join(bin_dir, "input-leaps"), "-f", "--no-tray",
                                   "--display", SERVER_DISPLAY, "--name", "server",
                                   "--config", config, "--address", address,
                                   "--profile-dir", server_profile,
                                   "--debug", "DEBUG", "--log", server_log],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        procs.append(server)
        wait_for("the server to listen",
                 lambda: "started server" in read_file(server_log))
        client = subprocess.Popen([os.path.join(bin_dir, "input-leapc"), "-f", "--no-tray",
                                   "--display", CLIENT_DISPLAY, "--name", "client",
                                   "--profile-dir", client_profile,
                                   "--debug", "DEBUG", "--log", client_log, address],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        procs.append(client)
        wait_for("the client to connect",
                 lambda: 'client "client" has connected' in read_file(server_log))
        if "accepted secure socket" not in read_file(server_log):
            raise SmokeTestFailure("the connection is not encrypted")
        step("PASS: the client connected over TLS")

        mouse = Input(SERVER_DISPLAY)
        displays.append(mouse.display)
        mouse.move_to(WIDTH // 2, HEIGHT // 2)
        start = probe.pointer()

        step("moving the mouse off the server's right edge")
        mouse.move_to(WIDTH - 1, HEIGHT // 2)
        wait_for("the switch to the client",
                 lambda: 'switch from "server" to "client"' in read_file(server_log))
        wait_for("the client's pointer to move", lambda: probe.pointer() != start)
        entered = probe.pointer()
        if entered[0] > 10:
            raise SmokeTestFailure("the pointer entered the client at %s, "
                                   "expected its left edge" % (entered,))
        # On X11 the server keeps its own pointer in the middle of its screen
        # and sends the difference, so moves are relative to the center.
        for _ in range(5):
            mouse.move_to(WIDTH // 2 + 40, HEIGHT // 2 + 20)
            time.sleep(0.1)
        wait_for("the client's pointer to follow the mouse",
                 lambda: probe.pointer()[0] > entered[0] + 100)
        step("PASS: the pointer moved onto the client, now at %s" % (probe.pointer(),))

        step("typing on the server")
        mouse.type("hello")
        wait_for("the keys to arrive on the client", lambda: probe.poll().endswith("hello"))
        step("PASS: the client received the keys")

        step("pasting on the client")
        env = dict(os.environ, DISPLAY=CLIENT_DISPLAY)

        def pasted():
            result = subprocess.run(["xclip", "-selection", "clipboard", "-o"], env=env,
                                    capture_output=True, text=True, timeout=5)
            return result.stdout == CLIPBOARD_TEXT

        wait_for("the clipboard to reach the client", pasted)
        step("PASS: the client's clipboard has the text copied on the server")

        step("moving the mouse back across the left edge")

        def moved_back():
            mouse.move_to(WIDTH // 2 - 200, HEIGHT // 2)
            return 'switch from "client" to "server"' in read_file(server_log)

        wait_for("the switch back to the server", moved_back)
        before = probe.poll()
        mouse.type("x")
        time.sleep(0.5)
        if probe.poll() != before:
            raise SmokeTestFailure("keys still reached the client after moving back")
        step("PASS: control returned to the server")

        step("stopping the server")
        server.terminate()
        server.wait(timeout=10)
        wait_for("the client to notice",
                 lambda: "disconnected from server" in read_file(client_log))
        step("PASS: the client disconnected when the server stopped")
        return True
    finally:
        for d in displays:
            d.close()
        for proc in reversed(procs):
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--bin-dir", required=True,
                        help="directory with input-leaps and input-leapc")
    parser.add_argument("--port", type=int, default=24890)
    parser.add_argument("--keep", action="store_true",
                        help="keep the work directory with the logs")
    args = parser.parse_args()

    work = tempfile.mkdtemp(prefix="smoke-")
    ok = False
    try:
        ok = run(os.path.abspath(args.bin_dir), work, args.port)
    except SmokeTestFailure as e:
        print("FAIL:", e, flush=True)
    finally:
        if not ok:
            for name in ("server.log", "client.log"):
                print("\n==== %s ====" % name)
                print(read_file(os.path.join(work, name))[-6000:])
        if args.keep or not ok:
            print("logs kept in", work)
        else:
            shutil.rmtree(work, ignore_errors=True)
    print("smoke test", "passed" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
