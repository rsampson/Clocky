#!/usr/bin/env python3
"""Record Clocky's touch pad over Wi-Fi (firmware built with -D TOUCH_DIAG).

    ~/.platformio/penv/bin/python tools/touch_watch.py [--host clocky.local] [--secs 60] [--out FILE]

Polls GET /api/touchraw?since=<seq> about 5x a second and writes every raw
reading as CSV: seq,ms,raw_ns,filtered_ns,touched. Prints a one-line summary
per second (raw min/max shows any mains hum) and every new press. Works on
wall power, where a USB/serial connection would change the pad's grounding.

    ~/.platformio/penv/bin/python tools/touch_watch.py replay FILE [--window 7] [--press 20] ...

re-runs the TouchPad decision logic offline on a recording, to try filter
settings without re-flashing.
"""
import argparse
import json
import socket
import sys
import time
import urllib.request


def watch(host, secs, out):
    host = socket.gethostbyname(host)  # resolve clocky.local once, not on every poll
    since, presses0, lost = 0, None, 0
    sec_raw, sec_filt, last_print = [], [], time.time()
    t_end = time.time() + secs
    with open(out, "w") as f:
        f.write("seq,ms,raw_ns,filtered_ns,touched\n")
        while time.time() < t_end:
            try:
                with urllib.request.urlopen(f"http://{host}/api/touchraw?since={since}", timeout=2) as r:
                    d = json.load(r)
            except Exception as e:  # device rebooting, Wi-Fi hiccup...
                print(f"  (poll failed: {e})", flush=True)
                time.sleep(0.5)
                continue
            if d["seq"] < since:  # device rebooted
                print("  (device restarted)", flush=True)
                since, presses0 = 0, None
                continue
            recs = d["s"]
            if recs and since and recs[0][0] != since + 1:
                lost += recs[0][0] - since - 1
            for seq, ms, raw, filt, touched in recs:
                f.write(f"{seq},{ms},{raw},{filt},{touched}\n")
                if raw:
                    sec_raw.append(raw)
                sec_filt.append(filt)
            since = d["seq"]
            if presses0 is None:
                presses0 = d["presses"]
            elif d["presses"] != presses0:
                print(f"  PRESS -> count {d['presses']} (+{d['presses'] - presses0} this run)", flush=True)
                presses0 = d["presses"]  # keep printing increments
            if time.time() - last_print >= 1 and sec_raw:
                base = d["base"]
                print(f"{time.strftime('%H:%M:%S')} base {base/1000:5.2f}us  raw {min(sec_raw)/1000:6.2f}..{max(sec_raw)/1000:6.2f}"
                      f"  filtered {min(sec_filt)/1000:6.2f}..{max(sec_filt)/1000:6.2f}  nearMisses {d['nearMisses']}", flush=True)
                sec_raw, sec_filt, last_print = [], [], time.time()
            time.sleep(0.2)
    print(f"wrote {out}; samples lost between polls: {lost}")


TIMEOUT_NS = 1_000_000  # a timed-out reading's window value: TouchPad::Config::timeoutUs * 1000


def replay(path, window, press, release, confirm, shift=10, stuck=12, relearn_ms=30000):
    """Offline port of TouchPad::decide() (lib/TouchPad/src/TouchPad.cpp); keep
    the two in sync. Defaults mirror TouchPad::Config."""
    stuck = max(1, min(stuck, confirm - 1))  # as the TouchPad constructor clamps stuckChecks
    rows = [list(map(int, l.split(","))) for l in open(path).read().splitlines()[1:] if l]
    win, bq, touched, above, presses, near, peak, run0, high_since = [], 0, False, 0, 0, 0, 0, 0, 0
    for _seq, ms, raw, _filt, _t in rows:
        run0 = run0 + 1 if raw == 0 else 0
        win = (win + [raw or TIMEOUT_NS])[-window:]  # a timeout counts as a very long reading ...
        if len(win) < window:
            continue
        if run0 >= stuck:  # ... unless it's a sustained run: pad stuck => untouched
            touched, above, high_since, peak = False, 0, 0, 0
            continue
        r = sorted(win)[window // 2]
        if bq == 0:  # no baseline yet; never adopt one that is mostly timeouts
            if r != TIMEOUT_NS:
                bq = r << 8
            continue
        base = bq >> 8
        p, rel = base + base * press // 100, base + base * release // 100
        if r < rel:  # held above the release level too long: re-learn the baseline
            high_since = 0
        elif high_since == 0:
            high_since = ms | 1
        elif ms - high_since >= relearn_ms and r != TIMEOUT_NS:  # never onto a timeout
            bq, touched, above, high_since, peak = r << 8, False, 0, 0, 0
            continue
        if touched:
            touched = r >= rel
            continue
        if r > p:
            above += 1
            if above >= confirm:
                touched, above, peak, presses = True, 0, 0, presses + 1
                continue
        else:
            above = 0
        if r >= rel:
            peak = max(peak, r)
            continue
        if peak > base + base * press // 200:
            near += 1
        peak = 0
        rq = r << 8
        bq = bq - ((bq - rq) >> 4) if rq < bq else bq + ((rq - bq) >> shift)
    print(f"window {window} press +{press}% release +{release}% confirm {confirm}: "
          f"{presses} presses, {near} near-misses over {len(rows)} samples")


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "replay":
        ap = argparse.ArgumentParser()
        ap.add_argument("cmd")
        ap.add_argument("file")
        ap.add_argument("--window", type=int, default=7)
        ap.add_argument("--press", type=int, default=35)
        ap.add_argument("--release", type=int, default=8)
        ap.add_argument("--confirm", type=int, default=15)
        ap.add_argument("--shift", type=int, default=10)
        ap.add_argument("--stuck", type=int, default=12)
        ap.add_argument("--relearn", type=int, default=30000, help="ms")
        a = ap.parse_args()
        replay(a.file, a.window, a.press, a.release, a.confirm, a.shift, a.stuck, a.relearn)
        return
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="clocky.local")
    ap.add_argument("--secs", type=float, default=60)
    ap.add_argument("--out", default="touch_watch.csv")
    a = ap.parse_args()
    watch(a.host, a.secs, a.out)


if __name__ == "__main__":
    main()
