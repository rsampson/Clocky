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
import sys
import time
import urllib.request


def watch(host, secs, out):
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


def replay(path, window, press, release, confirm, shift=10):
    rows = [list(map(int, l.split(","))) for l in open(path).read().splitlines()[1:] if l]
    raws = [r[2] for r in rows]
    win, base, touched, above, presses, near, peak, bq = [], 0, False, 0, 0, 0, 0, 0
    run0 = 0
    for raw in raws:
        run0 = run0 + 1 if raw == 0 else 0
        win = (win + [raw or 1_000_000])[-window:]  # a timeout counts as a very long reading ...
        if len(win) < window:
            continue
        if run0 >= 12:  # ... unless it's a sustained run: pad stuck => untouched
            touched, above = False, 0
            continue
        r = sorted(win)[window // 2]
        if base == 0:
            base, bq = r, r << 8
            continue
        p, rel = base + base * press // 100, base + base * release // 100
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
        base = bq >> 8
    print(f"window {window} press +{press}% release +{release}% confirm {confirm}: "
          f"{presses} presses, {near} near-misses over {len(raws)} samples")


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
        a = ap.parse_args()
        replay(a.file, a.window, a.press, a.release, a.confirm, a.shift)
        return
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="clocky.local")
    ap.add_argument("--secs", type=float, default=60)
    ap.add_argument("--out", default="touch_watch.csv")
    a = ap.parse_args()
    watch(a.host, a.secs, a.out)


if __name__ == "__main__":
    main()
