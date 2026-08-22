#!/usr/bin/env python3
"""ppctl -- drive a PortaPack running Mayhem over its USB serial console.

Cross-platform (macOS and Linux). Requires pyserial; screenshots additionally
need Pillow.

    pip install pyserial pillow

Subcommands
    port                       print the resolved serial device and exit
    info                       run `info` and print it
    cmd  "<console command>"   run any console command (quote it)
    shot [out.png]             capture the framebuffer as a PNG
    btn  N [N ...]             inject switch presses (see mapping below)
    touch X Y                  inject a touchscreen tap
    splash                     dismiss the boot splash (touch 120 150)
    launch "<App Name>"        find an app in the menus is NOT automated -- see
                               TENSORLAB_AGENT_HANDOFF.md; use btn/shot instead

Switch mapping is 1-based over enum Switch:
    1=Right 2=Left 3=Down 4=Up 5=Sel  6=DFU (AVOID)  7/8=encoder (-1/+1)
OptionsField values change with the ENCODER (7/8), not Left/Right.

Notes that cost real debugging time and are baked in here:
  * On macOS use /dev/cu.* not /dev/tty.* -- opening tty.* blocks waiting for
    carrier detect and appears to hang.
  * The device alternates between ttyACM0/ttyACM1 (Linux) and renumbers its
    cu.usbmodem name (macOS) across reboots, so always re-resolve.
  * A serial OSError mid-command is normal for `hackrf`, `reboot` and
    `sd_over_usb`: the CDC port disappears as the device switches mode. It is
    not a failure.
  * If the port opens but writes raise SerialTimeoutException, the firmware's
    USB stack is wedged -- power cycle. That is NOT a brick.
"""

from __future__ import annotations

import argparse
import glob
import os
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial missing:  pip install pyserial")

BY_ID = "/dev/serial/by-id/usb-Great_Scott_Gadgets_PortaPack_Mayhem_Transceiver-if00"
BAUD = 115200  # ignored by CDC-ACM, but pyserial wants a number


def resolve_port(explicit: str | None = None) -> str:
    """Find the PortaPack console, preferring stable identifiers."""
    if explicit:
        return explicit
    if os.getenv("PPCTL_PORT"):
        return os.environ["PPCTL_PORT"]

    # Linux: the by-id symlink is stable across ttyACM renumbering.
    if os.path.exists(BY_ID):
        return os.path.realpath(BY_ID)

    # macOS: cu.* only. tty.* blocks on open waiting for DCD.
    for pat in ("/dev/cu.usbmodem*", "/dev/cu.usbserial*"):
        hits = sorted(glob.glob(pat))
        if hits:
            return hits[0]

    # Linux fallback if by-id is absent for some reason.
    hits = sorted(glob.glob("/dev/ttyACM*"))
    if hits:
        return hits[0]

    sys.exit("no PortaPack console found. Is it powered on and in PortaPack "
             "mode (not HackRF mode, not mass-storage mode)?")


def open_port(port: str, write_timeout: float = 3.0) -> serial.Serial:
    s = serial.Serial(port, BAUD, timeout=2, write_timeout=write_timeout)
    time.sleep(0.3)
    s.reset_input_buffer()
    return s


def run_command(s: serial.Serial, cmd: str, settle: float = 1.2,
                extra: float = 0.4) -> str:
    s.write((cmd + "\r\n").encode())
    s.flush()
    time.sleep(settle)
    out = s.read(s.in_waiting or 1)
    time.sleep(extra)
    out += s.read(s.in_waiting or 0)
    return out.decode("utf-8", "replace")


def drain_until(s: serial.Serial, quiet_for: float, sentinel: bytes = b"ok") -> bytes:
    """Read until the stream goes quiet or ends with `sentinel`.

    Used for bulk replies like screenframeshort (~77KB) where a fixed sleep
    either truncates or wastes time.
    """
    buf = b""
    last = time.time()
    while time.time() - last < quiet_for:
        n = s.in_waiting
        if n:
            buf += s.read(n)
            last = time.time()
            if buf.rstrip().endswith(sentinel):
                break
        else:
            time.sleep(0.05)
    return buf


def cmd_shot(port: str, out_path: str) -> int:
    try:
        from PIL import Image
    except ImportError:
        sys.exit("screenshots need Pillow:  pip install pillow")

    s = open_port(port)
    s.write(b"screenframeshort\r\n")
    s.flush()
    raw = drain_until(s, quiet_for=6.0)
    s.close()

    # 320 rows of 240 chars. RGB222 packed as 32 + (r<<4 | g<<2 | b).
    rows = [ln for ln in raw.split(b"\r\n") if len(ln) == 240]
    if not rows:
        print("no frame data -- the app has most likely wedged the UI thread.\n"
              "Power cycle the device (this is a hang, not a brick).",
              file=sys.stderr)
        return 1

    img = Image.new("RGB", (240, len(rows)))
    px = img.load()
    for y, line in enumerate(rows):
        for x, ch in enumerate(line):
            n = ch - 32
            px[x, y] = (((n >> 4) & 3) * 85, ((n >> 2) & 3) * 85, (n & 3) * 85)
    img = img.resize((480, len(rows) * 2), Image.NEAREST)
    img.save(out_path)
    print(f"wrote {out_path}  ({len(rows)} rows)")
    print("NOTE: only 2 bits per channel, so subtle theme colours render as "
          "black here. Do not judge colour work from a screenshot.")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(prog="ppctl", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-p", "--port", help="override the serial device")
    sub = ap.add_subparsers(dest="action", required=True)

    sub.add_parser("port")
    sub.add_parser("info")
    sub.add_parser("splash")
    p_cmd = sub.add_parser("cmd"); p_cmd.add_argument("text")
    p_shot = sub.add_parser("shot"); p_shot.add_argument("out", nargs="?", default="screen.png")
    p_btn = sub.add_parser("btn"); p_btn.add_argument("n", nargs="+", type=int)
    p_touch = sub.add_parser("touch"); p_touch.add_argument("x", type=int); p_touch.add_argument("y", type=int)

    a = ap.parse_args()
    port = resolve_port(a.port)

    if a.action == "port":
        print(port)
        return 0

    if a.action == "shot":
        return cmd_shot(port, a.out)

    try:
        s = open_port(port)
    except OSError as e:
        print(f"cannot open {port}: {e}\n"
              "If the port exists but this fails, the USB stack is wedged -- "
              "power cycle.", file=sys.stderr)
        return 1

    try:
        if a.action == "info":
            print(run_command(s, "info").strip())
        elif a.action == "cmd":
            print(run_command(s, a.text, settle=2.0).strip())
        elif a.action == "splash":
            print(run_command(s, "touch 120 150", settle=1.5).strip())
        elif a.action == "touch":
            print(run_command(s, f"touch {a.x} {a.y}", settle=1.2).strip())
        elif a.action == "btn":
            for n in a.n:
                if n == 6:
                    print("refusing button 6: that is the DFU switch", file=sys.stderr)
                    continue
                out = run_command(s, f"button {n}", settle=0.9)
                print(f"button {n} -> {'ok' if 'ok' in out else out.strip()[:40]}")
    except OSError as e:
        # Expected for hackrf / reboot / sd_over_usb -- the port vanishes.
        print(f"[port dropped: {e}] -- expected if the device changed mode")
    finally:
        try:
            s.close()
        except Exception:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
