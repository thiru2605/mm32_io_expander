# MM32F0020 I/O Expander — Host Developer Guide

For firmware developers writing the **host** side: the ESP32, or any other MCU, that controls the expander over UART.

The expander is an MM32F0020 (TSSOP-20) running fixed firmware. It gives the host **13 GPIO channels**: 6 of them can also read analog values, with debounced input events and a link watchdog. The host configures each channel at runtime, then reads and writes it with short binary commands. Nothing is stored on the expander, so after every reset the host configures it again.

Covers expander **firmware 1.0 / protocol 1.0**.

---

## 1. What's in this folder

| File | Use it for |
|---|---|
| `iox_protocol.h` | Every constant in the protocol: IDs, lengths, status codes, channel map, timings. No code; include it anywhere. |
| `iox_host.h` | Header-only C99/C++ helpers: build requests, parse the incoming byte stream, decode responses and events. No malloc, no I/O. |
| `iox_host_example.c` | Reference driver: stop-and-wait, retries, event handling, re-configuring after a reboot. Port it by implementing 3 functions (UART write, UART read, millis). |
| `IOX_HOST_GUIDE.md` | This guide. |
| `../tools/iox_test.py` | Python bench test: runs about 40 protocol checks against a real board through a USB-UART adapter. |
| `../tools/iox_esp_sim.py` | Interactive Python host: type `13 1` to drive pin 13 high. Handy for bring-up. |

Every value in `iox_protocol.h` is fixed at compile time in the expander firmware. The host can't change them.

---

## 2. At a glance

| | |
|---|---|
| Link | UART, **115200 baud, 8N1**, 3.3 V logic |
| Expander pins | **PA12 (pin 2) = expander TX** → host RX · **PA3 (pin 3) = expander RX** ← host TX |
| Framing | KU envelope, KU command `0x40`, 8-bit additive checksum |
| Flow | Stop-and-wait: one command in flight; every command gets exactly one response |
| Channels | 13 (CH0–CH12); CH0–CH5 can also be ADC |
| Boot state | Every channel **floating input**, no events, heartbeat mask `0x3F` |
| Config storage | RAM only. Lost on every reset (the host re-applies it) |
| Typical round trip | ~2 ms (WRITE + response at 115200) |
| Largest frame | 74 bytes (~6.4 ms on the wire) |

---

## 3. Hardware

### 3.1 Wiring
| Expander (TSSOP-20) | Host |
|---|---|
| PA12, pin 2 (UART1 TX) | UART RX |
| PA3, pin 3 (UART1 RX) | UART TX |
| VSS, pin 7 | GND |
| VDD, pin 9 | 3.3 V (same rail as the host recommended) |
| PA10, pin 4 (NRST) | Optional: host GPIO, open-drain, for a hard reset |
| PA13/PA14, pins 18/17 | SWD only (programming). **Not** connected to the host. |

There is **no bootloader entry** from the host. BOOT0 isn't wired, and expander firmware is updated over SWD. Command `0x05` (formerly ENTER_BOOTLOADER) returns `UNKNOWN_CMD`.

### 3.2 Electrical limits
- **Not 5 V tolerant.** Keep every pin between −0.3 V and VDD + 0.3 V.
- Up to ±20 mA per pin, **60 mA total** for the chip.
- Internal pull-up/pull-down: 50–75 kΩ (60 kΩ typical). They're weak, so use external resistors for long wires or noisy environments.
- **Board rule:** any channel that drives a load (relay driver, MOSFET gate) needs an **external pull resistor to its safe level**. From power-up until the host configures it, every channel is a floating input, so the load input must not float.

### 3.3 Channel map

Channel *n* is bit *n* in every 16-bit mask. Bits 13–15 must always be 0.

| CH | Port pin | TSSOP pin | ADC | `iox_protocol.h` name |
|---|---|---|---|---|
| 0 | PB1 | 6 | ✓ | `IOX_CH0_PB1` |
| 1 | PB0 | 5 | ✓ | `IOX_CH1_PB0` |
| 2 | PA11 | 1 | ✓ | `IOX_CH2_PA11` |
| 3 | PA2 | 20 | ✓ | `IOX_CH3_PA2` |
| 4 | PA15 | 19 | ✓ | `IOX_CH4_PA15` |
| 5 | PA7 | 14 | ✓ | `IOX_CH5_PA7` |
| 6 | PA0 | 10 | | `IOX_CH6_PA0` |
| 7 | PA1 | 8 | | `IOX_CH7_PA1` |
| 8 | PA4 | 11 | | `IOX_CH8_PA4` |
| 9 | PA5 | 12 | | `IOX_CH9_PA5` |
| 10 | PA6 | 13 | | `IOX_CH10_PA6` |
| 11 | PA8 | 15 | | `IOX_CH11_PA8` |
| 12 | PA9 | 16 | | `IOX_CH12_PA9` |

`IOX_TSSOP_TO_CH(pin)` converts a package pin number to a channel. It returns −1 for the UART, NRST, power and SWD pins.

### 3.4 Channel types

| type | param | Allowed on |
|---|---|---|
| `IOX_TYPE_INPUT` (0) | `IOX_IN_FLOAT` 0 · `IOX_IN_PULLUP` 1 · `IOX_IN_PULLDOWN` 2 | All channels |
| `IOX_TYPE_OUTPUT` (1) | Starting level: `IOX_OUT_LOW` 0 · `IOX_OUT_HIGH` 1 | All channels (push-pull only) |
| `IOX_TYPE_ADC` (2) | Always 0 | CH0–CH5 only |

Details:
- **Outputs start glitch-free.** The level is written first, then the pin starts driving.
- **Inputs are sampled every 1 ms,** with a per-channel debounce of 0–255 ms. 0 means one scan.
- **ADC readings** are 12-bit (0–4095) against VDD. Raw counts are the accurate value. mV is approximate: it's derived from the internal 1.2 V typical reference, with no calibration, so expect a few % error. Keep the source impedance ≤ ~27 kΩ, or put 10–100 nF from the pin to GND.

---

## 4. Quick start

1. **Open the UART** at 115200 8N1 and wait ~50 ms after powering the expander (`IOX_BOOT_READY_MS`).
2. **Check the link:** send `GET_INFO`. Refuse to continue if `proto_major != IOX_PROTO_MAJOR`.
3. **Configure the channels:** `SET_MODE` / `SET_MODE_MASK`, then `SET_DIG_EVENT` for inputs that should send events.
4. **Run:** `WRITE` / `WRITE_MASK` to drive outputs; handle `PIN_CHANGE` events; send `HEARTBEAT` periodically (e.g. every 100 ms).
5. **Handle reboots:** on a `BOOTED` event, go back to step 3.

With the helpers (`plat_*` are your UART and timer functions):

```c
#include "iox_host.h"

uint8_t wire[IOX_WIRE_MAX], n;
uint8_t seq = 0;

/* CH9..CH12 outputs, starting low */
n = iox_req_set_mode_mask(wire, seq, 0x1E00, IOX_TYPE_OUTPUT, IOX_OUT_LOW);
plat_uart_write(wire, n);          /* then wait for the response with (seq, IOX_CMD_SET_MODE_MASK) */
seq++;

/* CH10 high */
n = iox_req_write(wire, seq, IOX_CH10_PA6, 1);
plat_uart_write(wire, n);
seq++;

/* receive side: feed every byte */
static iox_rx_t rx;                /* iox_rx_init(&rx) once */
iox_frame_t f;
if (iox_rx_feed(&rx, byte, &f)) {
    if (f.type == IOX_FT_EVENT)  { /* iox_dec_booted / iox_dec_pin_change / iox_dec_fault */ }
    else if (iox_is_response_to(&f, my_seq, my_id)) { int st = iox_rsp_status(&f); }
}
```

`iox_host_example.c` wraps this into a complete driver: `iox_transact()` handles send, wait, retry and events.

---

## 5. Frame format

Every frame in both directions:

```
55 AA  ver  40  lenH lenL | type  seq  id  payload… | chk
└ KU header (6 bytes) ──┘ └ len bytes ─────────────┘ └ 1 byte
```

| Field | Value |
|---|---|
| `55 AA` | Start marker |
| `ver` | **`00` host → expander**, **`03` expander → host**. The expander ignores frames with any other `ver`. |
| `40` | KU command. All expander traffic uses `0x40`. |
| `lenH lenL` | `len` = 3 + payload bytes, **big-endian**. `lenH` is always 0; `len` is 3–67. |
| `type` | `01` command · `02` response · `03` event |
| `seq` | Commands: host counter. Responses: echoes the command's seq. Events: expander's own counter. |
| `id` | Command ID (`01`–`7F`); a response echoes it. Event IDs are `80`–`FF`. |
| payload | 0–64 bytes. **Multi-byte fields are little-endian.** Responses always start with a status byte. |
| `chk` | `(sum of every byte from 55 up to the byte before chk) & 0xFF` |

Wire length = 10 + payload bytes.

**Example: `WRITE` CH10 high, seq 3, and its response:**
```
host → 55 AA 00 40 00 05 | 01 03 20 | 0A 01 | 73
                           │  │  │    │  └ level 1
                           │  │  │    └ ch 10
                           │  │  └ id WRITE (0x20)
                           │  └ seq 3
                           └ command
MM32 → 55 AA 03 40 00 04 | 02 03 20 | 00 | 6B
                           response, seq 3, WRITE, status OK
```

**Receiving:** the expander only ever sends `ver 03`, KU command `0x40` frames. `iox_rx_feed()` recovers from noise the same way the expander does: if any check fails, it drops one byte and searches again, so a `0x55` inside a payload never breaks framing. If your firmware already runs a KU parser for other traffic, route its `0x40` frames to `iox_decode_ku()` instead.

---

## 6. Link rules

The expander relies on these. Breaking them gives confusing symptoms.

1. **One command at a time.** Wait for its response (or a timeout) before sending the next.
2. **A new seq for every new command.** Increment by 1 and wrap at 255.
3. **Retry with the same seq and id,** and resend the same bytes. The expander remembers its **last** response. If a command arrives with the same `seq` **and** `id` as the previous one, the expander resends that response **without running the command again**. That makes retries safe: a toggle can't happen twice.
   - The flip side: if you send a *new* command with the same seq and id as the previous command, it is **not executed**. Always increment seq.
4. **Timeout and retry policy:** wait `IOX_RSP_TIMEOUT_MS` (50 ms) per attempt, and retry up to `IOX_RETRIES` (3) times. The expander normally answers in under 3 ms. The 50 ms covers host-side scheduling delays.
5. **Send each frame in one burst.** If the gap between bytes of a frame exceeds **20 ms**, the expander discards what it has so far.
6. **Expect events at any time,** including between your command and its response. Handle events while you wait, and match a response by `(type == response, seq, id)`. Ignore responses that don't match: they're late answers to an earlier attempt.
7. **Frames never overlap.** The expander sends whole frames only. It sends a response before any queued event, and at most one event per main-loop pass.
8. **If all retries fail:** wait > 20 ms, then send `PING` with a new seq. If PING also fails, check power and wiring. If NRST is wired to the host, pulse it. The expander's internal watchdog also recovers it from a firmware hang within about 1–2 s, and it then sends `BOOTED`.

---

## 7. Command reference

The response payload always starts with **status** (section 10). The *Response* column lists the fields that follow status when status is OK. **Every error response is exactly 1 byte: the status.** A wrong request length always returns `BAD_LEN` (2).

### System

| ID | Command | Request | Response | Errors |
|---|---|---|---|---|
| `01` | **PING** | 0–60 bytes, anything | Echo of the request bytes | 2 if > 60 bytes |
| `02` | **GET_INFO** | — | `fw_major u8, fw_minor u8, proto_major u8, proto_minor u8, uid[12], ch_count u8 (13), adc_mask u16 (0x003F), framing u8 (4)` (20 B) | — |
| `03` | **GET_STATUS** | — | `uptime_ms u32, vdd_mv u16, reset_cause u8, flags u8, uart_errors u16, event_drops u16` (12 B) | — |
| `04` | **RESET** | `magic u32` = `0x52535421` (bytes `21 54 53 52`) | — (the response is sent in full, then the expander resets, then `BOOTED`) | 5 wrong magic |
| `05` | *(removed)* | — | — | always 1 |
| `06` | **SET_FRAMING** | `framing u8` | — | 5 unless `4` (KU, the only framing built) |
| `07` | **HEARTBEAT** | — | See section 9 | — |
| `08` | **SET_HB_CONTENT** | `mask u8` (`IOX_HB_*`) | — | 5 if bit 6 or 7 set |

Notes:
- **GET_STATUS `flags` does not clear the flags.** Only the heartbeat FLAGS block clears them.
- **`vdd_mv` starts at 3300** (a placeholder) until the first internal measurement, a few ms after boot.
- **`uart_errors` and `event_drops` are running totals** since boot.

### Configuration

| ID | Command | Request | Response | Errors (checked in this order) |
|---|---|---|---|---|
| `10` | **SET_MODE** | `ch u8, type u8, param u8` | — | 3 ch ≥ 13 · 5 unknown type · 4 ADC on CH6–CH12 · 5 bad param |
| `11` | **SET_MODE_MASK** | `mask u16, type u8, param u8` | — | 3 mask bits 13–15 · then as SET_MODE for each channel. **All or nothing:** on any error, no channel changes. |
| `12` | **GET_CONFIG** | `ch u8` | `ch, type, param, edge, debounce_ms` (5 × u8) | 3 |
| `13` | **SET_DIG_EVENT** | `ch u8, edge u8, debounce_ms u8` | — | 3 ch · 5 edge > 3 · 4 channel is not INPUT |

Behaviour to know:
- **Leaving INPUT clears events.** Switching a channel to OUTPUT or ADC resets its edge to NONE. If you switch it back to INPUT, send `SET_DIG_EVENT` again.
- **Switching to INPUT doesn't report a change.** The channel takes on its current level at the next 1 ms scan without generating an event.
- **`edge`:** 0 none · 1 rising · 2 falling · 3 both.
- **`debounce_ms`:** 0–255. A new level is accepted after it has been stable for that many 1 ms scans (0 counts as 1).
- **SET_DIG_EVENT only controls `PIN_CHANGE` events.** Every debounced input change is still recorded in the heartbeat CHANGED block, whatever the edge setting.

### I/O

| ID | Command | Request | Response | Errors (checked in this order) |
|---|---|---|---|---|
| `20` | **WRITE** | `ch u8, level u8` | — | 3 ch · 4 not OUTPUT · 5 level > 1 |
| `21` | **WRITE_MASK** | `mask u16, values u16` | — | 3 mask bits 13–15 · 4 any masked channel is not OUTPUT (**nothing written**) |
| `22` | **READ_ALL** | — | `levels u16, raw u16, out_mask u16` | — |
| `23` | **READ_ADC** | `ch u8, avg u8` | `ch u8, raw u16, mv u16` | 3 ch · 4 not ADC · 5 avg not 0/1/4/16 |
| `24` | **READ_ADC_ALL** | `avg u8` | `count u8`, then `count` × [`ch u8, raw u16, mv u16`], ascending channel order | 5 avg |

Behaviour to know:
- **WRITE_MASK changes only the channels in `mask`.** Each one gets its bit from `values`, and channels outside the mask are untouched. All channels on the same port switch at the same instant; PA and PB change within ~50 ns of each other.
- **READ_ALL fields:**
  - `levels`: debounced level of the INPUT channels, plus the commanded level of the OUTPUT channels.
  - `raw`: the pin state right now, for every channel. ADC channels read meaningless values here.
  - `out_mask`: channels currently set to OUTPUT.
- **`avg` choices:**
  - `avg = 0` returns the latest background sample instantly. The expander samples its ADC channels in turn, one conversion per ms (each channel refreshes every *(ADC channel count + 1)* ms). The value is 0 until the first sample.
  - `avg = 1/4/16` runs fresh conversions of about 3.4 µs each before replying.

### Link

| ID | Command | Request | Response | Errors |
|---|---|---|---|---|
| `30` | **SET_LINK_WDT** | `timeout_ms u16, force_mask u16, force_levels u16` | — | 3 bits 13–15 set in `force_mask` or `force_levels` |

See section 11.

---

## 8. Events (expander → host)

Events aren't acknowledged. They have their own `seq` counter, which starts at 0 at every boot and goes up by 1 per event. The expander queues up to 15 events.

| ID | Event | Payload | When |
|---|---|---|---|
| `80` | **BOOTED** | `reset_cause u8, fw_major u8, fw_minor u8, proto_major u8, proto_minor u8, framing u8` | Once after every reset. **All configuration is gone.** Re-apply it. |
| `81` | **PIN_CHANGE** | `changed u16, levels u16, t_ms u32` | A debounced input change matched that channel's edge setting. Changes in the same 1 ms scan are batched into one event. `levels` is the full level mask after the change. `t_ms` is expander uptime. |
| `82` | **FAULT** | `code u8, detail u16` | See below |

| FAULT code | Meaning | detail | What the host should do |
|---|---|---|---|
| 1 `EVT_OVF` | Event queue overflowed | Number of events dropped | Resync with `READ_ALL` or the next heartbeat |
| 2 `UART` | Expander saw a UART overrun, framing error, or full receive buffer | Running error count | Check the baud rate and wiring. Sent at most every 100 ms. |
| 3 `LINK_WDT` | Link watchdog tripped | Outputs that were actually forced | You were silent too long. See section 11. |

`reset_cause` bits (several can be set at once):

| Bit | Meaning |
|---|---|
| `0x01` | Power-on or brown-out |
| `0x02` | NRST pin |
| `0x04` | Expander watchdog: its firmware hung, which is worth logging |
| `0x08` | Window watchdog |
| `0x10` | `RESET` command |

A normal power-up usually reports `0x03`.

**Don't rely on catching BOOTED at startup.** If the host boots after the expander, it misses the event. Always configure after the first successful `GET_INFO`, and treat later BOOTED events as a sign the expander has rebooted.

---

## 9. Heartbeat

Send `HEARTBEAT` periodically (100 ms is a good default). The response is a snapshot of the expander's state, so lost events never leave you out of sync.

**Response:** `status u8, mask u8, blocks…`. Only the blocks whose bit is set in `mask` are included, in bit order. `mask` is echoed, so you can always parse the response, even right after an expander reboot has restored the default mask.

| Bit | Block | Size | Contents |
|---|---|---|---|
| `0x01` | FLAGS | 1 | `0x01` event overflow · `0x02` link watchdog tripped · `0x04` UART errors. **Cleared when sent.** |
| `0x02` | UPTIME | 4 | ms since boot. If it goes backwards, the expander rebooted. |
| `0x04` | LEVELS | 2 | Same as READ_ALL `levels` |
| `0x08` | CHANGED | 2 | INPUT channels that changed since the last CHANGED block, regardless of edge settings. **Cleared when sent.** Catches pulses shorter than your heartbeat period. |
| `0x10` | OUT_MASK | 2 | Channels set to OUTPUT. Handy for detecting lost configuration. |
| `0x20` | ADC | 1 + 2n | `adc_mask u8`, then `raw u16` for each set bit, in ascending channel order |

The default mask after boot is `0x3F` (all blocks). That's 14 bytes of payload with no ADC channels, or 26 bytes with all six. `SET_HB_CONTENT` changes the mask. It resets to `0x3F` after an expander reboot.

`iox_dec_heartbeat()` decodes any mask into `iox_heartbeat_t`.

---

## 10. Status codes

| Code | Name | Typical cause |
|---|---|---|
| 0 | OK | |
| 1 | UNKNOWN_CMD | Wrong ID, or `0x05` (removed) |
| 2 | BAD_LEN | Request payload length doesn't match the command (section 7) |
| 3 | BAD_CH | Channel ≥ 13, or a mask with bits 13–15 set |
| 4 | BAD_TYPE | WRITE to a channel that isn't OUTPUT, ADC on CH6–CH12, SET_DIG_EVENT on a channel that isn't INPUT, READ_ADC on a channel that isn't ADC |
| 5 | BAD_PARAM | Out-of-range type, param, level, edge, avg, or magic |
| 6 | BUSY | Reserved; firmware 1.0 never returns it |

A frame with a bad checksum or bad length gets **no response at all**. It's dropped silently, and your timeout and retry handle it.

---

## 11. Link watchdog (fail-safe outputs)

This protects outputs if the host crashes or the cable is unplugged. It's **off at boot**.

```
SET_LINK_WDT  timeout_ms = 300, force_mask = 0x1E00, force_levels = 0x0000
→ if the expander receives no valid frame for 300 ms, CH9–CH12 are driven LOW
```

- **Keeping it alive:** any valid expander frame from the host resets the timer. Your heartbeat does this; use a timeout of about 3× the heartbeat period. KU frames for other KU commands, and frames with bad checksums, **don't** count.
- **On timeout:**
  - OUTPUT channels in `force_mask` go to their bit in `force_levels`; other outputs keep their level.
  - Flag `0x02` is set.
  - `FAULT(3)` is queued, with detail = the outputs actually forced.
- **It trips once.** The next valid frame re-arms it. **The forced levels stay** until you write the outputs again.
- `timeout_ms = 0` turns it off. An expander reboot also turns it off; include it in your re-configuration after BOOTED.

---

## 12. Recovering from an expander reboot

The expander can reboot because of power loss, NRST, its internal watchdog, or your `RESET`. Afterwards every channel is a floating input with no events, the heartbeat mask is `0x3F`, and the link watchdog is off.

You'll notice in one or more of these ways:
1. A **`BOOTED` event** arrives (the normal case).
2. The heartbeat **UPTIME goes backwards**.
3. The heartbeat **OUT_MASK** no longer matches what you configured.

In every case, re-run your configuration: modes, events, heartbeat mask, link watchdog, and the output levels you want.

---

## 13. More byte examples

All generated with the encoder in `tools/iox_test.py`, which has been run against the real expander.

| What | Bytes |
|---|---|
| GET_INFO, seq 1 | `55 AA 00 40 00 03 01 01 02 46` |
| SET_MODE CH9 → OUTPUT low, seq 2 | `55 AA 00 40 00 06 01 02 10 09 01 00 62` |
| WRITE_MASK CH9–CH12: CH9, CH10 high, CH11, CH12 low, seq 4 | `55 AA 00 40 00 07 01 04 21 00 1E 00 06 90` |
| HEARTBEAT, seq 5 | `55 AA 00 40 00 03 01 05 07 4F` |
| HEARTBEAT response (mask 3F, flags 0, uptime 1234, levels 0x0640, changed 0, out_mask 0x1E00, no ADC) | `55 AA 03 40 00 11 02 05 07 00 3F 00 D2 04 00 00 40 06 00 00 00 1E 00 DA 14` |
| RESET, seq 6 | `55 AA 00 40 00 07 01 06 04 21 54 53 52 6B` |
| Error response: WRITE to an input → BAD_TYPE | `55 AA 03 40 00 04 02 07 20 04 73` |
| BOOTED event (cause 0x03, fw 1.0, proto 1.0, KU) | `55 AA 03 40 00 09 03 00 80 03 01 00 01 00 04 D7` |
| PIN_CHANGE event (CH6 changed, levels 0x0600, t = 5021 ms) | `55 AA 03 40 00 0B 03 01 81 40 00 00 06 9D 13 00 00 C8` |

---

## 14. Limits and things that aren't supported

- **No persistence.** Configuration lives in RAM only. IDs `0x70–0x7F` and events `0x84–0x8F` are reserved for it.
- **No firmware update over UART.** `0x05` was removed; use SWD.
- **KU framing only.** `SET_FRAMING` accepts only `4`.
- **No open-drain output, no PWM, and no ADC thresholds or analog events.**
- **At most one event per expander main-loop pass,** and 15 queued. Sustained input chatter faster than the link can carry turns into `FAULT(1)` plus the CHANGED latch.
- **mV readings are approximate** (section 3.4). Use raw counts when accuracy matters.

---

## 15. Bench testing without the host firmware

With a USB-UART adapter on PA12/PA3:

```
pip install pyserial
cd IOX_F0020/tools
python iox_test.py COM5        # full protocol test, prints PASS/FAIL per check
python iox_esp_sim.py COM5     # interactive: "13 1", "12,13 h", "all 0", "read", "status"
```

These are useful for checking a board before your host firmware exists, and as a reference when your host behaves differently from what you expect.
