#!/usr/bin/env python3
"""Mock Clocky device — serves the real dashboard assets out of
src/web_assets.cpp and implements the /api/* routes in memory, so the UI can be
developed and tested without flashing hardware.

    python3 tools/mock_server.py        # then open http://localhost:8080

State is per-process only. Log lines accumulate as you poke the UI, so you can
see the same round-trip behaviour the device gives you.
"""

import json
import re
import time
import pathlib
from http.server import BaseHTTPRequestHandler, HTTPServer

ROOT = pathlib.Path(__file__).resolve().parent.parent
ASSETS = ROOT / "src" / "web_assets.cpp"

# Pull the three PROGMEM raw-string literals straight out of the .cpp so this
# harness can never drift from what the firmware actually serves.
SRC = ASSETS.read_text()


def extract(tag):
    m = re.search(r'R"' + tag + r"\((.*?)\)" + tag + r'"', SRC, re.S)
    if not m:
        raise SystemExit(f"could not find R\"{tag}(...)\" in {ASSETS}")
    return m.group(1)


PAGES = {
    "/": ("text/html", extract("HTML")),
    "/style.css": ("text/css", extract("CSS")),
    "/app.js": ("application/javascript", extract("JS")),
}

state = {
    "timezone": "Pacific Standard Time",
    "timezoneCode": "PST",
    "rssi": -52,
    "disabled": False,
    "runHour": 7,
    "runMinute": 30,
    "activeDays": 0x7F,
    "ssid": "SquirrelNet",
    "apMode": False,
    "log": [],
}


def web_print(line):
    state["log"].append(line)
    del state["log"][:-40]


for line in ("WiFi connected, IP 192.168.1.42", "NTP sync ok", "Button press count: 01"):
    web_print(line)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _send(self, code, ctype, body):
        raw = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self):
        if self.path in PAGES:
            ctype, body = PAGES[self.path]
            return self._send(200, ctype, body)
        if self.path == "/api/status":
            t = time.localtime()
            snap = dict(state)
            snap["time"] = time.strftime("%H:%M:%S %m/%d", t)
            snap["weekday"] = (t.tm_wday + 1) % 7 + 1  # 1=Sunday..7=Saturday
            snap["log"] = "\n".join(state["log"])
            return self._send(200, "application/json", json.dumps(snap))
        if self.path == "/update":
            return self._send(200, "text/html", "<p>ElegantOTA stub</p>")
        self._send(404, "text/plain", "not found")

    def do_POST(self):
        n = int(self.headers.get("Content-Length") or 0)
        body = json.loads(self.rfile.read(n) or "{}") if n else {}
        p = self.path
        if p == "/api/alarm":
            state["disabled"] = bool(body.get("disable"))
            web_print("Alarm %s" % ("disabled" if state["disabled"] else "enabled"))
        elif p == "/api/schedule":
            state["runHour"] = body.get("hour", state["runHour"])
            state["runMinute"] = body.get("minute", state["runMinute"])
            state["activeDays"] = body.get("activeDays", state["activeDays"])
            web_print("Saved %s alarm hour: %d and minute: %d" % (
                "disabled" if state["disabled"] else "active",
                state["runHour"], state["runMinute"]))
        elif p == "/api/wifi":
            state["ssid"] = body.get("ssid") or state["ssid"]
            web_print("Saved WiFi credentials for %s" % state["ssid"])
        elif p == "/api/timezone":
            state["timezoneCode"] = body.get("tz", "UTC")
            state["timezone"] = state["timezoneCode"]
            web_print("Time zone set to %s" % state["timezoneCode"])
        elif p == "/api/settime":
            web_print("Browser time accepted")
        elif p == "/api/reboot":
            web_print("Rebooting")
        else:
            return self._send(404, "text/plain", "not found")
        self._send(200, "text/plain", "ok")


if __name__ == "__main__":
    print("Clocky mock device on http://localhost:8080")
    HTTPServer(("", 8080), Handler).serve_forever()
