////////////////////////////////////////////////////////////////////////////////
/// @file    iox_env.h
/// @brief   Envelope interface. Phase 1 builds only the KU envelope
///          (env_ku.c); other framings can be added as further env_ops_t.
////////////////////////////////////////////////////////////////////////////////
#ifndef __IOX_ENV_H
#define __IOX_ENV_H

#include "mm32_device.h"
#include "iox_config.h"

#define FT_COMMAND  0x01
#define FT_RESPONSE 0x02
#define FT_EVENT    0x03

typedef struct {
    u8 type;
    u8 seq;
    u8 id;
    u8 len;
    u8 payload[IOX_MAX_PAYLOAD];
} iox_frame_t;

typedef struct {
    u8   framing;                                                               // FRAMING_xxx
    void (*reset)(void);                                                        // drop any partial frame
    u8   (*poll)(iox_frame_t* out, u32 now);                                    // 1 = valid frame decoded
    u8   (*encode)(const iox_frame_t* f, u8* wire);                             // returns wire length
} env_ops_t;

extern const env_ops_t env_ku_ops;

// 8-bit additive checksum (KU: over every byte from 55 up to the checksum)
static __inline u8 sum8(const u8* p, u16 n)
{
    u8 s = 0;
    while (n--)
        s = (u8)(s + *p++);
    return s;
}

#endif
