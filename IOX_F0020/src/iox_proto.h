////////////////////////////////////////////////////////////////////////////////
/// @file    iox_proto.h
/// @brief   Command dispatcher, responses, duplicate cache, events, heartbeat.
////////////////////////////////////////////////////////////////////////////////
#ifndef __IOX_PROTO_H
#define __IOX_PROTO_H

#include "mm32_device.h"

// Command ids (spec 7.1)
#define OP_PING            0x01
#define OP_GET_INFO        0x02
#define OP_GET_STATUS      0x03
#define OP_RESET           0x04
                                                                                // 0x05 ENTER_BOOTLOADER: removed (no BOOT0 control)
#define OP_SET_FRAMING     0x06
#define OP_HEARTBEAT       0x07
#define OP_SET_HB_CONTENT  0x08
#define OP_SET_MODE        0x10
#define OP_SET_MODE_MASK   0x11
#define OP_GET_CONFIG      0x12
#define OP_SET_DIG_EVENT   0x13
#define OP_WRITE           0x20
#define OP_WRITE_MASK      0x21
#define OP_READ_ALL        0x22
#define OP_READ_ADC        0x23
#define OP_READ_ADC_ALL    0x24
#define OP_SET_LINK_WDT    0x30

// Status codes (spec 6)
#define ST_OK               0
#define ST_UNKNOWN_CMD      1
#define ST_BAD_LEN          2
#define ST_BAD_CH           3
#define ST_BAD_TYPE         4
#define ST_BAD_PARAM        5
#define ST_BUSY             6

void proto_init(void);                                                          // queues BOOTED
void proto_poll(u32 now);                                                       // receive + execute one command
void proto_flush_event(void);                                                   // send at most one event
void proto_check_faults(u32 now);

#endif
