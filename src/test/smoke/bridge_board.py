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

"""GlideKVM Bridge smoke test.

Runs a real server on a virtual X display with a pretend Bridge board on a
pseudo terminal, as if plugged in over USB, then checks:

  * the server finds the board and adds the tablet paired with it as a screen,
  * moving the mouse off the server's right edge sends input to the tablet,
    pushing the pointer into the corner first and then to where it entered,
  * the pointer moves, keys go by their HID usage and clicks arrive,
  * moving back lets go of everything on the tablet,
  * a device set to disconnect while not in use is asked back on entering,
    and its pointer is placed once it is back,
  * a device that leaves Bluetooth leaves the desk, and comes back with it,
  * unplugging the board takes its devices off the desk.

Needs Xvfb and python-xlib. Usage:

    python3 src/test/smoke/bridge_board.py --bin-dir build/bin
"""

import argparse
import os
import pty
import select
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import tty

from Xlib import X, XK, display
from Xlib.ext import xtest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from two_screens import (SmokeTestFailure, make_profile, read_file, start_xvfb,  # noqa: E402
                         wait_for, WIDTH, HEIGHT)

SERVER_DISPLAY = ":94"

CONFIG = """section: screens
    server:
    tablet:
    phone:
end
section: links
    server:
        right = tablet
        down = phone
    tablet:
        left = server
    phone:
        up = server
end
"""


class Board:
    """Answers like the Bridge firmware does and records what it is told."""

    def __init__(self):
        self.master, slave = pty.openpty()
        tty.setraw(slave)
        self.path = os.ttyname(slave)
        self.slave = slave
        self.lines = []
        self.lock = threading.Lock()
        self.unplugged = False
        self.thread = threading.Thread(target=self._read, daemon=True)
        self.thread.start()

    def say(self, line):
        os.write(self.master, (line + "\n").encode())

    def _read(self):
        buffer = b""
        while not self.unplugged:
            try:
                if not select.select([self.master], [], [], 0.1)[0]:
                    continue
                data = os.read(self.master, 1024)
            except OSError:
                return
            if not data:
                return
            buffer += data
            while b"\n" in buffer:
                line, buffer = buffer.split(b"\n", 1)
                line = line.decode().strip()
                if not line:
                    continue
                with self.lock:
                    self.lines.append(line)
                if line == "hello":
                    # a log line first, which the server should skip
                    self.say("I (312) bridge: started")
                    self.say("@hello glidekvm-bridge 1 0.1.0 8")
                elif line == "list":
                    self.say("@slot 2 AA:BB:CC:DD:EE:02 connected Oren's iPad")
                    self.say("@slot 3 AA:BB:CC:DD:EE:03 off iPhone")
                    self.say("@end")

    def told(self):
        with self.lock:
            return list(self.lines)

    def since(self, mark):
        return self.told()[mark:]

    def unplug(self):
        # the reading thread must let go of the port before it really closes
        self.unplugged = True
        self.thread.join()
        os.close(self.master)
        os.close(self.slave)


def run(bin_dir, work, port):
    procs = []
    server_log = os.path.join(work, "server.log")

    def step(message):
        print("--", message, flush=True)

    board = Board()
    try:
        procs.append(start_xvfb(SERVER_DISPLAY, work))
        profile = os.path.join(work, "server-profile")
        make_profile(profile)
        config = os.path.join(work, "server.conf")
        with open(config, "w") as f:
            f.write(CONFIG)

        step("starting the server with the board on " + board.path)
        server = subprocess.Popen([os.path.join(bin_dir, "glidekvm-server"), "-f", "--no-tray",
                                   "--display", SERVER_DISPLAY, "--name", "server",
                                   "--config", config, "--address", "127.0.0.1:%d" % port,
                                   "--profile-dir", profile,
                                   "--bridge", board.path,
                                   "--bridge-device", "2,tablet,1280x800",
                                   "--bridge-device", "3,phone,400x800,away",
                                   "--debug", "DEBUG", "--log", server_log],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        procs.append(server)
        wait_for("the server to find the board", lambda: "found the GlideKVM Bridge" in read_file(server_log))
        wait_for("the tablet to join", lambda: 'client "tablet" has connected' in read_file(server_log))
        wait_for("the phone to join", lambda: 'client "phone" has connected' in read_file(server_log))
        told = board.told()
        for expected in ["allow 2 1", "allow 3 0", "list"]:
            if expected not in told:
                raise SmokeTestFailure("the board wasn't told %r: %s" % (expected, told))
        step("PASS: found the board; the tablet and the phone (kept away) are on the desk")

        mouse = display.Display(SERVER_DISPLAY)

        def move_to(x, y):
            xtest.fake_input(mouse, X.MotionNotify, x=x, y=y)
            mouse.sync()
            time.sleep(0.05)

        def press(keysym):
            keycode = mouse.keysym_to_keycode(XK.string_to_keysym(keysym))
            xtest.fake_input(mouse, X.KeyPress, keycode)
            xtest.fake_input(mouse, X.KeyRelease, keycode)
            mouse.sync()
            time.sleep(0.05)

        step("moving the mouse off the server's right edge")
        mark = len(board.told())
        move_to(WIDTH // 2, HEIGHT // 2)
        move_to(WIDTH - 1, HEIGHT // 2)
        wait_for("the tablet to be targeted", lambda: "target 2" in board.since(mark))
        entered = board.since(mark)
        at = entered.index("target 2")
        if entered[at + 1] != "m -30000 -30000" or not entered[at + 2].startswith("m 0 "):
            raise SmokeTestFailure("expected the pointer pushed home then placed: %s" % entered)
        step("PASS: the pointer went to the corner, then to %s" % entered[at + 2])

        mark = len(board.told())
        for _ in range(3):
            move_to(WIDTH // 2 + 40, HEIGHT // 2 + 20)
            time.sleep(0.1)

        def moved_right():
            return any(line.startswith("m ") and int(line.split()[1]) > 0 for line in board.since(mark))

        wait_for("the pointer to move", moved_right)
        step("PASS: the pointer moved")

        mark = len(board.told())
        press("h")
        press("i")
        xtest.fake_input(mouse, X.ButtonPress, 1)
        xtest.fake_input(mouse, X.ButtonRelease, 1)
        mouse.sync()
        wait_for("the click", lambda: "b 0" in board.since(mark))
        sent = [line for line in board.since(mark) if line[0] in "kb"]
        if sent != ["kd 0b", "ku 0b", "kd 0c", "ku 0c", "b 1", "b 0"]:
            raise SmokeTestFailure("expected h, i and a click, the board got %s" % sent)
        step("PASS: keys and a click went as HID usages")

        step("moving back to the server")
        mark = len(board.told())

        def moved_back():
            move_to(WIDTH // 2 - 300, HEIGHT // 2)
            return "target -1" in board.since(mark)

        wait_for("the tablet to be left", moved_back)
        if "release" not in board.since(mark):
            raise SmokeTestFailure("nothing was let go of: %s" % board.since(mark))
        step("PASS: everything on the tablet was let go of")

        step("moving down onto the phone, which is kept away while not in use")
        move_to(WIDTH // 2, HEIGHT // 2)
        mark = len(board.told())

        def moved_down():
            move_to(WIDTH // 2, HEIGHT - 1)
            return "target 3" in board.since(mark)

        wait_for("the phone to be targeted", moved_down)
        wait_for("the phone to be asked back", lambda: "allow 3 1" in board.since(mark))
        if any(line.startswith("m -30000") for line in board.since(mark)):
            raise SmokeTestFailure("the pointer was placed before the phone was back")
        board.say("@connected 3")
        wait_for("the pointer to be placed", lambda: "m -30000 -30000" in board.since(mark))
        step("PASS: the phone was asked back and its pointer placed once it was")

        mark = len(board.told())

        def moved_up():
            move_to(WIDTH // 2, 10)
            return "allow 3 0" in board.since(mark)

        wait_for("the phone to be sent away again", moved_up)
        board.say("@disconnected 3")
        time.sleep(0.5)
        if 'client "phone" has disconnected' in read_file(server_log):
            raise SmokeTestFailure("the phone left the desk while it was only away")
        step("PASS: the phone was sent away again and stays on the desk")

        step("the tablet leaving Bluetooth")
        board.say("@disconnected 2")
        wait_for("the tablet to leave", lambda: 'client "tablet" has disconnected' in read_file(server_log))
        board.say("@connected 2")
        wait_for("the tablet to come back",
                 lambda: read_file(server_log).count('client "tablet" has connected') == 2)
        step("PASS: the tablet left the desk and came back with Bluetooth")

        step("stopping a second server while the board is plugged in")
        board2 = Board()
        log2 = os.path.join(work, "server2.log")
        second = subprocess.Popen([os.path.join(bin_dir, "glidekvm-server"), "-f", "--no-tray",
                                   "--display", SERVER_DISPLAY, "--name", "server",
                                   "--config", config, "--address", "127.0.0.1:%d" % (port + 1),
                                   "--profile-dir", profile, "--bridge", board2.path,
                                   "--bridge-device", "2,tablet,1280x800",
                                   "--debug", "DEBUG", "--log", log2],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        wait_for("the second server's tablet", lambda: 'client "tablet" has connected' in read_file(log2))
        second.terminate()
        code = second.wait(timeout=10)
        board2.unplug()
        if code < 0 or code > 1:
            raise SmokeTestFailure("the server ended badly (%d) with the board plugged in" % code)
        step("PASS: the server stopped cleanly (%d)" % code)

        step("unplugging the board")
        board.unplug()
        wait_for("the server to notice", lambda: "lost the GlideKVM Bridge" in read_file(server_log))
        wait_for("the devices to leave",
                 lambda: read_file(server_log).count('client "tablet" has disconnected') == 2
                 and 'client "phone" has disconnected' in read_file(server_log))
        if server.poll() is not None:
            raise SmokeTestFailure("the server stopped")
        step("PASS: the devices left the desk and the server kept running")
        mouse.close()
        return True
    finally:
        for proc in reversed(procs):
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
        print("\n==== board was told ====\n" + "\n".join(board.told()[-60:]))
        print("\n==== server log ====\n" + "\n".join(read_file(server_log).splitlines()[-40:]))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--bin-dir", required=True, help="directory with glidekvm-server")
    parser.add_argument("--port", type=int, default=24894)
    parser.add_argument("--keep", action="store_true", help="keep the work directory")
    args = parser.parse_args()

    work = tempfile.mkdtemp(prefix="glidekvm-bridge-")
    ok = False
    try:
        ok = run(os.path.abspath(args.bin_dir), work, args.port)
    except SmokeTestFailure as e:
        print("FAIL:", e, flush=True)
    finally:
        if args.keep or not ok:
            print("work directory:", work)
        else:
            shutil.rmtree(work, ignore_errors=True)
    print("bridge smoke test", "passed" if ok else "failed")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
