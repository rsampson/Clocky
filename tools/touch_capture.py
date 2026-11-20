#!/usr/bin/env python3
"""Capture and analyse touch-pad readings from the touch_tuner firmware.

Run with PlatformIO's bundled Python (it already has pyserial):

    ~/.platformio/penv/bin/python tools/touch_capture.py capture [--port /dev/ttyUSB0] [--out FILE]
    ~/.platformio/penv/bin/python tools/touch_capture.py analyse FILE [FILE ...]   (pooled)

`capture` opens the port without toggling DTR/RTS (so the board doesn't
reset), sends 's' to start the guided test, and records every line until the
firmware prints "# done". Then it runs the analysis on what it recorded.
"""
import argparse
import csv
import statistics
import sys
import time
from collections import defaultdict


def find_port():
    from serial.tools import list_ports  # from the PlatformIO penv
    ports = sorted(p.device for p in list_ports.comports() if "USB" in p.device or "ACM" in p.device)
    if not ports:
        sys.exit("no USB serial port found; pass --port")
    return ports[0]


def capture(port, out, timeout_s):
    import serial  # from the PlatformIO penv

    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = port, 115200, 0.5
    ser.dtr = ser.rts = False  # don't reset the ESP8266 on open
    ser.open()
    time.sleep(0.5)
    ser.reset_input_buffer()
    ser.write(b"x")
    time.sleep(0.2)
    ser.write(b"s")
    n = 0
    t_end = time.time() + timeout_s
    with open(out, "w") as f:
        while time.time() < t_end:
            line = ser.readline().decode("ascii", "replace").strip()
            if not line:
                continue
            f.write(line + "\n")
            if line.startswith("#"):
                print(line, flush=True)
                if line == "# done":
                    break
            else:
                n += 1
    ser.close()
    print(f"wrote {n} data lines to {out}")


def pct(vals, p):
    """p-th percentile (1..99), linearly interpolated."""
    if len(vals) < 2:
        return vals[0] if vals else 0
    return statistics.quantiles(vals, n=100, method="inclusive")[p - 1]


def analyse(paths):
    # data[(settle_us, kind)][phase] -> list of ns ; kind = rise/fall/sum
    data = defaultdict(lambda: defaultdict(list))
    timeouts = defaultdict(int)
    rows = []
    for path in paths:
        with open(path) as f:
            rows.extend(csv.reader(f))
    for row in rows:
        if not row or row[0] != "D" or len(row) != 6:
            continue
        _, phase, _ms, settle, rise, fall = row
        settle, rise, fall = int(settle), int(rise), int(fall)
        if rise == 0 or fall == 0:
            timeouts[(phase, settle)] += 1
            continue
        data[(settle, "rise")][phase].append(rise)
        data[(settle, "fall")][phase].append(fall)
        data[(settle, "sum")][phase].append(rise + fall)

    if timeouts:
        print("timeouts (phase, settle_us): count ->", dict(timeouts))

    phases = ["off", "hover", "light", "tap", "hold"]
    for key in sorted(data):
        settle, kind = key
        print(f"\n=== settle {settle} us, {kind} (ns) ===")
        print(f"{'phase':6} {'n':>5} {'min':>7} {'p1':>7} {'p50':>7} {'p99':>7} {'max':>7} {'sd':>6}")
        stats = {}
        for ph in phases:
            v = sorted(data[key].get(ph, []))
            if not v:
                continue
            stats[ph] = v
            sd = statistics.pstdev(v)
            print(f"{ph:6} {len(v):5d} {v[0]:7d} {pct(v,1):7.0f} {pct(v,50):7.0f} "
                  f"{pct(v,99):7.0f} {v[-1]:7d} {sd:6.0f}")
        if "off" in stats and "hold" in stats:
            off, hold = stats["off"], stats["hold"]
            gap = hold[0] - off[-1]
            mid = (pct(hold, 1) + pct(off, 99)) / 2
            sd = statistics.pstdev(off) or 1
            print(f"  touch shift (p50): {pct(hold,50) - pct(off,50):.0f} ns; "
                  f"worst-case gap hold.min - off.max: {gap} ns; "
                  f"(hold.p50 - off.p50)/off.sd = {(pct(hold,50) - pct(off,50)) / sd:.1f}")
            for ph in ("hover", "light"):
                if ph in stats:
                    v = stats[ph]
                    above = sum(x > mid for x in v) / len(v)
                    print(f"  {ph}: {above*100:.0f}% of samples above midpoint {mid:.0f} ns")


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("capture")
    c.add_argument("--port")
    c.add_argument("--out", default="touch_capture.csv")
    c.add_argument("--timeout", type=float, default=300)
    a = sub.add_parser("analyse")
    a.add_argument("files", nargs="+")
    args = ap.parse_args()
    if args.cmd == "capture":
        capture(args.port or find_port(), args.out, args.timeout)
        analyse([args.out])
    else:
        analyse(args.files)


if __name__ == "__main__":
    main()
