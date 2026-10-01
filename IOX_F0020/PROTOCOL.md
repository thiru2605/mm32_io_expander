# MM32F0020 UART I/O Expander — Protocol Reference (phase 1)

Firmware 1.0, protocol 1.0. Full design: [../mm32f0020_uart_io_expander_spec.md](../mm32f0020_uart_io_expander_spec.md).

## Link

- **UART:** UART1, 115200 8N1. MM32 TX = PA12 (pin 2), RX = PA3 (pin 3).
  - The baud rate is set per link (`UART1_BaudRate`). Other KU products run at 9600.
- **Framing:** **KU** (framing id `4`), the Kiot/Tuya `55 AA` frame. All expander traffic travels in KU command **`0x40`**.
- **Configuration is RAM only:** every reset returns to the defaults.
- **Byte order:** the KU length is big-endian; all IOX fields inside the payload are **little-endian**.

## Frame

```
55 AA | ver | 40 | lenH lenL | type | seq | id | payload[n] | chk
```

| Field | Meaning |
|---|---|
| `55 AA` | KU header |
| ver | `00` ESP32 → MCU · `03` MCU → ESP32. The MM32 accepts only `00`. |
| `40` | KU command "IOX"; frames with any other KU command are ignored |
| lenH lenL | `3 + n`, big-endian (n = 0–64, so lenH = 0) |
| type | `01` command · `02` response · `03` event |
| seq | Commands: ESP32 counter. Responses: echoed. Events: MM32's own counter. |
| id | Commands `01–7F`; responses echo the command ID; events `80–FF` |
| chk | Sum of **all** preceding bytes (`55` … last payload byte) & 0xFF, same rule as `KUSendCmd()` |

Examples:

| Message | Frame |
|---|---|
| PING seq 1 "hi" | `55 AA 00 40 00 05 01 01 01 68 69 18` |
| ↳ reply | `55 AA 03 40 00 06 02 01 01 00 68 69 1D` |
| WRITE CH12 = 0, seq 6 | `55 AA 00 40 00 05 01 06 20 0C 00 77` |
| ↳ reply OK | `55 AA 03 40 00 04 02 06 20 00 6E` |
| PIN_CHANGE event | `55 AA 03 40 00 0B 03 00 81 40 00 40 10 3E 30 00 00 CF` |

**Framing rules**
- The largest frame on the wire is 74 bytes.
- **The receiver is length-driven.** A `0x55` or `0xAA` inside a payload is normal data. The ESP32 side must not restart its parser on every `0x55`; the current `KUSerialprocess()` does, and needs fixing.
- **Invalid frames are dropped silently.** That covers a bad header or version, `len` < 3 or > 67, and a bad checksum. After a failure the receiver rescans from the byte after the `55`.
- **Partial frames** are discarded after 20 ms idle.
- **Stop-and-wait:** at most one command outstanding.
  - Retry with the **same seq** after about 10 ms, up to 3 times.
  - A repeated command (same seq **and** id as the previous one) gets the cached response back without being executed again.
- **Events** can arrive before a response. Event seq gaps mean events were missed; the heartbeat reconciles state.

## Status codes (payload[0] of every response)

`0` OK · `1` unknown command · `2` bad length · `3` bad channel · `4` wrong channel type/capability · `5` bad parameter · `6` busy

## Channels

| CH | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Pin | PB1 | PB0 | PA11 | PA2 | PA15 | PA7 | PA0 | PA1 | PA4 | PA5 | PA6 | PA8 | PA9 |
| ADC | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | | | | | | | |

- Types:
  - `0` INPUT, with param `0` floating · `1` pull-up · `2` pull-down
  - `1` OUTPUT push-pull, with param = starting level
  - `2` ADC (CH0–5, param `0`)
- **Boot default:** every channel INPUT floating, edge none, debounce 0.

## Commands

| ID | Command | Request | Response (after status) |
|---|---|---|---|
| 01 | PING | 0–60 B | echo |
| 02 | GET_INFO | — | fw_maj, fw_min, proto_maj, proto_min, uid[12], ch_count=13, adc_mask:u16=0x003F, framing=4 |
| 03 | GET_STATUS | — | uptime_ms:u32, vdd_mv:u16, reset_cause, flags, uart_errors:u16, event_drops:u16 |
| 04 | RESET | magic:u32 = `0x52535421` | — then software reset |
| 05 | ENTER_BOOTLOADER | magic:u32 = `0x424F4F54` | — then software reset (see note) |
| 06 | SET_FRAMING | framing | `4` (KU) → OK; any other value → status 5 (not built) |
| 07 | HEARTBEAT | — | mask, blocks (below) |
| 08 | SET_HB_CONTENT | mask (bits 6–7 must be 0) | — |
| 10 | SET_MODE | ch, type, param | — |
| 11 | SET_MODE_MASK | mask:u16, type, param | — (all-or-nothing) |
| 12 | GET_CONFIG | ch | ch, type, param, edge, debounce_ms |
| 13 | SET_DIG_EVENT | ch, edge (0 none · 1 rise · 2 fall · 3 both), debounce_ms | — (INPUT only) |
| 20 | WRITE | ch, level | — (OUTPUT only) |
| 21 | WRITE_MASK | mask:u16, values:u16 | — (all targets must be OUTPUT, else nothing is written) |
| 22 | READ_ALL | — | levels:u16, raw:u16, out_mask:u16 |
| 23 | READ_ADC | ch, avg (0 cached · 1/4/16 fresh) | ch, raw:u16, mv:u16 |
| 24 | READ_ADC_ALL | avg | count, count × [ch, raw:u16, mv:u16] |
| 30 | SET_LINK_WDT | timeout_ms:u16 (0 = off), force_mask:u16, force_levels:u16 | — |

**Command notes**
- **reset_cause bits:** 0 POR · 1 NRST · 2 IWDG · 3 WWDG · 4 software.
- **flags bits:** 0 event-queue overflow · 1 link watchdog tripped · 2 UART error.
- **Switching a channel to a non-INPUT type** resets its edge setting to none.
- **ENTER_BOOTLOADER:** the ESP32 must drive BOOT0 (PA14) high *before* sending this command. The MM32 then resets.
  - If the ROM bootloader does not start, BOOT0 was not re-sampled on the software reset. Keep BOOT0 high and pulse NRST instead.
- **Link watchdog:**
  - Any valid frame restarts it.
  - On timeout, OUTPUT channels in `force_mask` go to their `force_levels` bit; all other outputs hold their level.
  - It also sends FAULT(3, detail = mask actually forced) and sets flag bit 1.
  - It trips once, then re-arms on the next valid frame.

## Heartbeat blocks (sent in bit order; default mask `0x3F`)

| Bit | Block | Size |
|---|---|---|
| 0 | FLAGS (latched, cleared when sent) | 1 |
| 1 | UPTIME ms | 4 |
| 2 | LEVELS (debounced inputs + output levels) | 2 |
| 3 | CHANGED (latched input changes, cleared when sent) | 2 |
| 4 | OUT_MASK | 2 |
| 5 | ADC: adc_mask:u8, then raw:u16 per set bit, ascending | 1 + 2n |

## Events

| ID | Event | Payload |
|---|---|---|
| 80 | BOOTED | reset_cause, fw_maj, fw_min, proto_maj, proto_min, framing |
| 81 | PIN_CHANGE | changed:u16, levels:u16, t_ms:u32 (all matching edges in one 1 ms tick) |
| 82 | FAULT | code, detail:u16 |

FAULT codes:
- `1`: event queue overflow (detail = number dropped). Resync with READ_ALL or a heartbeat.
- `2`: UART overrun/framing error (detail = cumulative count; at most one per 100 ms).
- `3`: link watchdog tripped.

## Board / electrical notes (datasheet DS_MM32F0020 v1.01)

- **I/O limits:** not 5 V tolerant. ±20 mA per pin, 60 mA total. Internal pulls are 50–75 kΩ.
- **Loads:** every channel boots as a floating input. A channel that drives a load needs an external 10–100 kΩ resistor to the load's safe level.
- **ADC:**
  - External inputs sample for 42.5 cycles, so keep source impedance ≤ ~27 kΩ.
  - `mv` is derived from VREFINT (1.2 V typical, uncalibrated), so treat it as approximate. Raw counts are the primary unit.
  - Accuracy is guaranteed only for VDD ≥ 2.5 V.
- **NRST:**
  - Pulse ≥ 1 ms; pulses under 0.5 µs are filtered.
  - The reset itself takes ~2.5 ms, so expect BOOTED about 3 ms after release.
- **IWDG** is always on (~1 s typical, 0.57 s minimum over temperature).
