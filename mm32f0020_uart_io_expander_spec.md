# MM32F0020 UART I/O Expander — Design Specification (Draft)

**Status:** Phase 1 firmware written and building (KU framing, RAM only); not yet bench-tested. Persistence (EEPROM) is parked; the ASCII command grammar is not yet designed. Items marked *(proposed)* are defaults filled in but not explicitly confirmed.

---

## 1. Purpose and scope

An MM32F0020 acts as a **UART-controlled, reconfigurable GPIO/ADC expander** for an ESP32 master. The ESP32 configures each channel at runtime as a digital input, digital output, or ADC input, then reads and writes it over a framed protocol. The MM32 reports input changes as asynchronous events and returns a state snapshot on every heartbeat.

**Deliverable:** MM32 firmware only — C using the MindMotion SPL for peripheral setup, with direct register access in ISRs and hot paths, in a Keil MDK project that uses the MindMotion MM32F0020 DFP — plus a short protocol reference. The ESP32 driver is out of scope.

**Phase 1 (implemented in `IOX_F0020/`):** RAM-only configuration and 8-bit additive checksum instead of CRC-16. Protocol reference: `IOX_F0020/PROTOCOL.md`.
- **Framing:** KU framing only (§5.3). IOX frames travel inside the Kiot/Tuya `55 AA` frame as KU command `0x40`.
- **UART driver:** all UART handling goes through the shared `SYSTEM/UART/uart.c`. It gained a binary-safe **ring mode** (`UART1_RX_RING_ENABLE`): the ISR stores every byte in a ring buffer, read with `uart1_rx_read()`, and counts line errors in `uart1_rx_errors()`.
- **Existing products** keep the `'\n'` line mode (`UART1_RX_INTERRUPT_ENABLE`). The two modes are mutually exclusive.
- **Alternative envelope:** a HEX-line version that used the unmodified line mode is kept in git stash "iox phase1: hex-line framing".

---

## 2. Hardware

### 2.1 Device and clocking

| Item | Decision |
|---|---|
| Part | MM32F0020B1T, TSSOP20 |
| Supply | 3.3 V, same rail as the ESP32 (the ADC reference is VDDA = VDD) |
| Clock | Internal 8 MHz HSI → PLL → 48 MHz. No crystal. |
| Clock accuracy | ±2.5 % worst case over −40…105 °C — **accepted** |
| Flash | 1 wait state; prefetch enabled before the clock switch |
| UART | UART1, 115200 baud, 8N1 |

### 2.2 Reserved pins

| Pin (TSSOP) | Function | Notes |
|---|---|---|
| PA12 (2) | UART1_TX (AF1) | → ESP32 RX |
| PA3 (3) | UART1_RX (AF1) | ← ESP32 TX |
| PA10 (4) | NRST | ESP32 GPIO, open-drain, 0.1 µF to GND |
| PA14 (17) | SWCLK | Kept for debugging and flashing; not connected to the ESP32 (no BOOT0 control) |
| PA13 (18) | SWDIO | Kept for debugging |
| VDD (9), VSS (7) | Power | 5 × 100 nF + 4.7 µF on VDD, per the datasheet power scheme |

### 2.3 Channel map

Bit *n* of every 16-bit channel mask in the protocol is CH*n*. Bits 13–15 are always 0.

| CH | Pin (TSSOP) | ADC input | Capability |
|---|---|---|---|
| 0 | PB1 (6) | ch0 | INPUT / OUTPUT / ADC |
| 1 | PB0 (5) | ch1 | INPUT / OUTPUT / ADC |
| 2 | PA11 (1) | ch4 | INPUT / OUTPUT / ADC |
| 3 | PA2 (20) | ch5 | INPUT / OUTPUT / ADC |
| 4 | PA15 (19) | ch6 | INPUT / OUTPUT / ADC |
| 5 | PA7 (14) | ch7 | INPUT / OUTPUT / ADC |
| 6 | PA0 (10) | — | INPUT / OUTPUT |
| 7 | PA1 (8) | — | INPUT / OUTPUT |
| 8 | PA4 (11) | — | INPUT / OUTPUT |
| 9 | PA5 (12) | — | INPUT / OUTPUT |
| 10 | PA6 (13) | — | INPUT / OUTPUT |
| 11 | PA8 (15) | — | INPUT / OUTPUT |
| 12 | PA9 (16) | — | INPUT / OUTPUT |

ADC-capable mask: `0x003F`.

### 2.4 Electrical constraints (datasheet)

- Pin voltage must stay within VSS − 0.3 V … VDD + 0.3 V. The I/O is **not 5 V tolerant**.
- Up to ±20 mA per pin; 60 mA total through VDD and VSS.
- Internal pull-up and pull-down resistors: 50–75 kΩ (60 kΩ typical).
- **Board rule:** every channel that drives a load needs an external pull resistor (10–100 kΩ) to the load's safe level, because every channel is a floating input until the ESP32 configures it.

---

## 3. Channel model

### 3.1 Channel types

| type | Name | param | Allowed on |
|---|---|---|---|
| `0` | INPUT | `0` floating · `1` pull-up · `2` pull-down | all channels |
| `1` | OUTPUT (push-pull only) | `0` low · `1` high (starting level) | all channels |
| `2` | ADC | reserved, send `0` | CH0–CH5 only (status 4 otherwise) |

Open-drain output was deliberately dropped.

### 3.2 Boot default

Every channel starts as **INPUT / floating** with edge detection set to **none**. Configuration is RAM-only, so the same defaults apply after every reset.

### 3.3 Digital input processing

- A 1 ms SysTick interrupt reads both port input registers.
- Per-channel **debounce** of 0–255 ms (one scan per ms): a new level counts only once it has been stable that long.
- **Edge detection:** none, rising, falling, or both. A matching edge queues a PIN_CHANGE event.
- **Latched CHANGED mask:** any debounced change on an INPUT channel sets its bit. The mask clears **only when the heartbeat's CHANGED block is sent**, so a pulse that comes and goes between heartbeats is never lost.

### 3.4 ADC processing

- Readings only — no thresholds and no analog events.
- All ADC-type channels are sampled **in the background, round-robin**, and the latest raw 12-bit value (0–4095) is cached. Sampling rate *(proposed)*: one channel per ms.
- `READ_ADC` with `avg = 0` returns the cached value immediately; `avg` = 1, 4 or 16 runs a fresh averaged conversion.
- mV values are derived from the internal 1.2 V reference, measured to get VDD. The datasheet gives only a typical value and no calibration constant, so **mV is approximate**. Raw counts are the primary unit.

---

## 4. Firmware architecture

### 4.1 Execution model

Bare-metal super-loop with interrupts. No RTOS (2 KB SRAM).

```
UART RX ISR ──► RX ring (128 B) ──► [main] active parser ──► len/chk check ──► dispatcher
                                                                                │
SysTick ISR (1 ms) ──► scan/debounce ──► event queue (16) ──┐                    ▼
                                                            ├──► [main] TX builder (active envelope) ──► TX ring (256 B) ──► UART TX ISR
[main] ADC round-robin ──► ADC cache ───────────────────────┘
```

| Priority | Source | Role |
|---|---|---|
| 0 | UART1 RX/TX interrupt | Never lose a byte (one byte every 87 µs at 115200) |
| 1 | SysTick (1 ms) | Scan, debounce, edge detection, CHANGED latch (~6 µs per tick) |
| — | Main loop | Parse, dispatch, respond, ADC sampling, event flush, link watchdog, IWDG feed |

**Main loop order each pass:**

1. Parse received bytes.
2. Execute a complete command, if one has arrived.
3. Send any pending response (responses always go before events).
4. Run the next ADC conversion.
5. Flush events if the transmit ring has room.
6. Check the link watchdog.
7. Feed the IWDG.

Queues are single-producer, single-consumer lock-free rings.

### 4.2 Modules

| Module | Responsibility |
|---|---|
| `system` | Clock and PLL, flash wait states, SysTick, IWDG, reset-cause capture |
| `uart` | UART1 on PA12/PA3, interrupt-driven RX/TX rings, error counters |
| `checksum` | 8-bit additive checksum |
| `env_cobs` / `env_sync` / `env_ascii` | Envelope parsers and encoders; only one active at a time |
| `proto` | Dispatcher, response builder, duplicate-seq cache, event queue, heartbeat builder |
| `pins` | Channel table, mode application, reads and writes |
| `scan` | Debounce, edges, CHANGED latch |
| `adc` | Round-robin sampling, cache, averaged reads, VDD from the internal reference |
| `linkwdt` | Link watchdog timer and forcing outputs to safe levels |

### 4.3 Resource budget (estimates)

| SRAM use | Bytes |
|---|---|
| Stack | 512 |
| RX ring / TX ring | 160 / 256 |
| RX frame buffer / TX build buffer | 71 / 71 |
| Channel state (13 × ~8 B) | ~104 |
| Event queue (16 × ~10 B) | ~160 |
| Response cache | 71 |
| ADC cache, counters, miscellaneous | ~100 |
| **Total** | **~1.5 KB of 2 KB** |

Flash: roughly 7–10 KB of code out of 32 KB. The last 2 KB of flash is reserved for future persistence.

### 4.4 Safety

- **IWDG** always enabled, fed from the main loop.
- **Link watchdog** off by default (see §7.16).
- Reset cause captured at boot and reported in BOOTED and GET_STATUS.
- `RESET` requires a u32 magic value.

---

## 5. Framing

### 5.1 Layer model

```
Envelope (COBS | Sync | ASCII)
   └── Core frame (binary envelopes only)
          └── Command / response / event payload
```

### 5.2 Core frame (COBS and Sync)

| Offset | Field | Type | Notes |
|---|---|---|---|
| 0 | type | u8 | `01` command · `02` response · `03` event |
| 1 | seq | u8 | Commands: ESP32 counter. Responses: echoed. Events: MM32's own counter. |
| 2 | id | u8 | Commands `0x01–0x7F`; responses echo the command ID; events `0x80–0xFF` |
| 3 | len | u8 | Payload length, 0–64 |
| 4… | payload | len bytes | |
| 4+len | chk | u8 | 8-bit additive checksum over bytes 0 … 3+len |

**Checksum:** `chk = (sum of bytes 0 … 3+len) & 0xFF`. It catches single-byte errors but not reordered bytes or errors that cancel out, which CRC-16 would.

All multi-byte fields are **little-endian**. Largest core frame: 69 bytes.

### 5.3 Envelopes

| | A: COBS | B: Sync | ASCII | **KU (phase 1)** |
|---|---|---|---|---|
| Framing id | 0 | 1 | 2 | 4 |
| On the wire | `COBS(core) 00` | `A5 5A core` | text line + `\n` (`\r\n` accepted) | `55 AA ver 40 lenH lenL type seq id payload chk` |
| Max size on wire | 71 B | 71 B | 80 characters | 74 B |
| Integrity | 8-bit checksum | 8-bit checksum (sync bytes not covered) | none | 8-bit sum over **all** bytes incl. header (KU rule) |
| Resync | at the next `00` | rescan from the byte after `A5` | at the next `\n` | rescan from the byte after `55` |
| seq / duplicate cache | yes | yes | no | yes |

**KU envelope details:**
- **Why KU:** it's the frame of the Kiot ESP framework's KU protocol (`controller_ku.ino`: `KUSendCmd` / `KUSerialprocess`), so the existing ESP-side code can carry expander traffic.
- **Version byte:** `ver` = `00` ESP32 → MCU and `03` MCU → ESP32. This follows the Tuya convention.
- **KU command `0x40`:** all IOX traffic uses it. KU uses `00–08, 0A, 0B, 0E, 0F, 1C`. Using one command keeps IOX IDs from colliding with KU commands such as `06` SetDP and `07` ReportDP.
- **Length:** `len = 3 + payload`, big-endian. The inner IOX `len` byte is dropped.
- **Byte order:** IOX fields inside the payload stay little-endian.
- **Other KU commands:** valid KU frames with another command are ignored by the MM32.

### 5.4 Receive (parser) rules

- **COBS:** accumulate bytes up to `00`, decode, check `len` against the decoded length, then check the checksum.
- **Sync:** hunt for `A5` then `5A`; read 4 header bytes and reject if `len` > 64; read `len` + 1 more bytes and check the checksum. **On any failure, rescan from the byte after the `A5`.**
- **ASCII:** accumulate up to `\n`, tokenise, map onto a command ID. Grammar to be designed.
- **KU:** hunt for `55` then `AA`; read `ver cmd lenH lenL`.
  - Reject the frame if `ver` ≠ `00`, `lenH` ≠ 0, or `len` is outside 3–67.
  - Read `len` + 1 more bytes and check the sum over all preceding bytes.
  - Skip valid frames whose `cmd` ≠ `0x40`.
  - **On any failure, rescan from the byte after the `55`**, so a `0x55` inside a payload never breaks framing.
- **All envelopes:**
  - A frame with a bad length or bad checksum is dropped **silently**; the sender's timeout triggers a retry.
  - An oversized frame is discarded up to the next delimiter.
  - A partial frame is discarded after **20 ms** idle.

### 5.5 Transmit (builder) rules

- **Whole frames only:** a frame is not started unless the entire encoded frame fits in the transmit ring, so frames never interleave on the wire.
- A pending **response always goes before queued events**.
- Events carry their own seq counter. A gap tells the ESP32 it missed one; the next heartbeat reconciles state.

### 5.6 Selecting the framing

- **Boot default:** compile-time `IOX_DEFAULT_FRAMING` (`FRAMING_COBS` / `FRAMING_SYNC` / `FRAMING_ASCII`). BOOTED and any events before the first command use it.
- **Runtime switch:** the `SET_FRAMING` command (§7.6).
- Any reset returns to the compile-time default. A misconfigured link is recovered with an ESP32 NRST pulse.

### 5.7 Link discipline

- **Stop-and-wait:** at most one command outstanding.
- **Duplicate cache:** the MM32 keeps its last response. A command with the same seq as the previous one gets that response resent without re-executing.
- **ESP32 retry policy:**
  1. Time out after 5 ms + 2 × the frame's time on the wire (~10 ms typical); retry with the **same seq**, up to 3 times.
  2. If all retries fail, flush: send a lone `00` (COBS) or wait > 20 ms idle (Sync), then send PING.
  3. If PING fails, pulse NRST.
- **The ESP32 must accept events while waiting for a response** — an event may arrive before the response.

---

## 6. Status codes

`payload[0]` of every response:

| Code | Meaning |
|---|---|
| 0 | OK |
| 1 | Unknown command |
| 2 | Bad length |
| 3 | Bad channel |
| 4 | Operation not valid for this channel's type or capability |
| 5 | Bad parameter |
| 6 | Busy |

---

## 7. Command reference

The response fields listed below follow the status byte.

### 7.1 Command ID table

| ID | Command | Group |
|---|---|---|
| `01` | PING | System |
| `02` | GET_INFO | System |
| `03` | GET_STATUS | System |
| `04` | RESET | System |
| `05` | *(reserved: was ENTER_BOOTLOADER, removed; answers UNKNOWN_CMD)* | System |
| `06` | SET_FRAMING | System |
| `07` | HEARTBEAT | System |
| `08` | SET_HB_CONTENT | System |
| `10` | SET_MODE | Configuration |
| `11` | SET_MODE_MASK | Configuration |
| `12` | GET_CONFIG | Configuration |
| `13` | SET_DIG_EVENT | Configuration |
| `20` | WRITE | I/O |
| `21` | WRITE_MASK | I/O |
| `22` | READ_ALL | I/O |
| `23` | READ_ADC | I/O |
| `24` | READ_ADC_ALL | I/O |
| `30` | SET_LINK_WDT | Link |
| `70–7F` | *reserved: persistence* | — |

### 7.2 `01` PING

- **Request:** any data, 0–60 B.
- **Response:** echoes the data.

### 7.3 `02` GET_INFO

- **Request:** none.
- **Response:** `fw_major:u8, fw_minor:u8, proto_major:u8, proto_minor:u8, uid[12], ch_count:u8 (=13), adc_mask:u16 (=0x003F), framing:u8` (20 B).

### 7.4 `03` GET_STATUS

- **Request:** none.
- **Response:** `uptime_ms:u32, vdd_mv:u16, reset_cause:u8, flags:u8, uart_errors:u16, event_drops:u16` (12 B).
- **reset_cause bits:** 0 power-on/power-down reset · 1 NRST pin · 2 IWDG · 3 WWDG · 4 software.
- **flags:** same bits as the heartbeat FLAGS block (§9.2).

### 7.5 `04` RESET

- **Request:** `magic:u32` = `0x52535421` ("RST!").
- **Behaviour:** respond, drain the transmit ring, then reset.
- `05` ENTER_BOOTLOADER was removed: the ESP32 cannot drive BOOT0, so the MM32 cannot be put into the ROM bootloader from the UART. Firmware is updated over SWD (PA13/PA14).

### 7.6 `06` SET_FRAMING

- **Request:** `framing:u8` — `0` COBS · `1` Sync · `2` ASCII.
- **Response:** status only, sent in the **old** framing.
- **Behaviour:** after the response, drain the transmit ring, discard any partial received frame, and switch both directions.

### 7.7 `07` HEARTBEAT / `08` SET_HB_CONTENT

See §9.

### 7.8 `10` SET_MODE

- **Request:** `ch:u8, type:u8, param:u8` (§3.1).
- **Response:** status only (3, 4 or 5 on error).
- **Side effect:** switching a channel away from INPUT resets its edge setting to none.

### 7.9 `11` SET_MODE_MASK

- **Request:** `mask:u16, type:u8, param:u8`.
- **Response:** status only.
- **Behaviour** *(proposed)*: all-or-nothing. If any channel in the mask cannot take the type (e.g. ADC on CH6–CH12), return status 4 and change nothing.

### 7.10 `12` GET_CONFIG

- **Request:** `ch:u8`.
- **Response:** `ch:u8, type:u8, param:u8, edge:u8, debounce_ms:u8`.

### 7.11 `13` SET_DIG_EVENT

- **Request:** `ch:u8, edge:u8 (0 none · 1 rising · 2 falling · 3 both), debounce_ms:u8`.
- **Response:** status only; status 4 if the channel is not INPUT.

### 7.12 `20` WRITE / `21` WRITE_MASK

- **WRITE request:** `ch:u8, level:u8`.
- **WRITE_MASK request:** `mask:u16, values:u16`.
- **Response:** status only; status 4 if any targeted channel is not OUTPUT (*(proposed)*: WRITE_MASK then writes nothing).
- **Timing:** writes within one port are simultaneous; writes spanning PA and PB land within ~50 ns of each other.

### 7.13 `22` READ_ALL

- **Request:** none.
- **Response:** `levels:u16 (debounced), raw:u16 (instantaneous), out_mask:u16`.

### 7.14 `23` READ_ADC

- **Request:** `ch:u8, avg:u8` — `0` cached · `1`/`4`/`16` fresh averaged conversion.
- **Response:** `ch:u8, raw:u16, mv:u16`; status 4 if the channel is not ADC.

### 7.15 `24` READ_ADC_ALL

- **Request:** `avg:u8`.
- **Response:** `count:u8`, then `count ×` [`ch:u8, raw:u16, mv:u16`], one entry per ADC-type channel.

### 7.16 `30` SET_LINK_WDT

- **Request** *(proposed layout)*: `timeout_ms:u16 (0 = off), force_mask:u16, force_levels:u16`.
- **Response:** status only.
- **Behaviour:**
  - Off by default.
  - Any valid frame from the ESP32 resets the watchdog; the heartbeat is the natural keep-alive (a timeout of ~3 × the heartbeat period is typical).
  - On timeout, OUTPUT channels in `force_mask` are driven to their bit in `force_levels`; all other outputs **hold** their level.
  - A FAULT event is sent and flag bit 1 is set.

---

## 8. Events (MM32 → ESP32)

Events are **fire-and-forget** (no acknowledgements); the heartbeat is the reconciliation path. They use their own seq counter.

| ID | Event | Payload |
|---|---|---|
| `80` | BOOTED | `reset_cause:u8, fw_major:u8, fw_minor:u8, proto_major:u8, proto_minor:u8, framing:u8`. Sent **once** at boot, in the default framing. |
| `81` | PIN_CHANGE | `changed:u16, levels:u16, t_ms:u32`. All channels changing in the same 1 ms tick are batched into one event. |
| `82` | FAULT | `code:u8, detail:u16`. Codes: 1 event queue overflow (detail = number dropped) · 2 UART overrun or framing error (detail = count) · 3 link watchdog tripped |
| `84–8F` | *reserved* | Future SAVE_BEGIN / SAVE_DONE |

After a FAULT with code 1, the ESP32 should resynchronise with `READ_ALL` or the next heartbeat.

---

## 9. Heartbeat

### 9.1 Concept

The ESP32 sends `HEARTBEAT` periodically (period chosen on the ESP32 side; ~100 ms assumed for sizing). The MM32 answers with a snapshot of its current state. The ESP32 does **not** perform explicit reset detection: BOOTED announces resets, and the UPTIME block is available as a backstop.

- **`07` HEARTBEAT request:** none.
- **Response:** `[status:u8][mask:u8][blocks…]`. The mask is always echoed, so the response can be parsed even right after an MM32 reset restores the default mask.
- **`08` SET_HB_CONTENT request:** `mask:u8`. Response is status only.
- **Default mask at boot:** `0x3F` (all blocks).

### 9.2 Blocks

Sent in bit order; only blocks whose bit is set are included.

| Bit | Block | Size | Contents |
|---|---|---|---|
| 0 | FLAGS | 1 B | bit 0 event queue overflow · bit 1 link watchdog tripped · bit 2 UART errors · bits 3–7 reserved. Latched; cleared when sent. |
| 1 | UPTIME | 4 B | ms since boot |
| 2 | LEVELS | 2 B | Debounced level of every channel (inputs and outputs) |
| 3 | CHANGED | 2 B | Latched mask of input channels that changed; cleared only when this block is sent |
| 4 | OUT_MASK | 2 B | Channels currently configured as OUTPUT |
| 5 | ADC | 1 + 2n B | `adc_mask:u8`, then `raw:u16` per set bit in ascending channel order (self-describing) |
| 6–7 | reserved | — | |

OUT_MASK plus the ADC block's mask together describe every channel's type; LEVELS gives their state.

### 9.3 Sizes and link load

| Case | Payload | On the wire (COBS) | Time at 115200 |
|---|---|---|---|
| Fresh boot, mask `0x3F`, no ADC channels | 14 B | 21 B (Sync) | ~1.8 ms |
| Mask `0x3F`, all 6 ADC channels active | 26 B | 33 B (Sync) | ~2.9 ms |

At a 100 ms period the heartbeat uses about 3–4 % of the link.

---

## 10. Timing summary

| Operation | Typical |
|---|---|
| WRITE command + response round trip | ~2 ms |
| Maximum-size frame on the wire | ~6 ms |
| Input edge → PIN_CHANGE on the wire | debounce + ≤ 1 ms + ~1 ms to transmit |
| SysTick scan cost | ~6 µs per 1 ms |
| Link watchdog detection | configured timeout (off by default) |

---

## 11. Parked: persistence (EEPROM emulation)

Not part of the current design. Reserved resources: commands `0x70–0x7F`, events `0x84–0x8F`, and the last 2 KB of flash.

Notes from the earlier discussion, for when this is picked up:

- The MM32F0020 has no real EEPROM. The plan is a wear-levelled log of records (sequence number + CRC) across 2 flash pages, giving roughly 2 million saves over the device's life.
- RAM-only stays the default; persistence becomes an opt-in mode.
- Boot policy set by the ESP32: restore saved config with outputs at safe levels, restore with last output levels, or boot to defaults.
- Auto-save after changes settle (~2 s after the last change), plus a `SAVE_NOW` command.
- Flash write stalls (~7 ms per record write, 4–6 ms per page erase) handled by `SAVE_BEGIN` → ~5 ms pause → write → `SAVE_DONE`, with normal ESP32 retries as backup.
- The PVD blocks flash writes below ~2.9 V. A record with a bad CRC is skipped at boot.

---

## 12. Decision log

| # | Topic | Options considered | Decision |
|---|---|---|---|
| 1 | What "multiplexer" means | Software signal forwarding (crossbar); controller for external analog switches; ADC scanner; hybrid | **UART-controlled reconfigurable GPIO/ADC expander.** The MM32F0020 has no hardware crossbar, so software forwarding would be limited to slow, one-directional signals. |
| 2 | Signal types | Digital, analog, bidirectional buses, mixed | GPIO and ADC; no PWM for now |
| 3 | UART master | PC/terminal, another MCU, test jig | ESP32 |
| 4 | UART pins | Opt 1 PA12/PA3 (keep NRST, 6 ADC) · Opt 2 PA10/PA0 (all 8 ADC, lose NRST) · Opt 3 PA14/PA13 (lose SWD) | **Opt 1**. The ESP32 does not control BOOT0, so there is no field reflashing via the UART bootloader; `ENTER_BOOTLOADER` was removed and flashing is over SWD. |
| 5 | Change detection | EXTI interrupts · 1 ms timer scan | **1 ms timer scan** — EXTI lines are shared by pin number (PA0/PB0, PA1/PB1), and the scan gives debouncing for free |
| 6 | Protocol family | Binary + CRC · ASCII · Modbus RTU | Binary + ASCII; later expanded to three envelopes (#19). Modbus rejected: it cannot push events. |
| 7 | Toolchain / code base / deliverable | Keil, GCC, IAR · register-level vs MindMotion SPL | **Keil MDK**, MindMotion SPL for peripheral setup (reusing the repo's `uart.c` in its new ring mode, `wdg.c`, `gpios.c`), direct registers in ISRs and hot paths, firmware only |
| 8 | Config retention | RAM only · flash with explicit SAVE · wear-levelled EEPROM emulation | **RAM only for now**; wear-levelled persistence parked as an opt-in (§11) |
| 9 | Clock accuracy | Accept HSI · hardware auto-baud · crystal (loses PB0/PB1 ADC) | **Accept HSI at 115200** |
| 10 | Link watchdog | Off · configurable (off or on by default) · always on | **Configurable, off by default**; per-output hold or forced level |
| 11 | Command flow | Stop-and-wait · window of 4 | **Stop-and-wait** |
| 12 | Session handling | BOOTED once · repeated BOOTED · unconfigured flag + commit · ESP32 heartbeat | **ESP32 sends a periodic heartbeat; the MM32 replies with current state.** No explicit reset detection. |
| 13 | BOOTED event | Keep one-shot · drop | **Keep one-shot** |
| 14 | Heartbeat content | Full · compact · configurable mask | **Configurable content mask**, default `0x3F` (all blocks) |
| 15 | Event acknowledgements | Optional acks · always acked · fire-and-forget | **Dropped** — the heartbeat reconciles lost events |
| 16 | Channel configuration | Various mode sets incl. open-drain | INPUT (floating / pull-up / pull-down), OUTPUT (push-pull high/low), ADC. **Open-drain dropped.** |
| 17 | Output commands | WRITE only · + TOGGLE/PULSE | **WRITE + WRITE_MASK** |
| 18 | ADC features | Thresholds + zone events · readings only | **Readings only**, raw counts, background-sampled cache |
| 19 | Frame delimiting | COBS · Sync + length · SLIP | **COBS, Sync and ASCII all supported**; SLIP rejected (variable overhead) |
| 20 | Framing selection | Runtime switch · lock on first valid frame · always parallel | **Runtime `SET_FRAMING`**; boot default set by compile-time `#define` |
| 21 | Header | Typed 4-byte · implied-type 3-byte | **Typed `[type][seq][id][len]`** |
| 22 | Frame integrity | Software CRC-16 · hardware CRC-32 · 8-bit checksum | **8-bit additive checksum** (1 byte, trivial on both sides); CRC-16 dropped |
| 23 | ASCII integrity | None · optional XOR checksum · mandatory CRC | **None** |
| 24 | Boot-default pin state | Pull-down · pull-up · floating | **Floating input**, edge detection off |
| 25 | Receive path | Own ISR in the app · `uart.c` line mode (HEX lines) · `uart.c` new ring mode | **`uart.c` ring mode** (`UART1_RX_RING_ENABLE`); binary-safe and shared with other products |
| 26 | Wire framing | IOX `A5 5A` Sync · KU DP model (SetDP/ReportDP) · KU frame carrying IOX commands | **KU frame, KU cmd `0x40`, IOX commands inside**. It keeps replies, status codes, seq and the snapshot heartbeat, which the DP model lacks, while reusing the ESP framework's KU framing. MCU replies use version `03`. |

---

## 13. Open items

**Still to design**

1. **ASCII command grammar:** keywords, argument formats, response and event line formats (80-character limit, no integrity check).

**To confirm** (proposed defaults are in place)

2. Magic value for `RESET`.
3. All-or-nothing behaviour for `SET_MODE_MASK` and `WRITE_MASK`.
4. `SET_LINK_WDT` payload layout.
5. Background ADC sampling rate.

**To verify in the MM32F0020 user manual before coding**

6. ~~Which pins the UART bootloader uses~~ (no longer needed: bootloader entry removed).
7. Whether the GPIO has atomic set/reset registers (for WRITE_MASK and glitch-free writes).
8. GPIO state during reset (expected floating, matching the boot default).
9. ~~PA14's behaviour as BOOT0 versus SWCLK at reset~~ (no longer needed: PA14 is SWCLK only).
10. ADC register details: channel selection, sampling time, internal-reference channel.

**Implementation environment**

11. Whether the MindMotion DFP is installed in Keil, and which programmer will be used (DAP-Link, J-Link or MM32-LINK).
