# GlideKVM -- mouse and keyboard sharing utility
# Copyright (C) GlideKVM contributors
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

"""Android client smoke test.

Runs a real server on a virtual X display and connects the Android app's
connection core to it (as the command line probe, so no device is needed),
then checks:

  * an unknown main computer is refused until its fingerprint is trusted,
  * the main computer refuses the device until it trusts the device's
    fingerprint, logs that fingerprint for the app to ask about, and the
    device says it hasn't been accepted yet,
  * a device whose name isn't in the layout is told to add it,
  * the device connects over TLS with its own certificate,
  * moving the mouse off the server's right edge enters the device,
  * the pointer moves and keys typed on the server arrive,
  * text copied on the server arrives as the device's clipboard,
  * moving back returns control to the server,
  * stopping the server ends the connection.

Needs Xvfb, xclip, openssl, java and python-xlib. Usage:

    (cd android && ./gradlew :core:probeJar)
    python3 src/test/smoke/android_client.py --bin-dir build/bin \\
        --probe android/core/build/libs/glidekvm-probe.jar
"""

import argparse
import os
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import time

from Xlib import X, XK, display
from Xlib.ext import xtest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from two_screens import (SmokeTestFailure, make_profile, read_file, start_xvfb,  # noqa: E402
                         trust, wait_for)

WIDTH, HEIGHT = 1024, 768
DEVICE_WIDTH, DEVICE_HEIGHT = 1280, 800
SERVER_DISPLAY = ":93"
CLIPBOARD_TEXT = "copied for the tablet"

CONFIG = """section: screens
    server:
    tablet:
end
section: links
    server:
        right = tablet
    tablet:
        left = server
end
"""


class Probe:
    """The connection core run as a command line client; collects the lines it
    prints, one per message from the server."""

    def __init__(self, jar, args):
        self.proc = subprocess.Popen(["java", "-jar", jar] + args, stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, text=True)
        self.lines = []
        self.queue = queue.Queue()
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for line in self.proc.stdout:
            self.queue.put(line.rstrip("\n"))
        self.queue.put(None)

    def poll(self):
        while True:
            try:
                line = self.queue.get_nowait()
            except queue.Empty:
                return self.lines
            if line is not None:
                self.lines.append(line)

    def has(self, prefix):
        return any(line.startswith(prefix) for line in self.poll())

    def last(self, prefix):
        found = [line for line in self.poll() if line.startswith(prefix)]
        return found[-1] if found else None

    def stop(self):
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()


def device_certificate(work):
    """A certificate for the device in a PKCS12 file, as the app keeps one in
    the Android key store, and its fingerprint in the trusted file format."""
    key = os.path.join(work, "device.key")
    crt = os.path.join(work, "device.crt")
    p12 = os.path.join(work, "device.p12")
    subprocess.run(["openssl", "req", "-x509", "-nodes", "-newkey", "rsa:2048", "-days", "1",
                    "-subj", "/CN=GlideKVM", "-keyout", key, "-out", crt],
                   check=True, capture_output=True)
    subprocess.run(["openssl", "pkcs12", "-export", "-inkey", key, "-in", crt, "-out", p12,
                    "-passout", "pass:glidekvm"], check=True, capture_output=True)
    result = subprocess.run(["openssl", "x509", "-in", crt, "-noout", "-fingerprint", "-sha256"],
                            check=True, capture_output=True, text=True)
    digest = result.stdout.strip().split("=", 1)[1].replace(":", "").lower()
    return p12, "v2:sha256:" + digest


def run(bin_dir, jar, work, port):
    procs = []
    probes = []
    server_log = os.path.join(work, "server.log")

    def step(message):
        print("--", message, flush=True)

    try:
        procs.append(start_xvfb(SERVER_DISPLAY, work))
        server_profile = os.path.join(work, "server-profile")
        server_fingerprint = make_profile(server_profile)
        p12, device_fingerprint = device_certificate(work)
        trusted_servers = os.path.join(work, "TrustedServers.txt")

        config = os.path.join(work, "server.conf")
        with open(config, "w") as f:
            f.write(CONFIG)

        env = dict(os.environ, DISPLAY=SERVER_DISPLAY)
        procs.append(subprocess.Popen(["xclip", "-selection", "clipboard", "-loops", "0"],
                                      stdin=subprocess.PIPE, stderr=subprocess.DEVNULL, env=env))
        procs[-1].stdin.write(CLIPBOARD_TEXT.encode())
        procs[-1].stdin.close()

        step("starting the server")
        address = "127.0.0.1:%d" % port
        server = subprocess.Popen([os.path.join(bin_dir, "glidekvm-server"), "-f", "--no-tray",
                                   "--display", SERVER_DISPLAY, "--name", "server",
                                   "--config", config, "--address", address,
                                   "--profile-dir", server_profile,
                                   "--debug", "DEBUG", "--log", server_log],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        procs.append(server)
        wait_for("the server to listen", lambda: "started server" in read_file(server_log))

        probe_args = ["--name", "tablet", "--pkcs12", p12, "--password", "glidekvm",
                      "--trusted", trusted_servers,
                      "--size", "%dx%d" % (DEVICE_WIDTH, DEVICE_HEIGHT), address]

        step("connecting before the main computer is trusted")
        first = Probe(jar, probe_args)
        probes.append(first)
        wait_for("the device to refuse the unknown server", lambda: first.has("untrusted"))
        if first.last("untrusted") != "untrusted " + server_fingerprint:
            raise SmokeTestFailure("the device showed fingerprint %r, the server's is %r"
                                   % (first.last("untrusted"), server_fingerprint))
        step("PASS: the device showed the server's fingerprint and did not connect")

        with open(trusted_servers, "w") as f:
            f.write(server_fingerprint + "\n")

        step("connecting before the main computer trusts the device")
        refused = Probe(jar, probe_args)
        probes.append(refused)
        wait_for("the device to be refused", lambda: refused.has("error"), timeout=30)
        if "hasn't accepted this device" not in refused.last("error"):
            raise SmokeTestFailure("the device said %r" % refused.last("error"))
        # the desktop app asks about the fingerprint in this line
        logged = device_fingerprint.split(":")[2]
        lines = [line for line in read_file(server_log).splitlines() if "peer fingerprint" in line]
        if not lines or logged not in lines[-1].replace(":", "").lower().split("(sha256)")[-1]:
            raise SmokeTestFailure("the server did not log the device's fingerprint: %s" % lines)
        step("PASS: the server logged the device's fingerprint and the device asked to be accepted")

        trust(server_profile, "TrustedClients.txt", device_fingerprint)
        probe = Probe(jar, probe_args)
        probes.append(probe)
        wait_for("the device to connect", lambda: probe.has("connected"))
        wait_for("the server to accept the device",
                 lambda: 'client "tablet" has connected' in read_file(server_log))
        if "accepted secure socket" not in read_file(server_log):
            raise SmokeTestFailure("the connection is not encrypted")
        step("PASS: the device connected over TLS with its own certificate")

        step("connecting with a name that isn't in the layout")
        stranger = Probe(jar, ["--name", "Pixel-3a"] + probe_args[2:])
        probes.append(stranger)
        wait_for("the stranger to be turned away", lambda: stranger.has("error"), timeout=30)
        if "Add a computer" not in stranger.last("error"):
            raise SmokeTestFailure("the device said %r" % stranger.last("error"))
        step("PASS: a device that isn't in the layout is told how to add it")

        mouse = display.Display(SERVER_DISPLAY)

        def move_to(x, y):
            xtest.fake_input(mouse, X.MotionNotify, x=x, y=y)
            mouse.sync()
            time.sleep(0.05)

        def type_text(text):
            for ch in text:
                keycode = mouse.keysym_to_keycode(XK.string_to_keysym(ch))
                xtest.fake_input(mouse, X.KeyPress, keycode)
                xtest.fake_input(mouse, X.KeyRelease, keycode)
                mouse.sync()
                time.sleep(0.05)

        step("moving the mouse off the server's right edge")
        move_to(WIDTH // 2, HEIGHT // 2)
        move_to(WIDTH - 1, HEIGHT // 2)
        wait_for("the device to be entered", lambda: probe.has("enter"))
        x, y = (int(v) for v in probe.last("enter").split()[1:3])
        if x > 10 or not 0 <= y < DEVICE_HEIGHT:
            raise SmokeTestFailure("entered the device at %d,%d, expected its left edge" % (x, y))
        for _ in range(5):
            move_to(WIDTH // 2 + 40, HEIGHT // 2 + 20)
            time.sleep(0.1)

        def moved_right():
            move = probe.last("move")
            return move is not None and int(move.split()[1]) > x + 100

        wait_for("the pointer to move on the device", moved_right)
        step("PASS: entered at %d,%d and the pointer moved to %s" % (x, y, probe.last("move")))

        step("typing on the server")
        type_text("hi")
        wait_for("the keys to arrive", lambda: probe.has("keyup 0x0069"))
        downs = [line.split()[1] for line in probe.poll() if line.startswith("keydown")]
        if downs[-2:] != ["0x0068", "0x0069"]:
            raise SmokeTestFailure("expected the keys h and i, got %s" % downs)
        step("PASS: the device received the keys")

        wait_for("the clipboard to arrive", lambda: probe.has("clipboard"))
        if probe.last("clipboard") != "clipboard " + CLIPBOARD_TEXT:
            raise SmokeTestFailure("the device got clipboard %r" % probe.last("clipboard"))
        step("PASS: the device received the copied text")

        step("moving the mouse back across the left edge")

        def moved_back():
            move_to(WIDTH // 2 - 200, HEIGHT // 2)
            return probe.has("leave")

        wait_for("the device to be left", moved_back)
        step("PASS: control returned to the server")

        step("stopping the server")
        server.terminate()
        server.wait(timeout=10)
        wait_for("the device to notice",
                 lambda: probe.has("disconnected") or probe.has("error"), timeout=20)
        step("PASS: the connection ended when the server stopped (%s)"
             % (probe.last("disconnected") or probe.last("error")))
        mouse.close()
        return True
    finally:
        for p in probes:
            p.stop()
            print("\n==== probe ====\n" + "\n".join(p.poll()[-40:]))
        for proc in reversed(procs):
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--bin-dir", required=True, help="directory with glidekvm-server")
    parser.add_argument("--probe", required=True, help="the glidekvm-probe.jar built by gradle")
    parser.add_argument("--port", type=int, default=24893)
    parser.add_argument("--keep", action="store_true", help="keep the work directory")
    args = parser.parse_args()

    work = tempfile.mkdtemp(prefix="android-smoke-")
    ok = False
    try:
        ok = run(os.path.abspath(args.bin_dir), os.path.abspath(args.probe), work, args.port)
    except SmokeTestFailure as e:
        print("FAIL:", e, flush=True)
    finally:
        if not ok:
            print("\n==== server.log ====")
            print(read_file(os.path.join(work, "server.log"))[-6000:])
        if args.keep or not ok:
            print("logs kept in", work)
        else:
            shutil.rmtree(work, ignore_errors=True)
    print("android client smoke test", "passed" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
