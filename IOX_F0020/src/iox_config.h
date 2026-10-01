////////////////////////////////////////////////////////////////////////////////
/// @file    iox_config.h
/// @brief   MM32F0020 UART I/O expander - build-time configuration.
////////////////////////////////////////////////////////////////////////////////
#ifndef __IOX_CONFIG_H
#define __IOX_CONFIG_H

// Versions reported in GET_INFO and BOOTED
#define FW_MAJOR                1
#define FW_MINOR                0
#define PROTO_MAJOR             1
#define PROTO_MINOR             0

// Framing ids (SET_FRAMING / GET_INFO / BOOTED). Only KU is built in phase 1.
#define FRAMING_COBS            0
#define FRAMING_SYNC            1
#define FRAMING_ASCII           2
#define FRAMING_HEX             3
#define FRAMING_KU              4
#define IOX_DEFAULT_FRAMING     FRAMING_KU

// Channels
#define IOX_CH_COUNT            13
#define IOX_CH_ALL              0x1FFFU
#define IOX_ADC_CAPABLE         0x003FU

// KU frame: 55 AA ver | 40 | lenH lenL | type seq id payload | chk
#define IOX_MAX_PAYLOAD         64
#define IOX_KU_H0               0x55
#define IOX_KU_H1               0xAA
#define IOX_KU_VER_RX           0x00                                            // ESP -> MCU
#define IOX_KU_VER_TX           0x03                                            // MCU -> ESP
#define IOX_KU_CMD              0x40                                            // KU command carrying IOX frames
#define IOX_KU_INNER_HDR        3                                               // type seq id
#define IOX_WIRE_MAX            (6 + IOX_KU_INNER_HDR + IOX_MAX_PAYLOAD + 1)    // 74

// Queues and timing
#define IOX_EVT_QUEUE_LEN       16                                              // power of 2
#define IOX_RX_IDLE_MS          20                                              // partial frame discard
#define IOX_FAULT_MIN_GAP_MS    100                                             // UART-error FAULT rate limit

// RESET magic value (u32, little-endian on the wire)
#define IOX_MAGIC_RESET         0x52535421U                                     // "RST!"

// Heartbeat
#define IOX_HB_DEFAULT_MASK     0x3F

// IWDG: LSI 40 kHz typ (20..70 kHz) / 64 * 625 -> 1.0 s typ, 0.57 s min
#define IOX_IWDG_PRESCALER      IWDG_Prescaler_64
#define IOX_IWDG_RELOAD         625

#endif
