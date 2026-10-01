////////////////////////////////////////////////////////////////////////////////
/// @file    env_ku.c
/// @brief   KU envelope (Kiot/Tuya style), IOX frames carried in KU cmd 0x40:
///            55 AA ver | 40 | lenH lenL | type seq id payload | chk
///          ver = 00 from the ESP, 03 from the MCU. len = 3 + payload bytes
///          (big-endian, lenH always 0). chk = sum of all preceding bytes.
/// @note    Bytes from a candidate 55 are kept in a window. When the candidate
///          fails (header, length or checksum) the window is shifted by one
///          byte and re-scanned, i.e. the search restarts at the byte after
///          the 55, so a 0x55 inside a payload never breaks framing. Valid KU
///          frames for other commands are skipped whole. A partial frame idle
///          for 20 ms is discarded.
////////////////////////////////////////////////////////////////////////////////
#define _ENV_KU_C_

#include <string.h>
#include "iox_env.h"
#include "uart.h"                                                               // uart1_rx_read (ring mode)

#define KU_HDR_LEN  6                                                           // 55 AA ver cmd lenH lenL

static u8  s_win[IOX_WIRE_MAX];
static u8  s_n;
static u32 s_last_ms;

static void drop(u8 k)
{
    s_n = (u8)(s_n - k);
    memmove(s_win, s_win + k, s_n);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief  Decide as much of the window as possible.
/// @retval 1 when a valid IOX frame was copied to out and removed from the window.
////////////////////////////////////////////////////////////////////////////////
static u8 scan(iox_frame_t* out)
{
    u8 len, total;

    while (s_n) {
        if (s_win[0] != IOX_KU_H0) { drop(1); continue; }
        if (s_n < 2) return 0;
        if (s_win[1] != IOX_KU_H1) { drop(1); continue; }
        if (s_n < KU_HDR_LEN) return 0;
        len = s_win[5];
        if (s_win[2] != IOX_KU_VER_RX || s_win[4] != 0
            || len < IOX_KU_INNER_HDR || len > IOX_KU_INNER_HDR + IOX_MAX_PAYLOAD) { drop(1); continue; }
        total = (u8)(KU_HDR_LEN + len + 1);
        if (s_n < total) return 0;
        if (sum8(s_win, (u16)(total - 1)) != s_win[total - 1]) { drop(1); continue; }

        if (s_win[3] != IOX_KU_CMD) {                                           // valid KU frame, not for IOX
            drop(total);
            continue;
        }
        out->type = s_win[6];
        out->seq  = s_win[7];
        out->id   = s_win[8];
        out->len  = (u8)(len - IOX_KU_INNER_HDR);
        memcpy(out->payload, &s_win[9], out->len);
        drop(total);
        return 1;
    }
    return 0;
}

static void ku_reset(void)
{
    s_n = 0;
}

static u8 ku_poll(iox_frame_t* out, u32 now)
{
    u8 c;

    if (s_n && (u32)(now - s_last_ms) > IOX_RX_IDLE_MS)
        s_n = 0;

    if (scan(out))
        return 1;

    while (uart1_rx_read(&c)) {
        s_last_ms = now;
        s_win[s_n++] = c;                                                       // scan() keeps s_n < IOX_WIRE_MAX
        if (scan(out))
            return 1;
    }
    return 0;
}

static u8 ku_encode(const iox_frame_t* f, u8* wire)
{
    u8 len = (u8)(IOX_KU_INNER_HDR + f->len);

    wire[0] = IOX_KU_H0;
    wire[1] = IOX_KU_H1;
    wire[2] = IOX_KU_VER_TX;
    wire[3] = IOX_KU_CMD;
    wire[4] = 0;
    wire[5] = len;
    wire[6] = f->type;
    wire[7] = f->seq;
    wire[8] = f->id;
    memcpy(&wire[9], f->payload, f->len);
    wire[KU_HDR_LEN + len] = sum8(wire, (u16)(KU_HDR_LEN + len));
    return (u8)(KU_HDR_LEN + len + 1);
}

const env_ops_t env_ku_ops = {
    FRAMING_KU,
    ku_reset,
    ku_poll,
    ku_encode
};
