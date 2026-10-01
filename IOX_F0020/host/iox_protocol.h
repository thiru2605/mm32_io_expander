/*
 * iox_protocol.h - MM32F0020 UART I/O expander: wire protocol constants.
 *
 * For firmware on the HOST MCU (ESP32 or any other) that controls the
 * expander. Plain C99, no dependencies beyond <stdint.h>. No code, only the
 * contract. Framing/encoding helpers are in iox_host.h.
 *
 * Matches expander firmware 1.0, protocol 1.0 (IOX_F0020/src). Every value
 * here is fixed in the MM32 build; the host cannot change it at runtime.
 * See IOX_HOST_GUIDE.md for how to use them.
 *
 * Byte order: KU length field is big-endian; every IOX field inside the
 * payload (u16/u32) is LITTLE-endian.
 */
#ifndef IOX_PROTOCOL_H
#define IOX_PROTOCOL_H

#include <stdint.h>

/* ------------------------------------------------------------------------ */
/* Versions (reported in BOOTED and GET_INFO)                               */
/* ------------------------------------------------------------------------ */
#define IOX_FW_MAJOR            1
#define IOX_FW_MINOR            0
#define IOX_PROTO_MAJOR         1   /* host must refuse a different major */
#define IOX_PROTO_MINOR         0

/* ------------------------------------------------------------------------ */
/* UART link                                                                 */
/* ------------------------------------------------------------------------ */
#define IOX_BAUD                115200u     /* 8 data bits, no parity, 1 stop */
/* MM32 side: PA12 (TSSOP pin 2) = MM32 TX -> host RX
 *            PA3  (TSSOP pin 3) = MM32 RX <- host TX
 * 3.3 V logic. MM32 pins are NOT 5 V tolerant. */

/* ------------------------------------------------------------------------ */
/* KU envelope: 55 AA ver cmd lenH lenL | type seq id payload | chk          */
/*   len = 3 + payload bytes (big-endian, lenH always 0)                     */
/*   chk = (sum of every byte from 55 up to, not including, chk) & 0xFF      */
/* ------------------------------------------------------------------------ */
#define IOX_KU_H0               0x55
#define IOX_KU_H1               0xAA
#define IOX_KU_VER_HOST         0x00    /* host -> MM32 (MM32 drops any other value) */
#define IOX_KU_VER_MCU          0x03    /* MM32 -> host */
#define IOX_KU_CMD              0x40    /* every IOX frame uses KU command 0x40 */

#define IOX_KU_HDR_LEN          6       /* 55 AA ver cmd lenH lenL */
#define IOX_INNER_HDR_LEN       3       /* type seq id */
#define IOX_MAX_PAYLOAD         64      /* bytes after type/seq/id */
#define IOX_KU_LEN_MIN          IOX_INNER_HDR_LEN
#define IOX_KU_LEN_MAX          (IOX_INNER_HDR_LEN + IOX_MAX_PAYLOAD)          /* 67 */
#define IOX_WIRE_MAX            (IOX_KU_HDR_LEN + IOX_KU_LEN_MAX + 1)          /* 74 */
#define IOX_WIRE_LEN(payload_len) (IOX_KU_HDR_LEN + IOX_INNER_HDR_LEN + (payload_len) + 1)

/* Offsets inside a whole KU frame */
#define IOX_OFS_VER             2
#define IOX_OFS_KUCMD           3
#define IOX_OFS_LENH            4
#define IOX_OFS_LENL            5
#define IOX_OFS_TYPE            6
#define IOX_OFS_SEQ             7
#define IOX_OFS_ID              8
#define IOX_OFS_PAYLOAD         9

/* Frame types (inner byte 'type') */
#define IOX_FT_COMMAND          0x01    /* host -> MM32 */
#define IOX_FT_RESPONSE         0x02    /* MM32 -> host, seq and id echo the command */
#define IOX_FT_EVENT            0x03    /* MM32 -> host, unsolicited, own seq counter */

/* ------------------------------------------------------------------------ */
/* Timing (ms)                                                               */
/* ------------------------------------------------------------------------ */
#define IOX_RX_IDLE_MS          20      /* MM32 drops a partial frame after this gap: send frames in one burst */
#define IOX_RSP_TIMEOUT_MS      50      /* recommended host wait per attempt (MM32 typically answers in < 3 ms) */
#define IOX_RETRIES             3       /* recommended retries, SAME seq */
#define IOX_BOOT_READY_MS       50      /* recommended wait after power-up/NRST before the first command */
#define IOX_IWDG_TIMEOUT_MS     1000    /* MM32 internal watchdog, typ (0.57..2.0 s) */

/* ------------------------------------------------------------------------ */
/* Command ids (host -> MM32)                                                */
/* ------------------------------------------------------------------------ */
#define IOX_CMD_PING            0x01    /* req: 0..60 B any    rsp: echo                         */
#define IOX_CMD_GET_INFO        0x02    /* req: -              rsp: iox_info (20 B)              */
#define IOX_CMD_GET_STATUS      0x03    /* req: -              rsp: iox_status (12 B)            */
#define IOX_CMD_RESET           0x04    /* req: magic u32      rsp: -, then MM32 resets          */
/*      0x05                             removed (was ENTER_BOOTLOADER) -> IOX_ST_UNKNOWN_CMD     */
#define IOX_CMD_SET_FRAMING     0x06    /* req: framing u8     rsp: -  (only IOX_FRAMING_KU)     */
#define IOX_CMD_HEARTBEAT       0x07    /* req: -              rsp: mask u8 + blocks             */
#define IOX_CMD_SET_HB_CONTENT  0x08    /* req: mask u8        rsp: -                            */
#define IOX_CMD_SET_MODE        0x10    /* req: ch type param  rsp: -                            */
#define IOX_CMD_SET_MODE_MASK   0x11    /* req: mask u16 type param   rsp: - (all-or-nothing)    */
#define IOX_CMD_GET_CONFIG      0x12    /* req: ch             rsp: ch type param edge debounce  */
#define IOX_CMD_SET_DIG_EVENT   0x13    /* req: ch edge debounce_ms   rsp: -                     */
#define IOX_CMD_WRITE           0x20    /* req: ch level       rsp: -                            */
#define IOX_CMD_WRITE_MASK      0x21    /* req: mask u16 values u16   rsp: - (all-or-nothing)    */
#define IOX_CMD_READ_ALL        0x22    /* req: -              rsp: levels raw out_mask (u16 x3) */
#define IOX_CMD_READ_ADC        0x23    /* req: ch avg         rsp: ch raw u16 mv u16            */
#define IOX_CMD_READ_ADC_ALL    0x24    /* req: avg            rsp: count + count x (ch raw mv)  */
#define IOX_CMD_SET_LINK_WDT    0x30    /* req: timeout u16 force_mask u16 force_levels u16      */
/*      0x70..0x7F                       reserved (persistence, not implemented)                 */

/* Request payload lengths. The MM32 answers IOX_ST_BAD_LEN to any other length. */
#define IOX_REQ_LEN_GET_INFO        0
#define IOX_REQ_LEN_GET_STATUS      0
#define IOX_REQ_LEN_RESET           4
#define IOX_REQ_LEN_SET_FRAMING     1
#define IOX_REQ_LEN_HEARTBEAT       0
#define IOX_REQ_LEN_SET_HB_CONTENT  1
#define IOX_REQ_LEN_SET_MODE        3
#define IOX_REQ_LEN_SET_MODE_MASK   4
#define IOX_REQ_LEN_GET_CONFIG      1
#define IOX_REQ_LEN_SET_DIG_EVENT   3
#define IOX_REQ_LEN_WRITE           2
#define IOX_REQ_LEN_WRITE_MASK      4
#define IOX_REQ_LEN_READ_ALL        0
#define IOX_REQ_LEN_READ_ADC        2
#define IOX_REQ_LEN_READ_ADC_ALL    1
#define IOX_REQ_LEN_SET_LINK_WDT    6
#define IOX_PING_MAX_DATA           60

/* Response payload lengths on success, INCLUDING the leading status byte.
 * Every error response is exactly 1 byte (the status). */
#define IOX_RSP_LEN_STATUS_ONLY     1
#define IOX_RSP_LEN_GET_INFO        21
#define IOX_RSP_LEN_GET_STATUS      13
#define IOX_RSP_LEN_GET_CONFIG      6
#define IOX_RSP_LEN_READ_ALL        7
#define IOX_RSP_LEN_READ_ADC        6
#define IOX_RSP_LEN_ADC_ENTRY       5       /* ch u8, raw u16, mv u16 */

#define IOX_MAGIC_RESET             0x52535421u     /* "RST!", sent little-endian: 21 54 53 52 */

/* ------------------------------------------------------------------------ */
/* Status codes: payload[0] of every response                                */
/* ------------------------------------------------------------------------ */
#define IOX_ST_OK               0
#define IOX_ST_UNKNOWN_CMD      1
#define IOX_ST_BAD_LEN          2
#define IOX_ST_BAD_CH           3   /* channel >= 13, or mask has bits 13..15 */
#define IOX_ST_BAD_TYPE         4   /* wrong type for this channel (e.g. WRITE to an input, ADC on CH6+) */
#define IOX_ST_BAD_PARAM        5
#define IOX_ST_BUSY             6   /* reserved, not returned by fw 1.0 */

/* ------------------------------------------------------------------------ */
/* Event ids (MM32 -> host)                                                  */
/* ------------------------------------------------------------------------ */
#define IOX_EV_BOOTED           0x80    /* reset_cause fw_maj fw_min proto_maj proto_min framing (6 B) */
#define IOX_EV_PIN_CHANGE       0x81    /* changed u16, levels u16, t_ms u32 (8 B) */
#define IOX_EV_FAULT            0x82    /* code u8, detail u16 (3 B) */
#define IOX_EV_LEN_BOOTED       6
#define IOX_EV_LEN_PIN_CHANGE   8
#define IOX_EV_LEN_FAULT        3

/* FAULT codes (IOX_EV_FAULT payload[0]) */
#define IOX_FAULT_EVT_OVF       1   /* event queue overflowed; detail = events dropped -> resync with READ_ALL */
#define IOX_FAULT_UART          2   /* MM32 saw UART overrun/framing/ring-full; detail = running error count */
#define IOX_FAULT_LINK_WDT      3   /* link watchdog tripped; detail = outputs actually forced */

/* reset_cause bits (BOOTED, GET_STATUS). Several can be set together. */
#define IOX_RST_POR             0x01    /* power-on / brown-out */
#define IOX_RST_PIN             0x02    /* NRST pin */
#define IOX_RST_IWDG            0x04    /* MM32 internal watchdog: firmware hung */
#define IOX_RST_WWDG            0x08
#define IOX_RST_SW              0x10    /* RESET command */

/* flags (heartbeat FLAGS block, GET_STATUS). Latched; heartbeat FLAGS clears them. */
#define IOX_FLAG_EVT_OVF        0x01
#define IOX_FLAG_LINK_WDT       0x02
#define IOX_FLAG_UART_ERR       0x04

/* ------------------------------------------------------------------------ */
/* Heartbeat content mask (SET_HB_CONTENT). Blocks are sent in bit order.    */
/* ------------------------------------------------------------------------ */
#define IOX_HB_FLAGS            0x01    /* 1 B   read-and-clear */
#define IOX_HB_UPTIME           0x02    /* 4 B   ms since boot */
#define IOX_HB_LEVELS           0x04    /* 2 B   debounced inputs | output levels */
#define IOX_HB_CHANGED          0x08    /* 2 B   inputs changed since last CHANGED block, read-and-clear */
#define IOX_HB_OUT_MASK         0x10    /* 2 B   channels configured as OUTPUT */
#define IOX_HB_ADC              0x20    /* 1 + 2n B  adc_mask u8, then raw u16 per set bit, ascending */
#define IOX_HB_ALL              0x3F    /* boot default */
#define IOX_HB_RESERVED         0xC0    /* setting these -> IOX_ST_BAD_PARAM */

/* ------------------------------------------------------------------------ */
/* Channel model                                                             */
/* ------------------------------------------------------------------------ */
#define IOX_CH_COUNT            13
#define IOX_CH_ALL              0x1FFFu     /* valid bits in every channel mask */
#define IOX_ADC_CAPABLE         0x003Fu     /* CH0..CH5 */
#define IOX_CH_BIT(ch)          ((uint16_t)(1u << (ch)))

/* type */
#define IOX_TYPE_INPUT          0
#define IOX_TYPE_OUTPUT         1   /* push-pull only */
#define IOX_TYPE_ADC            2   /* CH0..CH5 only */

/* param for IOX_TYPE_INPUT */
#define IOX_IN_FLOAT            0   /* boot default */
#define IOX_IN_PULLUP           1   /* internal 50..75 kohm */
#define IOX_IN_PULLDOWN         2
/* param for IOX_TYPE_OUTPUT: starting level, applied before the pin drives (no glitch) */
#define IOX_OUT_LOW             0
#define IOX_OUT_HIGH            1
/* param for IOX_TYPE_ADC: always 0 */

/* edge (SET_DIG_EVENT). Setting a non-INPUT type resets edge to NONE. */
#define IOX_EDGE_NONE           0   /* boot default */
#define IOX_EDGE_RISING         1
#define IOX_EDGE_FALLING        2
#define IOX_EDGE_BOTH           3
/* debounce_ms: 0..255; 0 is treated as 1 (one 1 ms scan) */

/* avg (READ_ADC / READ_ADC_ALL) */
#define IOX_AVG_CACHED          0   /* latest background sample, instant */
#define IOX_AVG_1               1   /* fresh conversion(s), ~3.4 us each */
#define IOX_AVG_4               4
#define IOX_AVG_16              16  /* any other value -> IOX_ST_BAD_PARAM */

#define IOX_ADC_MAX_RAW         4095    /* 12-bit */

/* Framing ids (GET_INFO, BOOTED, SET_FRAMING). fw 1.0 builds only KU. */
#define IOX_FRAMING_KU          4

/* ------------------------------------------------------------------------ */
/* Channel map: channel number = bit in every mask.                          */
/* Name = MM32 port pin, comment = TSSOP-20 package pin.                     */
/* ------------------------------------------------------------------------ */
enum iox_channel {
    IOX_CH0_PB1  = 0,   /* pin 6   ADC in 0 */
    IOX_CH1_PB0  = 1,   /* pin 5   ADC in 1 */
    IOX_CH2_PA11 = 2,   /* pin 1   ADC in 4 */
    IOX_CH3_PA2  = 3,   /* pin 20  ADC in 5 */
    IOX_CH4_PA15 = 4,   /* pin 19  ADC in 6 */
    IOX_CH5_PA7  = 5,   /* pin 14  ADC in 7 */
    IOX_CH6_PA0  = 6,   /* pin 10  digital only */
    IOX_CH7_PA1  = 7,   /* pin 8   digital only */
    IOX_CH8_PA4  = 8,   /* pin 11  digital only */
    IOX_CH9_PA5  = 9,   /* pin 12  digital only */
    IOX_CH10_PA6 = 10,  /* pin 13  digital only */
    IOX_CH11_PA8 = 11,  /* pin 15  digital only */
    IOX_CH12_PA9 = 12   /* pin 16  digital only */
};

/* TSSOP-20 package pin -> channel, or -1 if the pin is not a channel
 * (pins 2/3 UART, 4 NRST, 7 VSS, 9 VDD, 17/18 SWD). */
#define IOX_TSSOP_TO_CH(pin) \
    ((pin) == 6  ? 0  : (pin) == 5  ? 1  : (pin) == 1  ? 2  : (pin) == 20 ? 3  : \
     (pin) == 19 ? 4  : (pin) == 14 ? 5  : (pin) == 10 ? 6  : (pin) == 8  ? 7  : \
     (pin) == 11 ? 8  : (pin) == 12 ? 9  : (pin) == 13 ? 10 : (pin) == 15 ? 11 : \
     (pin) == 16 ? 12 : -1)

#endif /* IOX_PROTOCOL_H */
