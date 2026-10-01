#!/usr/bin/env python3
"""Bench test for the MM32F0020 UART I/O expander (KU framing, IOX in KU cmd 0x40).

Usage:  python iox_test.py COM5            (needs: pip install pyserial)

Wire:   55 AA ver | 40 | lenH lenL | type seq id payload | chk
        ver = 00 ESP->MCU, 03 MCU->ESP;  len = 3 + payload bytes (big-endian)
        chk = sum of all preceding bytes & 0xFF
"""
import struct
import sys
import time

import serial

KU_HDR = b"\x55\xAA"
KU_VER_TX, KU_VER_RX = 0x00, 0x03                                               # as seen from this (ESP) side
KU_IOX = 0x40
T_CMD, T_RSP, T_EVT = 1, 2, 3

PING, GET_INFO, GET_STATUS, RESET, SET_FRAMING = 0x01, 0x02, 0x03, 0x04, 0x06
ENTER_BOOT = 0x05                                                               # removed: must answer UNKNOWN_CMD
HEARTBEAT, SET_HB_CONTENT = 0x07, 0x08
SET_MODE, SET_MODE_MASK, GET_CONFIG, SET_DIG_EVENT = 0x10, 0x11, 0x12, 0x13
WRITE, WRITE_MASK, READ_ALL, READ_ADC, READ_ADC_ALL = 0x20, 0x21, 0x22, 0x23, 0x24
SET_LINK_WDT = 0x30
MAGIC_RESET = 0x52535421


def ku(cmd, data=b"", ver=KU_VER_TX):
    f = KU_HDR + bytes([ver, cmd, len(data) >> 8, len(data) & 0xFF]) + data
    return f + bytes([sum(f) & 0xFF])


def encode(ftype, seq, fid, payload=b""):
    return ku(KU_IOX, bytes([ftype, seq & 0xFF, fid]) + payload)


class Link:
    def __init__(self, port):
        self.s = serial.Serial(port, 115200, timeout=0.02)
        self.buf = bytearray()
        self.seq = 0
        self.events = []

    def _frames(self, deadline):
        while time.time() < deadline:
            self.buf += self.s.read(256)
            while True:
                i = self.buf.find(KU_HDR)
                if i < 0:
                    del self.buf[:-1]
                    break
                del self.buf[:i]
                if len(self.buf) < 6:
                    break
                n = (self.buf[4] << 8) | self.buf[5]
                if len(self.buf) < 7 + n:
                    break
                f = bytes(self.buf[:7 + n])
                if sum(f[:-1]) & 0xFF != f[-1] or f[2] != KU_VER_RX or n < 3:
                    del self.buf[:1]
                    continue
                del self.buf[:7 + n]
                if f[3] == KU_IOX:
                    yield f[6], f[7], f[8], f[9:-1]

    def cmd(self, fid, payload=b"", seq=None, timeout=0.2, raw=None):
        if seq is None:
            self.seq = (self.seq + 1) & 0xFF
            seq = self.seq
        self.s.write(raw if raw is not None else encode(T_CMD, seq, fid, payload))
        for t, sq, i, p in self._frames(time.time() + timeout):
            if t == T_EVT:
                self.events.append((sq, i, p))
            elif t == T_RSP and sq == seq and i == fid:
                return p
        return None

    def drain_events(self, secs=0.1):
        for t, sq, i, p in self._frames(time.time() + secs):
            if t == T_EVT:
                self.events.append((sq, i, p))
        ev, self.events = self.events, []
        return ev


FAILS = []


def check(name, cond, info=""):
    print(("PASS " if cond else "FAIL ") + name + (("  " + str(info)) if info else ""))
    if not cond:
        FAILS.append(name)


def main(port):
    L = Link(port)
    L.drain_events(0.3)

    r = L.cmd(PING, b"hello")
    check("PING echo", r == b"\x00hello", r)

    r = L.cmd(GET_INFO)
    check("GET_INFO len 21", r is not None and len(r) == 21, r and r.hex())
    if r:
        fw, fwm, pm, pmn = r[1:5]
        chn, adcm, fr = r[17], struct.unpack_from("<H", r, 18)[0], r[20]
        check("GET_INFO fields", chn == 13 and adcm == 0x3F and fr == 4, (fw, fwm, pm, pmn, chn, hex(adcm), fr))

    r = L.cmd(GET_STATUS)
    check("GET_STATUS len 13", r is not None and len(r) == 13, r and r.hex())
    if r:
        up, vdd, rc, fl, ue, ed = struct.unpack_from("<IHBBHH", r, 1)
        print("     uptime=%d vdd=%dmV reset_cause=%#x flags=%#x uart_err=%d evt_drops=%d" % (up, vdd, rc, fl, ue, ed))

    # error statuses
    check("unknown cmd -> 1", L.cmd(0x7E) == b"\x01")
    check("ENTER_BOOT removed -> 1", L.cmd(ENTER_BOOT, struct.pack("<I", 0x424F4F54)) == b"\x01")
    check("bad len -> 2", L.cmd(GET_INFO, b"\x00") == b"\x02")
    check("bad ch -> 3", L.cmd(SET_MODE, bytes([13, 0, 0])) == b"\x03")
    check("ADC on CH6 -> 4", L.cmd(SET_MODE, bytes([6, 2, 0])) == b"\x04")
    check("bad param -> 5", L.cmd(SET_MODE, bytes([0, 0, 3])) == b"\x05")
    check("WRITE to input -> 4", L.cmd(WRITE, bytes([7, 1])) == b"\x04")
    check("SET_FRAMING Sync -> 5", L.cmd(SET_FRAMING, b"\x01") == b"\x05")
    check("SET_FRAMING KU -> 0", L.cmd(SET_FRAMING, b"\x04") == b"\x00")
    check("mask all-or-nothing", L.cmd(SET_MODE_MASK, struct.pack("<HBB", 0x0041, 2, 0)) == b"\x04"
          and L.cmd(GET_CONFIG, b"\x00")[2] == 0)

    # dup-seq resend: same seq+id returns identical bytes without re-executing
    L.cmd(SET_MODE, bytes([12, 1, 0]))
    s = (L.seq + 1) & 0xFF
    a = L.cmd(PING, b"A", seq=s)
    b = L.cmd(PING, b"B", seq=s)
    check("dup seq resends cached response", a == b == b"\x00A", (a, b))

    # bad checksum dropped silently, then a valid frame still works
    bad = bytearray(encode(T_CMD, 0x55, PING, b"x"))
    bad[-1] ^= 0xFF
    check("bad checksum -> no reply", L.cmd(PING, raw=bytes(bad), seq=0x55, timeout=0.1) is None)
    check("valid after bad", L.cmd(PING, b"ok") == b"\x00ok")

    # 0x55 inside a frame must not break framing
    check("seq 0x55 works", L.cmd(PING, b"s", seq=0x55 ^ 0x01) == b"\x00s"
          and L.cmd(PING, b"U", seq=0x55) == b"\x00U")
    check("payload full of 0x55/0xAA", L.cmd(PING, b"\x55\xAA\x55\x55\xAA") == b"\x00\x55\xAA\x55\x55\xAA")

    # other KU commands (heartbeat 00, SetDP 06) are valid KU but not IOX: ignored
    L.s.write(ku(0x00, b"\x00"))
    L.s.write(ku(0x06, bytes([0x02, 0x01, 0x00, 0x02, 1, 1])))
    check("non-0x40 KU frames ignored", L.cmd(PING, b"ku") == b"\x00ku")

    # garbage + false header + partial frame, then a valid frame (rescan)
    L.s.write(b"\x00\x55\x13\x55\xAA\x00\x40")
    check("resync after garbage", L.cmd(PING, b"rs") == b"\x00rs")
    L.s.write(b"\x55\xAA\x00\x40\x00\x10\x01")                                  # partial, len 16
    time.sleep(0.05)                                                            # > 20 ms idle
    check("partial frame discarded after 20 ms", L.cmd(PING, b"pt") == b"\x00pt")

    # outputs + READ_ALL
    check("SET_MODE CH12 output", L.cmd(SET_MODE, bytes([12, 1, 1])) == b"\x00")
    r = L.cmd(READ_ALL)
    lv, raw, om = struct.unpack_from("<HHH", r, 1)
    check("READ_ALL out_mask/level", om & 0x1000 and lv & 0x1000, (hex(lv), hex(raw), hex(om)))
    check("WRITE_MASK non-output -> 4", L.cmd(WRITE_MASK, struct.pack("<HH", 0x1001, 0)) == b"\x04")
    check("WRITE_MASK", L.cmd(WRITE_MASK, struct.pack("<HH", 0x1000, 0)) == b"\x00")

    # heartbeat
    r = L.cmd(HEARTBEAT)
    check("HEARTBEAT default mask 0x3F", r is not None and r[0] == 0 and r[1] == 0x3F, r and r.hex())
    check("SET_HB_CONTENT 0x06", L.cmd(SET_HB_CONTENT, b"\x06") == b"\x00")
    r = L.cmd(HEARTBEAT)
    check("HEARTBEAT mask 0x06 size", r is not None and len(r) == 2 + 4 + 2, r and r.hex())
    L.cmd(SET_HB_CONTENT, b"\x3F")

    # ADC: CH0 as ADC
    check("SET_MODE CH0 ADC", L.cmd(SET_MODE, bytes([0, 2, 0])) == b"\x00")
    time.sleep(0.05)
    for avg in (0, 1, 4, 16):
        r = L.cmd(READ_ADC, bytes([0, avg]))
        if r:
            _, raw, mv = struct.unpack_from("<BHH", r, 1)
            check("READ_ADC avg=%d" % avg, r[0] == 0, "raw=%d mv=%d" % (raw, mv))
        else:
            check("READ_ADC avg=%d" % avg, False)
    check("READ_ADC bad avg -> 5", L.cmd(READ_ADC, bytes([0, 2])) == b"\x05")
    r = L.cmd(READ_ADC_ALL, b"\x00")
    check("READ_ADC_ALL count 1", r is not None and r[1] == 1, r and r.hex())

    # jumper test (optional): CH12 output -> CH6 input
    print("\n-- jumper CH12 (PA9) to CH6 (PA0) for the edge test --")
    L.cmd(SET_MODE, bytes([6, 0, 2]))
    L.cmd(SET_DIG_EVENT, bytes([6, 3, 5]))
    L.drain_events(0.05)
    L.cmd(WRITE, bytes([12, 1]))
    time.sleep(0.05)
    L.cmd(WRITE, bytes([12, 0]))
    ev = [e for e in L.drain_events(0.1) if e[1] == 0x81]
    print("     PIN_CHANGE events:", [(e[0], e[2].hex()) for e in ev])
    r = L.cmd(HEARTBEAT)
    print("     heartbeat:", r and r.hex())

    # link watchdog: 200 ms, force CH12 high
    check("SET_LINK_WDT", L.cmd(SET_LINK_WDT, struct.pack("<HHH", 200, 0x1000, 0x1000)) == b"\x00")
    ev = L.drain_events(0.4)
    check("link WDT FAULT(3)", any(e[1] == 0x82 and e[2][0] == 3 for e in ev), ev)
    L.cmd(SET_LINK_WDT, struct.pack("<HHH", 0, 0, 0))

    # reset
    r = L.cmd(RESET, struct.pack("<I", MAGIC_RESET))
    check("RESET ack", r == b"\x00")
    ev = L.drain_events(0.3)
    booted = [e for e in ev if e[1] == 0x80]
    check("BOOTED after RESET (sw cause)", booted and booted[0][2][0] & 0x10, ev)

    print("\n%d failure(s)" % len(FAILS))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "COM5"))
