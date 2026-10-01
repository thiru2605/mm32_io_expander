#!/usr/bin/env python3
"""ESP32 simulator for the MM32F0020 I/O expander: configure outputs, then
drive them from typed ASCII commands.

Usage:  python iox_esp_sim.py COM5            (needs: pip install pyserial)
Wiring: USB-TTL TX -> PA3 (TSSOP 3), RX <- PA12 (TSSOP 2), GND, 3.3 V logic.

Pins are TSSOP-20 package pin numbers. Commands (case-insensitive):
    13 1          pin 13 high         (1/h/high/on, 0/l/low/off, t/toggle)
    13=0          pin 13 low
    12,13,14 1    several pins at once (one WRITE_MASK)
    all 0         every configured pin
    read          debounced levels / raw pins / output mask
    status        uptime, VDD, reset cause, error counters
    ping          link check
    help, quit
"""
import re
import struct
import sys

from iox_test import (Link, PING, GET_STATUS, SET_MODE_MASK, WRITE_MASK, READ_ALL)

# TSSOP-20 pin -> (port pin, expander channel), spec 2.3
PIN_MAP = {1: ("PA11", 2), 12: ("PA5", 9), 13: ("PA6", 10),
           14: ("PA7", 5), 15: ("PA8", 11), 16: ("PA9", 12)}
PINS = sorted(PIN_MAP)

CH_OUTPUT = 1
EV_BOOTED, EV_PIN_CHANGE, EV_FAULT = 0x80, 0x81, 0x82
STATUS = {0: "OK", 1: "UNKNOWN_CMD", 2: "BAD_LEN", 3: "BAD_CH", 4: "BAD_TYPE", 5: "BAD_PARAM", 6: "BUSY"}
HIGH = {"1", "h", "high", "on"}
LOW = {"0", "l", "low", "off"}
TOGGLE = {"t", "toggle"}


def ch_mask(pins):
    m = 0
    for p in pins:
        m |= 1 << PIN_MAP[p][1]
    return m


class Esp:
    def __init__(self, port):
        self.L = Link(port)
        self.state = {p: 0 for p in PINS}                                       # last level we commanded

    def send(self, fid, payload=b"", tries=3):
        """Stop-and-wait: retries reuse the same seq, so the MM32 resends its cached reply."""
        self.L.seq = (self.L.seq + 1) & 0xFF
        seq = self.L.seq
        for _ in range(tries):
            r = self.L.cmd(fid, payload, seq=seq)
            if r is not None:
                self.show_events()
                return r
        print("  ! no response (check wiring / baud / power)")
        return None

    def ok(self, r, what):
        if r is None:
            return False
        if r[0] != 0:
            print("  ! %s failed: %s" % (what, STATUS.get(r[0], r[0])))
            return False
        return True

    def configure(self):
        """All mapped pins -> OUTPUT push-pull, low (level is applied before the pin drives)."""
        r = self.send(SET_MODE_MASK, struct.pack("<HBB", ch_mask(PINS), CH_OUTPUT, 0))
        if self.ok(r, "configure"):
            self.state = {p: 0 for p in PINS}
            print("  configured pins %s as outputs, all LOW" % ",".join(map(str, PINS)))

    def write(self, pins, level):
        if level == "t":
            hi = [p for p in pins if not self.state[p]]
            lo = [p for p in pins if self.state[p]]
            vals = ch_mask(hi)
        else:
            hi, lo = (pins, []) if level else ([], pins)
            vals = ch_mask(pins) if level else 0
        r = self.send(WRITE_MASK, struct.pack("<HH", ch_mask(pins), vals))
        if self.ok(r, "write"):
            for p in hi:
                self.state[p] = 1
            for p in lo:
                self.state[p] = 0
            print("  " + "  ".join("pin%d=%s" % (p, "H" if self.state[p] else "L") for p in pins))

    def read(self):
        r = self.send(READ_ALL)
        if not self.ok(r, "read"):
            return
        levels, raw, outs = struct.unpack_from("<HHH", r, 1)
        for p in PINS:
            port, ch = PIN_MAP[p]
            bit = 1 << ch
            print("  pin %2d %-4s CH%-2d  %-6s  level=%d  raw=%d" % (
                p, port, ch, "OUTPUT" if outs & bit else "input", bool(levels & bit), bool(raw & bit)))

    def status(self):
        r = self.send(GET_STATUS)
        if self.ok(r, "status"):
            up, vdd, rc, fl, ue, ed = struct.unpack_from("<IHBBHH", r, 1)
            print("  uptime=%.1fs vdd=%dmV reset_cause=%#04x flags=%#04x uart_err=%d evt_drops=%d"
                  % (up / 1000.0, vdd, rc, fl, ue, ed))

    def show_events(self):
        ev, self.L.events = self.L.events, []
        for seq, eid, p in ev:
            if eid == EV_BOOTED:
                print("  * BOOTED (reset_cause=%#04x, fw %d.%d) - MM32 lost its config, re-applying" % (p[0], p[1], p[2]))
                self.configure()
            elif eid == EV_FAULT:
                code, detail = p[0], struct.unpack_from("<H", p, 1)[0]
                print("  * FAULT code=%d detail=%#06x" % (code, detail))
            elif eid == EV_PIN_CHANGE:
                ch, lv, t = struct.unpack_from("<HHI", p)
                print("  * PIN_CHANGE changed=%#06x levels=%#06x t=%d" % (ch, lv, t))


def parse_pins(s):
    if s == "all":
        return PINS
    pins = [int(x) for x in s.split(",") if x]
    bad = [p for p in pins if p not in PIN_MAP]
    if bad:
        raise ValueError("pin(s) %s not configurable; use %s" % (bad, PINS))
    return pins


def main(port):
    esp = Esp(port)
    esp.L.drain_events(0.3)
    esp.show_events()
    r = esp.send(PING, b"sim")
    if r != b"\x00sim":
        print("MM32 not answering PING on %s" % port)
        return 1
    esp.configure()
    print(__doc__.split("Pins are", 1)[1].split("\n", 1)[1])

    while True:
        try:
            line = input("iox> ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            print()
            break
        if not line:
            esp.L.drain_events(0.05)
            esp.show_events()
            continue
        if line in ("q", "quit", "exit"):
            break
        if line in ("h", "help", "?"):
            print(__doc__)
            continue
        if line == "read":
            esp.read()
            continue
        if line == "status":
            esp.status()
            continue
        if line == "ping":
            print("  " + ("OK" if esp.send(PING, b"p") == b"\x00p" else "no reply"))
            continue
        if line == "config":
            esp.configure()
            continue

        m = re.fullmatch(r"(all|[\d,]+)\s*[= ]\s*(\S+)", line)
        if not m:
            print("  ? try: 13 1   13=0   12,13 h   all off   read   help")
            continue
        try:
            pins = parse_pins(m.group(1))
        except ValueError as e:
            print("  ! %s" % e)
            continue
        lv = m.group(2)
        if lv in HIGH:
            esp.write(pins, 1)
        elif lv in LOW:
            esp.write(pins, 0)
        elif lv in TOGGLE:
            esp.write(pins, "t")
        else:
            print("  ? level must be 1/0, h/l, on/off or t")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "COM5"))
