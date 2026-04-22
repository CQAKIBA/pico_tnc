/*
Copyright (c) 2021, JN1DFF
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
* Redistributions of source code must retain the above copyright notice, 
  this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright notice, 
  this list of conditions and the following disclaimer in the documentation 
  and/or other materials provided with the distribution.
* Neither the name of the <organization> nor the names of its contributors 
  may be used to endorse or promote products derived from this software 
  without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "class/cdc/cdc_device.h"
#include "pico/sync.h"

#include "usb_output.h"
#include "usb_input.h"
#include "ax25.h"
#include "tnc.h"
#include "tty.h"
#include "flash.h"
#include "receive.h"
#include "beacon.h"
#include "help.h"
#include "unproto.h"
#include "libmona_pico/mona_pico_api.h"
#include "mona_backend_minimal.h"

#define CONVERSE_PORT 0

typedef struct CMD {
    uint8_t *name;
    int len;
    bool (*func)(tty_t *ttyp, uint8_t *buf, int len);
} cmd_t;

enum STATE_CALLSIGN {
    CALL = 0,
    HYPHEN,
    SSID1,
    SSID2,
    SPACE,
    END,
};



static const uint8_t *gps_str[] = {
    "$GPGGA",
    "$GPGLL",
    "$GPRMC",
};

// indicate converse mode
bool converse_mode = false;
// indicate calibrate mode
bool calibrate_mode = false;
uint8_t calibrate_idx = 0;

typedef enum {
    CMD_PENDING_IDLE = 0,
    CMD_PENDING_PRIVKEY_SHOW_CONFIRM,
    CMD_PENDING_PRIVKEY_GEN_COLLECTING,
    CMD_PENDING_SIGN_QSL_WIZARD,
    CMD_PENDING_SIGN_TX_CONFIRM,
    CMD_PENDING_USB_BOOT_CONFIRM,
} cmd_pending_state_t;

static cmd_pending_state_t cmd_pending_state = CMD_PENDING_IDLE;
static tty_t *cmd_pending_ttyp = NULL;

static bool cmd_console_is_idle(tty_t *ttyp)
{
    if (!ttyp) return false;
    if (converse_mode || calibrate_mode) return false;
    if (cmd_pending_state != CMD_PENDING_IDLE) return false;
    if (cmd_pending_ttyp != NULL) return false;
    return true;
}

void cmd_emit_prompt_if_idle(tty_t *ttyp)
{
    if (!cmd_console_is_idle(ttyp)) return;
    tty_write_str(ttyp, "cmd: ");
}

static mona_addr_type_t mona_param_to_addr_type(uint8_t t);
static uint8_t mona_addr_type_to_param(mona_addr_type_t t);
static char const *mona_param_type_name(uint8_t t);
static bool sign_check_prerequisites(tty_t *ttyp, bool print_status, bool print_messages);

typedef struct {
    tty_t *ttyp;
    uint8_t state[32];
    uint8_t pending_secret[32];
    uint32_t event_index;
    uint64_t prev_event_us;
    uint32_t burst_len;
    int remaining;
    bool has_pending_secret;
    mona_addr_type_t active_type;
} privkey_gen_ctx_t;

static privkey_gen_ctx_t privkey_gen_ctx;

typedef struct {
    tty_t *ttyp;
    uint8_t payload[256];
    int payload_len;
} sign_tx_ctx_t;

static sign_tx_ctx_t sign_tx_ctx;

typedef enum {
    USB_BOOT_WAIT_Y = 0,
    USB_BOOT_WAIT_E,
    USB_BOOT_WAIT_S,
    USB_BOOT_WAIT_ENTER,
} usb_boot_confirm_state_t;

typedef struct {
    tty_t *ttyp;
    usb_boot_confirm_state_t state;
    uint32_t deadline_tick;
} usb_boot_confirm_ctx_t;

static usb_boot_confirm_ctx_t usb_boot_confirm_ctx;

typedef struct {
    char qsl[24];
    char rs[16];
    char date[16];
    char time[16];
    char freq[24];
    char mode[16];
    char qth[64];
} qsl_data_t;

typedef struct {
    bool is_set;
    int year;
    int month;
    int day;
    int hour;
    int min;
    int sec;
    uint64_t anchor_us;
} soft_clock_t;

typedef struct {
    tty_t *ttyp;
    int step;
    int input_len;
    char input[80];
    qsl_data_t data;
} sign_qsl_wizard_ctx_t;

static soft_clock_t soft_clock;
static sign_qsl_wizard_ctx_t sign_qsl_wizard_ctx;

static const uint8_t SECP256K1_ORDER[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
    0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48, 0xA0, 0x3B,
    0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x41
};

static void privkey_gen_reset(void)
{
    memset(&privkey_gen_ctx, 0, sizeof(privkey_gen_ctx));
}

static bool secp256k1_secret_is_valid(uint8_t const secret[32])
{
    int i;

    for (i = 0; i < 32; ++i) {
        if (secret[i] != 0) break;
    }
    if (i == 32) return false;

    for (i = 0; i < 32; ++i) {
        if (secret[i] < SECP256K1_ORDER[i]) return true;
        if (secret[i] > SECP256K1_ORDER[i]) return false;
    }
    return false;
}

static bool privkey_gen_mix(uint8_t state32[32], uint8_t const *payload, size_t payload_len)
{
    uint8_t mixbuf[128];
    const mona_crypto_vtable_t *crypto = mona_get_crypto();

    if (!crypto || !crypto->sha256) return false;
    if (32 + payload_len > sizeof(mixbuf)) return false;

    memcpy(mixbuf, state32, 32);
    memcpy(mixbuf + 32, payload, payload_len);
    return crypto->sha256(mixbuf, 32 + payload_len, state32) ? true : false;
}

static void privkey_gen_print_remaining_inline(tty_t *ttyp, int remaining)
{
    char s[48];
    int n = snprintf(s, sizeof(s), "Remaining entropy counter:%5d", remaining);
    if (n > 0) tty_write(ttyp, (uint8_t *)s, n);
}

static void privkey_gen_update_remaining_inline(tty_t *ttyp, int remaining)
{
    char s[16];
    int n = snprintf(s, sizeof(s), "\b\b\b\b\b%5d", remaining);
    if (n > 0) tty_write(ttyp, (uint8_t *)s, n);
}

static int privkey_gen_event_score(uint64_t delta_us, uint32_t burst_len, int ch)
{
    int dec;

    if (burst_len >= 32) dec = 1;
    else if (burst_len >= 12) dec = 2;
    else if (delta_us < 1000) dec = 2;
    else if (delta_us < 7000) dec = 4;
    else if (delta_us < 80000) dec = 7;
    else dec = 10;

    if (ch == '\r' || ch == '\n' || ch == '\t') {
        if (dec > 2) dec = 2;
    }
    return dec;
}

static bool privkey_gen_derive_secret(uint8_t out_secret[32], uint8_t const state32[32])
{
    const mona_crypto_vtable_t *crypto = mona_get_crypto();
    uint8_t buf[48];
    uint32_t round = 0;

    if (!crypto || !crypto->sha256) return false;

    memcpy(buf, state32, 32);
    memcpy(buf + 32, "privkey-gen-seed", 16);

    while (round < 1024) {
        memcpy(buf + 32, &round, sizeof(round));
        if (!crypto->sha256(buf, sizeof(buf), out_secret)) return false;
        if (secp256k1_secret_is_valid(out_secret)) return true;
        memcpy(buf, out_secret, 32);
        round++;
    }
    return false;
}

static bool privkey_gen_prepare_secret(void)
{
    bool ok = privkey_gen_derive_secret(privkey_gen_ctx.pending_secret, privkey_gen_ctx.state);
    if (!ok) return false;
    privkey_gen_ctx.has_pending_secret = true;
    return true;
}

static bool privkey_gen_save_pending_secret(void)
{
    mona_keyslot_t slot;

    if (!privkey_gen_ctx.has_pending_secret) return false;
    if (mona_keyslot_init_from_secret(&slot, privkey_gen_ctx.pending_secret, privkey_gen_ctx.active_type) != MONA_OK) {
        return false;
    }

    memcpy(param.mona_privkey, slot.secret, sizeof(param.mona_privkey));
    param.mona_privkey_valid = slot.valid ? 1 : 0;
    param.mona_privkey_compressed = slot.compressed ? 1 : 0;
    param.mona_active_type = mona_addr_type_to_param(slot.active_type);
    return true;
}

static void privkey_gen_abort(tty_t *ttyp)
{
    cmd_pending_state = CMD_PENDING_IDLE;
    cmd_pending_ttyp = NULL;
    privkey_gen_reset();
    tty_write_str(ttyp, "\r\nAborted by user.\r\n");
    cmd_emit_prompt_if_idle(ttyp);
}

static bool privkey_gen_start(tty_t *ttyp, mona_addr_type_t type)
{
    uint8_t init_buf[32];
    uint64_t now_us = time_us_64();
    uint32_t tick = tnc_time();
    uint32_t rnd = (uint32_t)now_us;

    privkey_gen_reset();
    privkey_gen_ctx.ttyp = ttyp;
    privkey_gen_ctx.active_type = type;
    privkey_gen_ctx.remaining = 1000;

    memset(init_buf, 0, sizeof(init_buf));
    memcpy(init_buf + 0, &now_us, sizeof(now_us));
    memcpy(init_buf + 8, &tick, sizeof(tick));
    memcpy(init_buf + 12, &rnd, sizeof(rnd));
    memcpy(init_buf + 16, &param.txdelay, sizeof(param.txdelay));
    memcpy(init_buf + 20, &param.axdelay, sizeof(param.axdelay));
    memcpy(init_buf + 22, &param.axhang, sizeof(param.axhang));
    if (!privkey_gen_mix(privkey_gen_ctx.state, init_buf, sizeof(init_buf))) {
        privkey_gen_reset();
        return false;
    }

    tty_write_str(ttyp, "Initiating private key generation.\r\n");
    tty_write_str(ttyp, "\r\n");
    tty_write_str(ttyp, "Please mash your keyboard randomly.\r\n");
    tty_write_str(ttyp, "Press [ESC] to abort.\r\n");
    tty_write_str(ttyp, "\r\n");
    tty_write_str(ttyp, "WARNING:\r\n");
    tty_write_str(ttyp, "IF EXECUTED BY MISTAKE, PRESS [ESC] IMMEDIATELY!\r\n");
    tty_write_str(ttyp, "\r\n");
    privkey_gen_print_remaining_inline(ttyp, privkey_gen_ctx.remaining);
    return true;
}

static bool privkey_gen_consume_char(tty_t *ttyp, int ch)
{
    uint8_t ev[48];
    uint64_t now_us;
    uint64_t delta_us;
    int dec;
    int remaining_before;
    bool ok;

    if (!privkey_gen_ctx.ttyp || ttyp != privkey_gen_ctx.ttyp) return false;
    if (ch == '\x1b') {
        privkey_gen_abort(ttyp);
        return true;
    }
    if (ch == '\n') return true;

    now_us = time_us_64();
    if (privkey_gen_ctx.event_index == 0) delta_us = 0;
    else delta_us = now_us - privkey_gen_ctx.prev_event_us;
    privkey_gen_ctx.prev_event_us = now_us;

    if (privkey_gen_ctx.event_index == 0 || delta_us > 12000) privkey_gen_ctx.burst_len = 1;
    else privkey_gen_ctx.burst_len++;

    remaining_before = privkey_gen_ctx.remaining;
    dec = privkey_gen_event_score(delta_us, privkey_gen_ctx.burst_len, ch);
    if (dec < 1) dec = 1;
    privkey_gen_ctx.remaining -= dec;
    if (privkey_gen_ctx.remaining < 0) privkey_gen_ctx.remaining = 0;

    memset(ev, 0, sizeof(ev));
    memcpy(ev + 0, &privkey_gen_ctx.event_index, sizeof(privkey_gen_ctx.event_index));
    ev[4] = (uint8_t)ch;
    memcpy(ev + 8, &now_us, sizeof(now_us));
    memcpy(ev + 16, &delta_us, sizeof(delta_us));
    memcpy(ev + 24, &privkey_gen_ctx.burst_len, sizeof(privkey_gen_ctx.burst_len));
    memcpy(ev + 28, &remaining_before, sizeof(remaining_before));
    memcpy(ev + 32, &dec, sizeof(dec));
    if (!privkey_gen_mix(privkey_gen_ctx.state, ev, sizeof(ev))) return false;

    privkey_gen_ctx.event_index++;
    privkey_gen_update_remaining_inline(ttyp, privkey_gen_ctx.remaining);

    if (privkey_gen_ctx.remaining == 0) {
        ok = privkey_gen_prepare_secret();
        tty_write_str(ttyp, "\r\n");
        if (!ok) {
            privkey_gen_abort(ttyp);
            return true;
        }
        cmd_pending_state = CMD_PENDING_PRIVKEY_SHOW_CONFIRM;
        tty_write_str(ttyp, "Private key generation complete.\r\n");
        tty_write_str(ttyp, "Press [Enter] to save or [ESC] to abort.\r\n");
    }
    return true;
}

static uint8_t *read_call(uint8_t *buf, callsign_t *c)
{
    callsign_t cs;
    int i, j;
    int state = CALL;
    bool error = false;

    cs.call[i] = '\0';
    for (i = 1; i < 6; i++) cs.call[i] = ' ';
    cs.ssid = 0;

    // callsign
    j = 0;
    for (i = 0; buf[i] && state != END; i++) {
        int ch = buf[i];

        switch (state) {

            case CALL:
                if (isalnum(ch)) {
                    cs.call[j++] = toupper(ch);
                    if (j >= 6) state = HYPHEN;
                    break;
                } else if (ch == '-') {
                    state = SSID1;
                    break;
                } else if (ch != ' ') {
                    error = true;
                }
                state = END;
                break;

            case HYPHEN:
                if (ch == '-') {
                    state = SSID1;
                    break;
                }
                if (ch != ' ') {
                    error = true;
                }
                state = END;
                break;

            case SSID1:
                if (isdigit(ch)) {
                    cs.ssid = ch - '0';
                    state = SSID2;
                    break;
                }
                error = true;
                state = END;
                break;

            case SSID2:
                if (isdigit(ch)) {
                    cs.ssid *= 10;
                    cs.ssid += ch - '0';
                    state = SPACE;
                    break;
                }
                /* FALLTHROUGH */

            case SPACE:
                if (ch != ' ') error = true;
                state = END;
        }
    }

    if (cs.ssid > 15) error = true;

    if (error) return NULL;

    memcpy(c, &cs, sizeof(cs));

    return &buf[i];
}

static int callsign2ascii(uint8_t *buf, callsign_t *c)
{
    int i;

    if (!c->call[0]) {
        memcpy(buf, "NOCALL", 7);
        
        return 6;
    }

    for (i = 0; i < 6; i++) {
        int ch = c->call[i];

        if (ch == ' ') break;

        buf[i] = ch;
    }

    if (c->ssid > 0) {
        buf[i++] = '-';

        if (c->ssid > 9) {
            buf[i++] = '1';
            buf[i++] = c->ssid - 10 + '0';
        } else {
            buf[i++] = c->ssid + '0';
        }
    }

    buf[i] = '\0';

    return i;
}

static bool cmd_mycall(tty_t *ttyp, uint8_t *buf, int len)
{
    if (buf && buf[0]) {

        return read_call(buf, &param.mycall) != NULL;

        //usb_write(buf, len);
        //usb_write("\r\n", 2);

    } else {
        uint8_t temp[10];

        tty_write_str(ttyp, "MYCALL ");
        tty_write(ttyp, temp, callsign2ascii(temp, &param.mycall));
        tty_write_str(ttyp, "\r\n");
    }

    return true;
}

static bool cmd_unproto(tty_t *ttyp, uint8_t *buf, int len)
{
    int i;
    uint8_t *p;


    if (buf && buf[0]) {

        p = read_call(buf, &param.unproto[0]);
        if (p == NULL) return false;

        for (i = 1; i < UNPROTO_N; i++) param.unproto[i].call[0] = '\0';

        for (i = 1; *p && i < 4; i++) {

            while (*p == ' ') p++;

            if (toupper(*p) != 'V') return false;
            p++;
            if (*p != ' ') return false;

            while (*p == ' ') p++;

            p = read_call(p, &param.unproto[i]);
            if (p == NULL) return false;
        }

    } else {

        tty_write_str(ttyp, "UNPROTO ");

        for (i = 0; i < 4; i++) {
            uint8_t temp[10];

            if (!param.unproto[i].call[0]) break;

            if (i > 0) tty_write_str(ttyp, " V ");
            tty_write(ttyp, temp, callsign2ascii(temp, &param.unproto[i]));
        }

        tty_write_str(ttyp, "\r\n");
    }

    return true;
}

static bool cmd_btext(tty_t *ttyp, uint8_t *buf, int len)
{
    if (buf && buf[0]) {

        uint8_t *p = buf;
        int i;

        if (buf[0] == '%' && len == 1) {
            param.btext[0] = '\0';
            return true;
        }

        for (i = 0; i < len && i < BTEXT_LEN; i++) {
            param.btext[i] = buf[i];
        }
        param.btext[i] = '\0';

    } else {

        tty_write_str(ttyp, "BTEXT ");
        tty_write_str(ttyp, param.btext);
        tty_write_str(ttyp, "\r\n");
    }

    return true;
}

static bool cmd_beacon(tty_t *ttyp, uint8_t *buf, int len)
{
    if (buf && buf[0]) {

        static uint8_t const every[] = "EVERY";
        uint8_t const *s = every;
        int i = 0;

        if (!strncasecmp(buf, "OFF", 3)) {
            param.beacon = 0;
            return true;
        }

        while (toupper(buf[i]) == *s) {
            i++;
            s++;
        }

        if (!buf[i] || buf[i] != ' ') return false;

        int r, t;
        r = sscanf(&buf[i], "%d", &t);

        if (r != 1 || (t < 0 || t > 60)) return false;

        param.beacon = t;
        beacon_reset();     // beacon timer reset

    } else {

        tty_write_str(ttyp, "BEACON ");

        if (param.beacon > 0) {
            uint8_t temp[4];

            tty_write_str(ttyp, "On EVERY ");
            tty_write(ttyp, temp, sprintf(temp, "%u", param.beacon));
        } else {
            tty_write_str(ttyp, "Off");
        }
        tty_write_str(ttyp, "\r\n");
    }

    return true;
}

static bool cmd_monitor(tty_t *ttyp, uint8_t *buf, int len)
{
    if (buf && buf[0]) {

        if (!strncasecmp(buf, "ALL", 3)) {
            param.mon = MON_ALL;
        } else if (!strncasecmp(buf, "ME", 2)) {
            param.mon = MON_ME;
        } else if (!strncasecmp(buf, "OFF", 3)) {
            param.mon = MON_OFF;
        } else {
            return false;
        }

    } else {

        tty_write_str(ttyp, "MONitor ");
        if (param.mon == MON_ALL) {
            tty_write_str(ttyp, "ALL");
        } else if (param.mon == MON_ME) {
            tty_write_str(ttyp, "ME");
        } else {
            tty_write_str(ttyp, "OFF");
        }
        tty_write_str(ttyp, "\r\n");

    }

    return true;
}

static bool cmd_digipeat(tty_t *ttyp, uint8_t *buf, int len)
{
    if (buf && buf[0]) {

        if (!strncasecmp(buf, "ON", 2)) {
            param.digi = true;
        } else if (!strncasecmp(buf, "OFF", 3)) {
            param.digi = false;
        } else {
            return false;
        }

    } else {

        tty_write_str(ttyp, "DIGIpeater ");
        if (param.digi) {
            tty_write_str(ttyp, "ON");
        } else {
            tty_write_str(ttyp, "OFF");
        }
        tty_write_str(ttyp, "\r\n");
    }

    return true;
}

static bool cmd_myalias(tty_t *ttyp, uint8_t *buf, int len)
{
    if (buf && buf[0]) {

        return read_call(buf, &param.myalias) != NULL;

        //usb_write(buf, len);
        //usb_write("\r\n", 2);

    } else {
        uint8_t call[10];

        tty_write_str(ttyp, "MYALIAS ");
        if (param.myalias.call[0]) tty_write(ttyp, call, callsign2ascii(call, &param.myalias));
        tty_write_str(ttyp, "\r\n");

    }

    return true;
}

static bool cmd_perm(tty_t *ttyp, uint8_t *buf, int len)
{
    //tty_write("PERM\r\n", 6);

    receive_off(); // stop ADC free running

    bool ret = flash_write(&param, sizeof(param));

    receive_on();

    return ret;
}

static bool cmd_echo(tty_t *ttyp, uint8_t *buf, int len)
{
    if (buf && buf[0]) {

        if (!strncasecmp(buf, "ON", 2)) {
            param.echo = 1;
        } else if (!strncasecmp(buf, "OFF", 3)) {
            param.echo = 0;
        } else {
            return false;
        }

     } else {

        tty_write_str(ttyp, "ECHO ");
        if (param.echo) {
            tty_write_str(ttyp, "ON"); 
        } else {
            tty_write_str(ttyp, "OFF");
        }
        tty_write_str(ttyp, "\r\n");
    }

    return true;
}

static bool cmd_gps(tty_t *ttyp, uint8_t *buf, int len)
{
    if (buf && buf[0]) {

        for (int i = 0; i < 3; i++) {
            uint8_t const *str = gps_str[i];
        
            if (!strncasecmp(buf, str, strlen(str))) {
                param.gps = i;
                return true;
            }
        }
        return false;

    } else {

        tty_write_str(ttyp, "GPS ");
        tty_write_str(ttyp, gps_str[param.gps]);
        tty_write_str(ttyp, "\r\n");
    }

    return true;
}

static bool parse_axhang_ms(uint8_t *buf, int *value_ms);

static bool cmd_txdelay(tty_t *ttyp, uint8_t *buf, int len)
{
    if (buf && buf[0]) {
        int t;

        if (!parse_axhang_ms(buf, &t)) return false;

        param.txdelay = t;
        param.axdelay = (uint16_t)((param.txdelay * 2 + 1) / 3);

        // set txdelay for KISS (10ms unit)
        tnc[0].kiss_txdelay = (param.axdelay + 5) / 10;

    } else {
        uint8_t temp[16];

        tty_write_str(ttyp, "TXDELAY ");
        tty_write(ttyp, temp, snprintf(temp, sizeof(temp), "%ums", param.txdelay));
        tty_write_str(ttyp, "\r\n");
    }

    return true;
}

static bool parse_axhang_ms(uint8_t *buf, int *value_ms)
{
    uint8_t *p = buf;
    char *end;
    double value;
    double scaled;

    while (*p == ' ') p++;
    if (!*p) return false;

    value = strtod((char *)p, &end);
    if (end == (char *)p) return false;
    if (value < 0.0) return false;

    while (*end == ' ') end++;

    if (*end == '\0') {
        // backward compatibility: unitless value means 10ms units
        scaled = value * 10.0;
    } else if (!strncasecmp(end, "ms", 2)) {
        end += 2;
        while (*end == ' ') end++;
        if (*end) return false;
        scaled = value;
    } else if (tolower(*end) == 's') {
        end++;
        while (*end == ' ') end++;
        if (*end) return false;
        scaled = value * 1000.0;
    } else {
        return false;
    }

    *value_ms = (int)(scaled + 0.5);

    if (*value_ms < 0 || *value_ms > 1000) return false;

    return true;
}

static bool cmd_axdelay(tty_t *ttyp, uint8_t *buf, int len)
{
    if (buf && buf[0]) {
        int t;

        if (!parse_axhang_ms(buf, &t)) return false;

        param.axdelay = t;
        param.txdelay = (uint16_t)((param.axdelay * 3 + 1) / 2);
        if (param.txdelay > 1000) {
            param.txdelay = 1000;
        }

        // set txdelay for KISS (10ms unit)
        tnc[0].kiss_txdelay = (param.axdelay + 5) / 10;

    } else {
        uint8_t temp[16];

        tty_write_str(ttyp, "AXDELAY ");
        tty_write(ttyp, temp, snprintf(temp, sizeof(temp), "%ums", param.axdelay));
        tty_write_str(ttyp, "\r\n");
    }

    return true;
}

static bool cmd_axhang(tty_t *ttyp, uint8_t *buf, int len)
{
    if (buf && buf[0]) {
        int t;

        if (!parse_axhang_ms(buf, &t)) return false;

        param.axhang = t;

    } else {
        uint8_t temp[16];

        tty_write_str(ttyp, "AXHANG ");
        tty_write(ttyp, temp, snprintf(temp, sizeof(temp), "%ums", param.axhang));
        tty_write_str(ttyp, "\r\n");
    }

    return true;
}

static bool cmd_calibrate(tty_t *ttyp, uint8_t *buf, int len)
{
    //tty_write_str(ttyp, "CALIBRATE\r\n");
    tnc_t *tp = &tnc[0];
    if (tp->send_state != SP_IDLE) {
        tty_write_str(ttyp, "Transmitter busy\r\n");
        return false;
    }

    tp->send_state = SP_CALIBRATE;
    tp->do_nrzi = false;
    calibrate_mode = true;
    calibrate_idx = 0;
    tp->cal_data = 0x00;
    tp->ttyp = ttyp;
    tp->cal_time = tnc_time();
    tty_write_str(ttyp, "Calibrate Mode. SP to toggle; ctl C to Exit\r\n");
    return true;
}

void calibrate(void)
{
    tnc_t *tp = &tnc[0];
    if (tp->send_state != SP_CALIBRATE_OFF) return;

    tp->send_state = SP_IDLE;
    tp->do_nrzi = true;
    calibrate_mode = false;
    tty_write_str(tp->ttyp, "Exit Calibrate Mode\r\n");
    cmd_emit_prompt_if_idle(tp->ttyp);
}

static bool cmd_converse(tty_t *ttyp, uint8_t *buf, int len)
{
    //tty_write("CONVERSE\r\n", 10);
    converse_mode = true;
    tty_write_str(ttyp, "***  Converse Mode, ctl C to Exit\r\n");
    return true;
}

static bool cmd_kiss(tty_t *ttyp, uint8_t *buf, int len)
{
    if (buf && buf[0]) {

        if (!strncasecmp(buf, "ON", 2)) {
            ttyp->kiss_mode = 1;
        } else if (!strncasecmp(buf, "OFF", 3)) {
            ttyp->kiss_mode = 0;
        } else {
            return false;
        }

     } else {

        tty_write_str(ttyp, "KISS ");
        if (ttyp->kiss_mode) {
            tty_write_str(ttyp, "ON"); 
        } else {
            tty_write_str(ttyp, "OFF");
        }
        tty_write_str(ttyp, "\r\n");
    }

    return true;
}

static uint8_t *skip_spaces(uint8_t *p)
{
    while (p && *p == ' ') p++;
    return p;
}

static mona_addr_type_t mona_param_to_addr_type(uint8_t t)
{
    if (t == MONA_ACTIVE_P2SH) return MONA_ADDR_P2SH;
    if (t == MONA_ACTIVE_P2WPKH) return MONA_ADDR_P2WPKH;
    return MONA_ADDR_P2PKH;
}

static uint8_t mona_addr_type_to_param(mona_addr_type_t t)
{
    if (t == MONA_ADDR_P2SH) return MONA_ACTIVE_P2SH;
    if (t == MONA_ADDR_P2WPKH) return MONA_ACTIVE_P2WPKH;
    return MONA_ACTIVE_P2PKH;
}

static const char *mona_param_type_name(uint8_t t)
{
    if (t == MONA_ACTIVE_P2SH) return "p2sh";
    if (t == MONA_ACTIVE_P2WPKH) return "p2wpkh";
    return "p2pkh";
}

static void bytes_to_hex(const uint8_t *src, int len, char *out, int out_len)
{
    static const char *hex = "0123456789abcdef";
    int i;
    int p = 0;

    for (i = 0; i < len && p + 2 < out_len; ++i) {
        out[p++] = hex[(src[i] >> 4) & 0x0f];
        out[p++] = hex[src[i] & 0x0f];
    }
    out[p] = '\0';
}

static bool mona_format_wif_for_type(const mona_keyslot_t *slot,
                                     mona_addr_type_t type,
                                     const char *typed_prefix,
                                     char *out,
                                     size_t out_sz)
{
    mona_privkey_t priv;
    char wif[MONA_WIF_MAX];
    mona_err_t err;

    memset(&priv, 0, sizeof(priv));
    memcpy(priv.secret, slot->secret, sizeof(priv.secret));
    priv.compressed = slot->compressed;
    priv.txin_type = mona_addr_type_to_txin_type(type);

    err = mona_encode_wif(&priv, wif, sizeof(wif));
    if (err != MONA_OK) {
        snprintf(out, out_sz, "(unavailable)");
        return false;
    }

    if (typed_prefix && typed_prefix[0]) {
        snprintf(out, out_sz, "%s:%s", typed_prefix, wif);
    } else {
        snprintf(out, out_sz, "%s", wif);
    }

    return true;
}

static void privkey_show_print_notice(tty_t *ttyp)
{
    tty_write_str(ttyp, "*\r\n");
    tty_write_str(ttyp, "* SECURITY NOTICE !\r\n");
    tty_write_str(ttyp, "*\r\n");
    tty_write_str(ttyp, "* This is your secret key.\r\n");
    tty_write_str(ttyp, "* Never share this data with anyone else!\r\n");
    tty_write_str(ttyp, "* Please ensure no one is standing behind you.\r\n");
    tty_write_str(ttyp, "*\r\n");
    tty_write_str(ttyp, "* Press [Enter] to proceed or any other key to abort.\r\n");
    tty_write_str(ttyp, "*\r\n");
}

static void privkey_show_print_body(tty_t *ttyp)
{
    mona_keyslot_t slot;
    mona_address_info_t addrs;
    char raw_hex[65];
    char wif_p2pkh[MONA_WIF_MAX + 8];
    char wif_p2sh[MONA_WIF_MAX + 20];
    char wif_p2wpkh[MONA_WIF_MAX + 16];
    mona_err_t err;

    memset(&slot, 0, sizeof(slot));
    memset(&addrs, 0, sizeof(addrs));
    memcpy(slot.secret, param.mona_privkey, sizeof(slot.secret));
    slot.valid = param.mona_privkey_valid ? true : false;
    slot.compressed = param.mona_privkey_compressed ? true : false;
    slot.active_type = mona_param_to_addr_type(param.mona_active_type);

    bytes_to_hex(slot.secret, 32, raw_hex, sizeof(raw_hex));
    mona_format_wif_for_type(&slot, MONA_ADDR_P2PKH, "", wif_p2pkh, sizeof(wif_p2pkh));
    mona_format_wif_for_type(&slot, MONA_ADDR_P2SH, "p2wpkh-p2sh", wif_p2sh, sizeof(wif_p2sh));
    mona_format_wif_for_type(&slot, MONA_ADDR_P2WPKH, "p2wpkh", wif_p2wpkh, sizeof(wif_p2wpkh));

    tty_write_str(ttyp, "*\r\n");
    tty_write_str(ttyp, "* KEY WIF:\r\n");
    tty_write_str(ttyp, "*   p2pkh   : ");
    tty_write_str(ttyp, wif_p2pkh);
    tty_write_str(ttyp, "\r\n");
    tty_write_str(ttyp, "*   p2sh    : ");
    tty_write_str(ttyp, wif_p2sh);
    tty_write_str(ttyp, "\r\n");
    tty_write_str(ttyp, "*   p2wpkh  : ");
    tty_write_str(ttyp, wif_p2wpkh);
    tty_write_str(ttyp, "\r\n");
    tty_write_str(ttyp, "*\r\n");
    tty_write_str(ttyp, "* KEY RAW:\r\n");
    tty_write_str(ttyp, "*   ");
    tty_write_str(ttyp, raw_hex);
    tty_write_str(ttyp, "\r\n");
    tty_write_str(ttyp, "*\r\n");
    tty_write_str(ttyp, "* This is your identification key.\r\n");
    tty_write_str(ttyp, "* Please be sure to save a backup of it.\r\n");
    tty_write_str(ttyp, "*\r\n");

    err = mona_keypair_from_secret(slot.secret, &addrs);
    tty_write_str(ttyp, "* ADDRESS\r\n");
    tty_write_str(ttyp, "*   p2pkh (M)     : ");
    tty_write_str(ttyp, (err == MONA_OK) ? addrs.addr_M : "(calculation failed)");
    tty_write_str(ttyp, "\r\n");
    tty_write_str(ttyp, "*   p2sh  (P)     : ");
    tty_write_str(ttyp, (err == MONA_OK) ? addrs.addr_P : "(calculation failed)");
    tty_write_str(ttyp, "\r\n");
    tty_write_str(ttyp, "*   p2wpkh(mona1) : ");
    tty_write_str(ttyp, (err == MONA_OK) ? addrs.addr_mona1 : "(calculation failed)");
    tty_write_str(ttyp, "\r\n");
    tty_write_str(ttyp, "*\r\n");
    tty_write_str(ttyp, "* Active : ");
    tty_write_str(ttyp, mona_param_type_name(param.mona_active_type));
    tty_write_str(ttyp, "\r\n");
}

static bool cmd_privkey(tty_t *ttyp, uint8_t *buf, int len)
{
    (void)len;
    mona_keyslot_t slot;
    mona_err_t err;
    uint8_t *p;

    if (!buf || !buf[0]) {
        tty_write_str(ttyp, "PRIVKEY show, gen [m|p|mona1|p2pkh|p2sh|p2wpkh], set [m|p|mona1|p2pkh|p2sh|p2wpkh|WIF|RAW]\r\n");
        return true;
    }

    p = skip_spaces(buf);

    mona_backend_minimal_init();

    if (!strncasecmp((char *)p, "SHOW", 4) && (p[4] == '\0' || p[4] == ' ')) {
        if (!param.mona_privkey_valid) return false;
        privkey_show_print_notice(ttyp);
        cmd_pending_state = CMD_PENDING_PRIVKEY_SHOW_CONFIRM;
        cmd_pending_ttyp = ttyp;
        return true;
    }

    if (!strncasecmp((char *)p, "GEN", 3) && (p[3] == '\0' || p[3] == ' ')) {
        mona_addr_type_t t = mona_param_to_addr_type(param.mona_active_type);
        p = skip_spaces(p + 3);

        if (*p) {
            err = mona_parse_addr_type((char *)p, &t);
            if (err != MONA_OK) return false;
        }

        if (!privkey_gen_start(ttyp, t)) return false;
        cmd_pending_state = CMD_PENDING_PRIVKEY_GEN_COLLECTING;
        cmd_pending_ttyp = ttyp;
        return true;
    }

    if (!strncasecmp((char *)p, "SET", 3) && p[3] == ' ') {
        mona_addr_type_t t;

        p = skip_spaces(p + 3);
        if (!*p) return false;

        err = mona_parse_addr_type((char *)p, &t);
        if (err == MONA_OK) {
            param.mona_active_type = mona_addr_type_to_param(t);
            return true;
        }

        memset(&slot, 0, sizeof(slot));
        if (param.mona_privkey_valid) {
            memcpy(slot.secret, param.mona_privkey, sizeof(slot.secret));
            slot.compressed = param.mona_privkey_compressed ? true : false;
            slot.active_type = mona_param_to_addr_type(param.mona_active_type);
            slot.valid = true;
        } else {
            slot.active_type = MONA_ADDR_P2PKH;
        }

        err = mona_keyslot_init_from_input(&slot, (char *)p, true);
        if (err != MONA_OK) return false;

        memcpy(param.mona_privkey, slot.secret, sizeof(param.mona_privkey));
        param.mona_privkey_valid = slot.valid ? 1 : 0;
        param.mona_privkey_compressed = slot.compressed ? 1 : 0;
        param.mona_active_type = mona_addr_type_to_param(slot.active_type);
        return true;
    }

    return false;
}

static int calc_unproto_ui_frame_len(int info_len)
{
    int pkt_len = 7 * 2;
    int repeaters = 0;
    int i;

    for (i = 1; i < UNPROTO_N; ++i) {
        if (param.unproto[i].call[0]) repeaters++;
    }

    pkt_len += repeaters * 7;
    pkt_len += 2 + info_len + 2;
    return pkt_len;
}

static bool json_escape_message(const uint8_t *in, int in_len, char *out, int out_sz)
{
    int i;
    int p = 0;

    if (!in || !out || out_sz <= 0) return false;

    for (i = 0; i < in_len; ++i) {
        uint8_t ch = in[i];

        if (ch == '"' || ch == '\\') {
            if (p + 2 >= out_sz) return false;
            out[p++] = '\\';
            out[p++] = (char)ch;
            continue;
        }

        if (ch < 0x20) return false;

        if (p + 1 >= out_sz) return false;
        out[p++] = (char)ch;
    }

    out[p] = '\0';
    return true;
}

static bool sign_prepare_and_prompt_tx(tty_t *ttyp, const char *json_msg)
{
    mona_keyslot_t slot;
    mona_err_t err;
    uint64_t t0_us;
    uint64_t t1_us;
    char sig_b64[MONA_SIG_B64_MAX];
    int payload_len;
    char s[80];
    int frame_len;
    if (!sign_check_prerequisites(ttyp, false, true)) return true;

    mona_backend_minimal_init();

    memset(&slot, 0, sizeof(slot));
    memcpy(slot.secret, param.mona_privkey, sizeof(slot.secret));
    slot.valid = param.mona_privkey_valid ? true : false;
    slot.compressed = param.mona_privkey_compressed ? true : false;
    slot.active_type = mona_param_to_addr_type(param.mona_active_type);

    tty_write_str(ttyp, "Digital signature calculation in progress... ");
    t0_us = time_us_64();
    err = mona_keyslot_sign_message(&slot, json_msg, sig_b64, sizeof(sig_b64), NULL, 0);
    t1_us = time_us_64();
    if (err != MONA_OK) {
        tty_write_str(ttyp, "Failed.\r\n");
        return true;
    }
    tty_write(ttyp, (uint8_t *)"Completed. (", 12);
    snprintf(s, sizeof(s), "%lluus", (unsigned long long)(t1_us - t0_us));
    tty_write_str(ttyp, s);
    tty_write_str(ttyp, ")\r\n");

    payload_len = snprintf((char *)sign_tx_ctx.payload, sizeof(sign_tx_ctx.payload), "%s%s", json_msg, sig_b64);
    if (payload_len <= 0 || payload_len >= (int)sizeof(sign_tx_ctx.payload)) {
        tty_write_str(ttyp, "Signed payload is too long.\r\n");
        return true;
    }
    sign_tx_ctx.payload_len = payload_len;
    sign_tx_ctx.ttyp = ttyp;

    tty_write(ttyp, sign_tx_ctx.payload, sign_tx_ctx.payload_len);
    tty_write_str(ttyp, "\r\n");

    frame_len = calc_unproto_ui_frame_len(sign_tx_ctx.payload_len);
    if (frame_len < 256) {
        snprintf(s, sizeof(s), "%dbyte < 256byte OK.\r\n", frame_len);
        tty_write_str(ttyp, s);
    } else {
        snprintf(s, sizeof(s), "%dbyte >= 256byte NG.\r\n", frame_len);
        tty_write_str(ttyp, s);
        return true;
    }

    tty_write_str(ttyp, "Ready to send. Press [Enter] to TX or [ESC] to abort.\r\n");
    cmd_pending_state = CMD_PENDING_SIGN_TX_CONFIRM;
    cmd_pending_ttyp = ttyp;
    return true;
}

static const char *sign_active_address(const mona_address_info_t *addrs)
{
    mona_addr_type_t type = mona_param_to_addr_type(param.mona_active_type);
    if (type == MONA_ADDR_P2SH) return addrs->addr_P;
    if (type == MONA_ADDR_P2WPKH) return addrs->addr_mona1;
    return addrs->addr_M;
}

static bool sign_check_prerequisites(tty_t *ttyp, bool print_status, bool print_messages)
{
    bool has_privkey = param.mona_privkey_valid ? true : false;
    bool has_route = (param.mycall.call[0] && param.unproto[0].call[0]) ? true : false;
    bool ready = has_privkey && has_route;

    if (print_status) {
        mona_address_info_t addrs;
        mona_err_t err = MONA_ERR_ARGS;
        const char *addr_text = "(not set)";

        tty_write_str(ttyp, "MYCALL  : ");
        if (param.mycall.call[0]) tty_write_str(ttyp, param.mycall.call);
        else tty_write_str(ttyp, "(not set)");
        tty_write_str(ttyp, "\r\n");

        tty_write_str(ttyp, "UNPROTO : ");
        if (param.unproto[0].call[0]) {
            int i;
            for (i = 0; i < UNPROTO_N; i++) {
                uint8_t temp[10];
                if (!param.unproto[i].call[0]) break;
                if (i > 0) tty_write_str(ttyp, " V ");
                tty_write(ttyp, temp, callsign2ascii(temp, &param.unproto[i]));
            }
        } else {
            tty_write_str(ttyp, "(not set)");
        }
        tty_write_str(ttyp, "\r\n");

        tty_write_str(ttyp, "ADDRESS : ");
        if (has_privkey) {
            memset(&addrs, 0, sizeof(addrs));
            mona_backend_minimal_init();
            err = mona_keypair_from_secret(param.mona_privkey, &addrs);
            if (err == MONA_OK) addr_text = sign_active_address(&addrs);
            else addr_text = "(calculation failed)";
        }
        tty_write_str(ttyp, addr_text);
        tty_write_str(ttyp, "\r\n");
    }

    if (print_messages && !ready) {
        if (!has_route) {
            tty_write_str(ttyp, "Please set MYCALL and UNPROTO before SIGN.\r\n");
        }
        if (!has_privkey) {
            tty_write_str(ttyp, "No private key. Run \"privkey gen\" or \"privkey set\" first.\r\n");
        }
    }

    return ready;
}

static bool soft_clock_is_leap(int year)
{
    if ((year % 400) == 0) return true;
    if ((year % 100) == 0) return false;
    return (year % 4) == 0;
}

static int soft_clock_days_in_month(int year, int month)
{
    static const int days[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    if (month == 2) return soft_clock_is_leap(year) ? 29 : 28;
    if (month < 1 || month > 12) return 31;
    return days[month - 1];
}

static bool parse_yyyymmdd(const char *s, int *year, int *month, int *day)
{
    int y, m, d;
    if (!s || strlen(s) != 8) return false;
    if (sscanf(s, "%4d%2d%2d", &y, &m, &d) != 3) return false;
    if (m < 1 || m > 12) return false;
    if (d < 1 || d > soft_clock_days_in_month(y, m)) return false;
    *year = y;
    *month = m;
    *day = d;
    return true;
}

static bool parse_hhmmtz(const char *s, int *hour, int *min)
{
    int hh, mm;
    if (!s || strlen(s) < 4) return false;
    if (!isdigit((unsigned char)s[0]) || !isdigit((unsigned char)s[1]) ||
        !isdigit((unsigned char)s[2]) || !isdigit((unsigned char)s[3])) {
        return false;
    }
    hh = (s[0] - '0') * 10 + (s[1] - '0');
    mm = (s[2] - '0') * 10 + (s[3] - '0');
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return false;
    *hour = hh;
    *min = mm;
    return true;
}

static void soft_clock_add_elapsed(soft_clock_t *c, uint64_t elapsed_sec)
{
    uint64_t total_min;
    uint64_t total_hour;
    uint64_t day_add;
    int dim;

    if (!c->is_set || elapsed_sec == 0) return;

    c->sec += (int)(elapsed_sec % 60);
    elapsed_sec /= 60;
    if (c->sec >= 60) {
        c->sec -= 60;
        elapsed_sec++;
    }

    total_min = (uint64_t)c->min + (elapsed_sec % 60);
    elapsed_sec /= 60;
    c->min = (int)(total_min % 60);
    elapsed_sec += total_min / 60;

    total_hour = (uint64_t)c->hour + (elapsed_sec % 24);
    day_add = elapsed_sec / 24;
    c->hour = (int)(total_hour % 24);
    day_add += total_hour / 24;

    while (day_add > 0) {
        dim = soft_clock_days_in_month(c->year, c->month);
        if (c->day < dim) {
            c->day++;
        } else {
            c->day = 1;
            c->month++;
            if (c->month > 12) {
                c->month = 1;
                c->year++;
            }
        }
        day_add--;
    }
}

static bool soft_clock_get_preset(char out_date[16], char out_time[16])
{
    soft_clock_t snap = soft_clock;
    uint64_t now_us;
    uint64_t elapsed_sec;

    if (!snap.is_set) return false;

    now_us = time_us_64();
    elapsed_sec = (now_us - snap.anchor_us) / 1000000ULL;
    soft_clock_add_elapsed(&snap, elapsed_sec);

    snprintf(out_date, 16, "%04d%02d%02d", snap.year, snap.month, snap.day);
    snprintf(out_time, 16, "%02d%02dJST", snap.hour, snap.min);
    return true;
}

static bool qsl_validate_and_normalize_date(const char *in, char out[16])
{
    int i;
    int p = 0;
    int year, month, day;

    if (!in || !in[0]) return false;

    for (i = 0; in[i] && p < 8; ++i) {
        if (isdigit((unsigned char)in[i])) {
            out[p++] = in[i];
            continue;
        }
        if (in[i] == '/' || in[i] == '-' || in[i] == ' ') continue;
        return false;
    }
    if (in[i] != '\0') return false;
    if (p != 8) return false;
    out[8] = '\0';
    if (!parse_yyyymmdd(out, &year, &month, &day)) return false;
    return true;
}

static bool qsl_validate_and_normalize_time(const char *in, char out[16])
{
    char digits[8];
    char tz[8];
    int d = 0;
    int t = 0;
    int i;
    int hour;
    int min;
    bool tz_started = false;

    if (!in || !in[0]) return false;

    for (i = 0; in[i]; ++i) {
        char ch = in[i];
        if (!tz_started && isdigit((unsigned char)ch)) {
            if (d >= 4) return false;
            digits[d++] = ch;
            continue;
        }
        if (!tz_started && (ch == ':' || ch == ' ')) continue;
        if (isalpha((unsigned char)ch)) {
            tz_started = true;
            if (t >= (int)sizeof(tz) - 1) return false;
            tz[t++] = (char)toupper((unsigned char)ch);
            continue;
        }
        if (ch == ' ' && tz_started) continue;
        return false;
    }

    if (d != 4) return false;
    digits[4] = '\0';
    if (t == 0) {
        strcpy(tz, "JST");
    } else {
        tz[t] = '\0';
    }

    if (!parse_hhmmtz(digits, &hour, &min)) return false;
    snprintf(out, 16, "%s%s", digits, tz);
    return true;
}

static void qsl_upper_copy(char *out, size_t out_sz, const char *in)
{
    size_t i;
    for (i = 0; i + 1 < out_sz && in && in[i]; ++i) {
        out[i] = (char)toupper((unsigned char)in[i]);
    }
    out[i] = '\0';
}

static bool qsl_build_json(const qsl_data_t *data, char *json_out, size_t json_out_sz)
{
    char qsl[64], rs[32], date[32], time[32], freq[64], mode[32], qth[128];

    if (!json_escape_message((const uint8_t *)data->qsl, (int)strlen(data->qsl), qsl, sizeof(qsl))) return false;
    if (!json_escape_message((const uint8_t *)data->rs, (int)strlen(data->rs), rs, sizeof(rs))) return false;
    if (!json_escape_message((const uint8_t *)data->date, (int)strlen(data->date), date, sizeof(date))) return false;
    if (!json_escape_message((const uint8_t *)data->time, (int)strlen(data->time), time, sizeof(time))) return false;
    if (!json_escape_message((const uint8_t *)data->freq, (int)strlen(data->freq), freq, sizeof(freq))) return false;
    if (!json_escape_message((const uint8_t *)data->mode, (int)strlen(data->mode), mode, sizeof(mode))) return false;
    if (!json_escape_message((const uint8_t *)data->qth, (int)strlen(data->qth), qth, sizeof(qth))) return false;

    if (snprintf(json_out, json_out_sz,
                 "{\"QSL\":\"%s\",\"S\":\"%s\",\"D\":\"%s\",\"T\":\"%s\",\"F\":\"%s\",\"M\":\"%s\",\"P\":\"%s\"}",
                 qsl, rs, date, time, freq, mode, qth) >= (int)json_out_sz) {
        return false;
    }
    return true;
}

static bool qsl_try_update_soft_clock(const qsl_data_t *data)
{
    int year, month, day;
    int hour, min;

    if (!parse_yyyymmdd(data->date, &year, &month, &day)) return false;
    if (!parse_hhmmtz(data->time, &hour, &min)) return false;

    soft_clock.is_set = true;
    soft_clock.year = year;
    soft_clock.month = month;
    soft_clock.day = day;
    soft_clock.hour = hour;
    soft_clock.min = min;
    soft_clock.sec = 0;
    soft_clock.anchor_us = time_us_64();
    return true;
}

static bool qsl_finalize_and_sign(tty_t *ttyp, qsl_data_t *data)
{
    char norm_date[16];
    char norm_time[16];
    char json_msg[256];

    if (!data->qsl[0] || !data->rs[0] || !data->date[0] || !data->time[0]) {
        tty_write_str(ttyp, "QSL requires TO/RS/DATE/TIME.\r\n");
        return true;
    }
    if (!qsl_validate_and_normalize_date(data->date, norm_date)) {
        tty_write_str(ttyp, "Invalid DATE. Use YYYYMMDD or YYYY/MM/DD.\r\n");
        return true;
    }
    if (!qsl_validate_and_normalize_time(data->time, norm_time)) {
        tty_write_str(ttyp, "Invalid TIME. Use HHMM, HH:MM, or HHMMTZ.\r\n");
        return true;
    }

    strncpy(data->date, norm_date, sizeof(data->date));
    data->date[sizeof(data->date) - 1] = '\0';
    strncpy(data->time, norm_time, sizeof(data->time));
    data->time[sizeof(data->time) - 1] = '\0';

    if (!qsl_build_json(data, json_msg, sizeof(json_msg))) {
        tty_write_str(ttyp, "QSL payload contains unsupported characters or is too long.\r\n");
        return true;
    }

    qsl_try_update_soft_clock(data);
    return sign_prepare_and_prompt_tx(ttyp, json_msg);
}

static bool qsl_parse_args(char *args, qsl_data_t *out)
{
    char *tok[32];
    int tok_n = 0;
    int i = 0;

    memset(out, 0, sizeof(*out));
    tok[tok_n] = strtok(args, " ");
    while (tok[tok_n] && tok_n + 1 < (int)(sizeof(tok) / sizeof(tok[0]))) {
        tok_n++;
        tok[tok_n] = strtok(NULL, " ");
    }
    if (tok_n == 0) return false;

    qsl_upper_copy(out->qsl, sizeof(out->qsl), tok[0]);
    i = 1;
    while (i < tok_n) {
        if (!strcasecmp(tok[i], "-rs")) {
            if (i + 1 >= tok_n) return false;
            strncpy(out->rs, tok[i + 1], sizeof(out->rs) - 1);
            i += 2;
            continue;
        }
        if (!strcasecmp(tok[i], "-date")) {
            char buf[32] = {0};
            int k = 0;
            int j = i + 1;
            if (j >= tok_n) return false;
            while (j < tok_n && tok[j][0] != '-' && k < 3) {
                if (k > 0) strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
                strncat(buf, tok[j], sizeof(buf) - strlen(buf) - 1);
                j++;
                k++;
            }
            if (k == 0) return false;
            strncpy(out->date, buf, sizeof(out->date) - 1);
            i = j;
            continue;
        }
        if (!strcasecmp(tok[i], "-time")) {
            char buf[32] = {0};
            int k = 0;
            int j = i + 1;
            if (j >= tok_n) return false;
            while (j < tok_n && tok[j][0] != '-' && k < 2) {
                if (k > 0) strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
                strncat(buf, tok[j], sizeof(buf) - strlen(buf) - 1);
                j++;
                k++;
            }
            if (k == 0) return false;
            strncpy(out->time, buf, sizeof(out->time) - 1);
            i = j;
            continue;
        }
        if (!strcasecmp(tok[i], "-freq")) {
            if (i + 1 >= tok_n) return false;
            strncpy(out->freq, tok[i + 1], sizeof(out->freq) - 1);
            i += 2;
            continue;
        }
        if (!strcasecmp(tok[i], "-mode")) {
            if (i + 1 >= tok_n) return false;
            qsl_upper_copy(out->mode, sizeof(out->mode), tok[i + 1]);
            i += 2;
            continue;
        }
        if (!strcasecmp(tok[i], "-qth")) {
            int j = i + 1;
            if (j >= tok_n) return false;
            out->qth[0] = '\0';
            while (j < tok_n) {
                if ((strlen(out->qth) + strlen(tok[j]) + 2) >= sizeof(out->qth)) return false;
                if (out->qth[0]) strncat(out->qth, " ", sizeof(out->qth) - strlen(out->qth) - 1);
                strncat(out->qth, tok[j], sizeof(out->qth) - strlen(out->qth) - 1);
                j++;
            }
            i = j;
            continue;
        }
        return false;
    }

    return true;
}

static void sign_qsl_wizard_prompt(tty_t *ttyp)
{
    char date[16];
    char time[16];

    switch (sign_qsl_wizard_ctx.step) {
        case 0: tty_write_str(ttyp, "TO: "); break;
        case 1: tty_write_str(ttyp, "RS: "); break;
        case 2:
            if (soft_clock_get_preset(date, time)) {
                tty_write_str(ttyp, "DATE[");
                tty_write_str(ttyp, date);
                tty_write_str(ttyp, "]: ");
            } else {
                tty_write_str(ttyp, "DATE: ");
            }
            break;
        case 3:
            if (soft_clock_get_preset(date, time)) {
                tty_write_str(ttyp, "TIME[");
                tty_write_str(ttyp, time);
                tty_write_str(ttyp, "]: ");
            } else {
                tty_write_str(ttyp, "TIME: ");
            }
            break;
        case 4: tty_write_str(ttyp, "FREQ: "); break;
        case 5: tty_write_str(ttyp, "MODE: "); break;
        case 6: tty_write_str(ttyp, "QTH: "); break;
        default: break;
    }
}

static bool sign_qsl_wizard_start(tty_t *ttyp)
{
    if (!sign_check_prerequisites(ttyp, false, true)) return true;

    memset(&sign_qsl_wizard_ctx, 0, sizeof(sign_qsl_wizard_ctx));
    sign_qsl_wizard_ctx.ttyp = ttyp;

    tty_write_str(ttyp, "Required: TO, RS, DATE, TIME\r\n");
    tty_write_str(ttyp, "Optional: FREQ, MODE, QTH\r\n");
    tty_write_str(ttyp, "Please input the data.\r\n");
    sign_qsl_wizard_prompt(ttyp);
    cmd_pending_state = CMD_PENDING_SIGN_QSL_WIZARD;
    cmd_pending_ttyp = ttyp;
    return true;
}

static bool sign_qsl_wizard_store_input(void)
{
    char date[16];
    char time[16];
    char *in = sign_qsl_wizard_ctx.input;

    if (sign_qsl_wizard_ctx.step == 2 && in[0] == '\0' && soft_clock_get_preset(date, time)) {
        in = date;
    }
    if (sign_qsl_wizard_ctx.step == 3 && in[0] == '\0' && soft_clock_get_preset(date, time)) {
        in = time;
    }

    switch (sign_qsl_wizard_ctx.step) {
        case 0: qsl_upper_copy(sign_qsl_wizard_ctx.data.qsl, sizeof(sign_qsl_wizard_ctx.data.qsl), in); break;
        case 1: strncpy(sign_qsl_wizard_ctx.data.rs, in, sizeof(sign_qsl_wizard_ctx.data.rs) - 1); break;
        case 2: strncpy(sign_qsl_wizard_ctx.data.date, in, sizeof(sign_qsl_wizard_ctx.data.date) - 1); break;
        case 3: strncpy(sign_qsl_wizard_ctx.data.time, in, sizeof(sign_qsl_wizard_ctx.data.time) - 1); break;
        case 4: strncpy(sign_qsl_wizard_ctx.data.freq, in, sizeof(sign_qsl_wizard_ctx.data.freq) - 1); break;
        case 5: qsl_upper_copy(sign_qsl_wizard_ctx.data.mode, sizeof(sign_qsl_wizard_ctx.data.mode), in); break;
        case 6: strncpy(sign_qsl_wizard_ctx.data.qth, in, sizeof(sign_qsl_wizard_ctx.data.qth) - 1); break;
        default: return false;
    }
    return true;
}

static bool sign_qsl_wizard_consume_char(tty_t *ttyp, int ch)
{
    char date[16];
    char time[16];

    if (ttyp != sign_qsl_wizard_ctx.ttyp) return false;
    if (ch == '\x1b') {
        cmd_pending_state = CMD_PENDING_IDLE;
        cmd_pending_ttyp = NULL;
        memset(&sign_qsl_wizard_ctx, 0, sizeof(sign_qsl_wizard_ctx));
        tty_write_str(ttyp, "\r\nAborted by user.\r\n");
        cmd_emit_prompt_if_idle(ttyp);
        return true;
    }
    if (ch == '\n') return true;
    if (ch == '\b' || ch == 0x7f) {
        if (sign_qsl_wizard_ctx.input_len > 0) {
            sign_qsl_wizard_ctx.input_len--;
            sign_qsl_wizard_ctx.input[sign_qsl_wizard_ctx.input_len] = '\0';
            if (param.echo) tty_write_str(ttyp, "\b \b");
        } else if (param.echo) {
            tty_write_char(ttyp, '\a');
        }
        return true;
    }
    if (ch != '\r') {
        if ((ch >= ' ' && ch <= '~') && sign_qsl_wizard_ctx.input_len + 1 < (int)sizeof(sign_qsl_wizard_ctx.input)) {
            sign_qsl_wizard_ctx.input[sign_qsl_wizard_ctx.input_len++] = (char)ch;
            sign_qsl_wizard_ctx.input[sign_qsl_wizard_ctx.input_len] = '\0';
            if (param.echo) tty_write_char(ttyp, (uint8_t)ch);
        } else if (param.echo) {
            tty_write_char(ttyp, '\a');
        }
        return true;
    }

    if (param.echo) tty_write_str(ttyp, "\r\n");
    if (sign_qsl_wizard_ctx.input[0] == '\0' &&
        sign_qsl_wizard_ctx.step >= 0 && sign_qsl_wizard_ctx.step <= 3) {
        bool has_preset = false;
        if (sign_qsl_wizard_ctx.step == 2 || sign_qsl_wizard_ctx.step == 3) {
            has_preset = soft_clock_get_preset(date, time);
        }
        if (!has_preset) {
            tty_write_str(ttyp, "Required.\r\n");
            sign_qsl_wizard_prompt(ttyp);
            return true;
        }
    }

    if (!sign_qsl_wizard_store_input()) return false;
    sign_qsl_wizard_ctx.input_len = 0;
    sign_qsl_wizard_ctx.input[0] = '\0';
    sign_qsl_wizard_ctx.step++;

    if (sign_qsl_wizard_ctx.step > 6) {
        qsl_data_t data = sign_qsl_wizard_ctx.data;
        cmd_pending_state = CMD_PENDING_IDLE;
        cmd_pending_ttyp = NULL;
        memset(&sign_qsl_wizard_ctx, 0, sizeof(sign_qsl_wizard_ctx));
        return qsl_finalize_and_sign(ttyp, &data);
    }

    sign_qsl_wizard_prompt(ttyp);
    return true;
}

static bool cmd_sign(tty_t *ttyp, uint8_t *buf, int len)
{
    qsl_data_t qsl_data;
    char escaped_msg[CMD_BUF_LEN * 2 + 1];
    char json_msg[256];
    char arg_copy[CMD_BUF_LEN + 1];
    uint8_t *p;
    (void)len;

    if (!buf || !buf[0]) {
        if (sign_check_prerequisites(ttyp, true, true)) {
            tty_write_str(ttyp, "Ready to go.\r\n");
        }
        return true;
    }

    p = skip_spaces(buf);

    if (!strncasecmp((char *)p, "MSG", 3) && p[3] == ' ') {
        p = skip_spaces(p + 3);
        if (!*p) return false;

        if (!json_escape_message(p, (int)strlen((char *)p), escaped_msg, sizeof(escaped_msg))) {
            tty_write_str(ttyp, "Message contains unsupported control characters or is too long.\r\n");
            return true;
        }
        snprintf(json_msg, sizeof(json_msg), "{\"msg\":\"%s\"}", escaped_msg);
        return sign_prepare_and_prompt_tx(ttyp, json_msg);
    }

    if (!strncasecmp((char *)p, "QSL", 3) && (p[3] == '\0' || p[3] == ' ')) {
        p = skip_spaces(p + 3);
        if (!*p) return sign_qsl_wizard_start(ttyp);

        strncpy(arg_copy, (char *)p, sizeof(arg_copy) - 1);
        arg_copy[sizeof(arg_copy) - 1] = '\0';
        if (!qsl_parse_args(arg_copy, &qsl_data)) return false;
        return qsl_finalize_and_sign(ttyp, &qsl_data);
    }

    return false;
}

static void disp_section(tty_t *ttyp, uint8_t const *title)
{
    tty_write_str(ttyp, "\r\n");
    tty_write_str(ttyp, "=== ");
    tty_write_str(ttyp, title);
    tty_write_str(ttyp, " ===\r\n");
}

static bool cmd_disp(tty_t *ttyp, uint8_t *buf, int len)
{
    (void)buf;
    (void)len;

    tty_write_str(ttyp, "\r\n");

    disp_section(ttyp, "Station");
    cmd_mycall(ttyp, NULL, 0);
    cmd_myalias(ttyp, NULL, 0);

    disp_section(ttyp, "Network");
    cmd_unproto(ttyp, NULL, 0);
    cmd_monitor(ttyp, NULL, 0);

    disp_section(ttyp, "Auto Operation");
    cmd_beacon(ttyp, NULL, 0);
    cmd_btext(ttyp, NULL, 0);
    cmd_digipeat(ttyp, NULL, 0);

    disp_section(ttyp, "GPS / Sensor");
    cmd_gps(ttyp, NULL, 0);

    disp_section(ttyp, "Hardware");
    cmd_txdelay(ttyp, NULL, 0);
    cmd_axdelay(ttyp, NULL, 0);
    cmd_axhang(ttyp, NULL, 0);

    disp_section(ttyp, "Diagnostics");
    cmd_echo(ttyp, NULL, 0);

    return true;
}

#define USB_BOOT_CONFIRM_TIMEOUT_TICKS (10 * 100)

static void usb_boot_confirm_abort(tty_t *ttyp)
{
    cmd_pending_state = CMD_PENDING_IDLE;
    cmd_pending_ttyp = NULL;
    memset(&usb_boot_confirm_ctx, 0, sizeof(usb_boot_confirm_ctx));
    tty_write_str(ttyp, "USB bootloader entry aborted.\r\n");
    cmd_emit_prompt_if_idle(ttyp);
}

static bool usb_boot_confirm_start(tty_t *ttyp)
{
    usb_boot_confirm_ctx.ttyp = ttyp;
    usb_boot_confirm_ctx.state = USB_BOOT_WAIT_Y;
    usb_boot_confirm_ctx.deadline_tick = tnc_time() + USB_BOOT_CONFIRM_TIMEOUT_TICKS;
    cmd_pending_state = CMD_PENDING_USB_BOOT_CONFIRM;
    cmd_pending_ttyp = ttyp;

    tty_write_str(ttyp, "WARNING: Enter USB bootloader mode?\r\n");
    tty_write_str(ttyp, "This will disconnect the current session.\r\n");
    tty_write_str(ttyp, "Press [Y] [E] [S] [Enter] in order within 10 seconds to continue; any other key aborts.\r\n");
    return true;
}

static bool cmd_system(tty_t *ttyp, uint8_t *buf, int len)
{
    if (!buf || !*buf) return false;

    uint8_t *p = skip_spaces(buf);
    if (!strncasecmp((char *)p, "usb_bootloader", 14) && (p[14] == '\0' || isspace(p[14]))) {
        p = skip_spaces(p + 14);
        if (*p) return false;
        return usb_boot_confirm_start(ttyp);
    }

    return false;
}

static bool cmd_about(tty_t *ttyp, uint8_t *buf, int len)
{
    (void)buf;
    (void)len;

    tty_write_str(ttyp, "\r\n");
    tty_write_str(ttyp, "PICO-TNC      Daisuke JA1UMW / CQAKIBA.TOKYO edition\r\n");
    tty_write_str(ttyp, "\r\n");
    tty_write_str(ttyp, "Extended and customized for modern DSP radios,\r\n");
    tty_write_str(ttyp, "Japanese help, and signed QSL experiments.\r\n");
    tty_write_str(ttyp, "\r\n");
    tty_write_str(ttyp, "Third-party components used or referenced by this project\r\n");
    tty_write_str(ttyp, "\r\n");
    tty_write_str(ttyp, "1. PICO-TNC\r\n");
    tty_write_str(ttyp, "   Project: amedes/pico_tnc\r\n");
    tty_write_str(ttyp, "   License: BSD-3-Clause\r\n");
    tty_write_str(ttyp, "   URL: https://github.com/amedes/pico_tnc\r\n");
    tty_write_str(ttyp, "   Notes: Original firmware base used for this project.\r\n");
    tty_write_str(ttyp, "\r\n");
    tty_write_str(ttyp, "2. libsecp256k1\r\n");
    tty_write_str(ttyp, "   Project: bitcoin-core/secp256k1\r\n");
    tty_write_str(ttyp, "   License: MIT\r\n");
    tty_write_str(ttyp, "   URL: https://github.com/bitcoin-core/secp256k1\r\n");
    tty_write_str(ttyp, "   Notes: Used for secp256k1 elliptic-curve operations.\r\n");
    tty_write_str(ttyp, "\r\n");
    tty_write_str(ttyp, "3. Electrum Mona\r\n");
    tty_write_str(ttyp, "   Project: wakiyamap/electrum-mona\r\n");
    tty_write_str(ttyp, "   License: MIT\r\n");
    tty_write_str(ttyp, "   URL: https://github.com/wakiyamap/electrum-mona\r\n");
    tty_write_str(ttyp, "   Notes: Used as behavioral/reference material for Monacoin-compatible\r\n");
    tty_write_str(ttyp, "          message signing and verification.\r\n");
    tty_write_str(ttyp, "   No code copied unless explicitly noted in source comments.\r\n");
    tty_write_str(ttyp, "\r\n");
    tty_write_str(ttyp, "4. libmona_pico / libmona-main\r\n");
    tty_write_str(ttyp, "   Author: Daisuke JA1UMW / CQAKIBA.TOKYO\r\n");
    tty_write_str(ttyp, "   License: MIT\r\n");
    tty_write_str(ttyp, "   Notes: Monacoin-compatible message-signing support created for this project.\r\n");

    return true;
}

static const cmd_t cmd_list[] = {
    { "HELP", 4, cmd_help, },
    { "?", 1, cmd_help, },
    { "ABOUT", 5, cmd_about, },
    { "DISP", 4, cmd_disp, },
    { "MYCALL", 6, cmd_mycall, },
    { "UNPROTO", 7, cmd_unproto, },
    { "BTEXT", 6, cmd_btext, },
    { "BEACON", 7, cmd_beacon, },
    { "MONITOR", 8, cmd_monitor, },
    { "DIGIPEAT", 9, cmd_digipeat, },
    { "MYALIAS", 8, cmd_myalias, },
    { "PERM", 4, cmd_perm, },
    { "ECHO", 4, cmd_echo, },
    { "GPS", 3, cmd_gps, },
    { "TXDELAY", 7, cmd_txdelay, },
    { "AXDELAY", 7, cmd_axdelay, },
    { "AXHANG", 6, cmd_axhang, },
    { "CALIBRATE", 9, cmd_calibrate, },
    { "CONVERSE", 8, cmd_converse, },
    { "K", 1, cmd_converse, },
    { "KISS", 4, cmd_kiss, },
    { "PRIVKEY", 7, cmd_privkey, },
    { "SIGN", 4, cmd_sign, },
    { "SYSTEM", 6, cmd_system, },

    // end mark
    { NULL, 0, NULL, },
};

bool cmd_has_pending_input(void)
{
    return cmd_pending_state != CMD_PENDING_IDLE;
}

bool cmd_consume_pending_input(tty_t *ttyp, int ch)
{
    if (ttyp != cmd_pending_ttyp) return false;

    if (cmd_pending_state == CMD_PENDING_PRIVKEY_SHOW_CONFIRM) {
        if (privkey_gen_ctx.has_pending_secret && privkey_gen_ctx.ttyp == ttyp) {
            if (ch == '\n') return true;
            if (ch == '\x1b') {
                privkey_gen_abort(ttyp);
                return true;
            }
            if (ch != '\r') return true;

            if (!privkey_gen_save_pending_secret()) {
                privkey_gen_abort(ttyp);
                return true;
            }

            cmd_pending_state = CMD_PENDING_IDLE;
            cmd_pending_ttyp = NULL;
            privkey_gen_reset();

            tty_write_str(ttyp, "Save complete.\r\n");
            tty_write_str(ttyp, "Run the \"privkey show\" command to verify your new private key.\r\n");
            tty_write_str(ttyp, "\r\n");
            tty_write_str(ttyp, "CRITICAL NOTICE:\r\n");
            tty_write_str(ttyp, "This key is your unique digital identity.\r\n");
            tty_write_str(ttyp, "It can NEVER be recreated.\r\n");
            tty_write_str(ttyp, "Please copy the key string immediately and store it in a secure location.\r\n");
            cmd_emit_prompt_if_idle(ttyp);
            return true;
        }

        if (ch == '\n') return true;

        if (param.echo) {
            if (ch >= ' ' && ch <= '~') {
                tty_write_char(ttyp, ch);
            }
            tty_write_str(ttyp, "\r\n");
        }

        if (ch == '\r') {
            privkey_show_print_body(ttyp);
        } else {
            tty_write_str(ttyp, "Aborted by user.\r\n");
        }

        cmd_pending_state = CMD_PENDING_IDLE;
        cmd_pending_ttyp = NULL;
        cmd_emit_prompt_if_idle(ttyp);
        return true;
    }

    if (cmd_pending_state == CMD_PENDING_PRIVKEY_GEN_COLLECTING) {
        return privkey_gen_consume_char(ttyp, ch);
    }

    if (cmd_pending_state == CMD_PENDING_SIGN_QSL_WIZARD) {
        return sign_qsl_wizard_consume_char(ttyp, ch);
    }

    if (cmd_pending_state == CMD_PENDING_SIGN_TX_CONFIRM) {
        if (ch == '\n') return true;
        if (ch == '\x1b') {
            cmd_pending_state = CMD_PENDING_IDLE;
            cmd_pending_ttyp = NULL;
            memset(&sign_tx_ctx, 0, sizeof(sign_tx_ctx));
            tty_write_str(ttyp, "Aborted by user.\r\n");
            cmd_emit_prompt_if_idle(ttyp);
            return true;
        }
        if (ch != '\r') return true;

        send_unproto(&tnc[CONVERSE_PORT], sign_tx_ctx.payload, sign_tx_ctx.payload_len);
        cmd_pending_state = CMD_PENDING_IDLE;
        cmd_pending_ttyp = NULL;
        memset(&sign_tx_ctx, 0, sizeof(sign_tx_ctx));
        cmd_emit_prompt_if_idle(ttyp);
        return true;
    }

    if (cmd_pending_state == CMD_PENDING_USB_BOOT_CONFIRM) {
        if (!usb_boot_confirm_ctx.ttyp || ttyp != usb_boot_confirm_ctx.ttyp) return false;
        if (ch == '\n') {
            if (usb_boot_confirm_ctx.state == USB_BOOT_WAIT_ENTER ||
                usb_boot_confirm_ctx.state == USB_BOOT_WAIT_Y) {
                return true;
            }
            usb_boot_confirm_abort(ttyp);
            return true;
        }
        if (ch == '\r' && usb_boot_confirm_ctx.state == USB_BOOT_WAIT_Y) return true;

        switch (usb_boot_confirm_ctx.state) {
            case USB_BOOT_WAIT_Y:
                if (ch != 'Y') {
                    usb_boot_confirm_abort(ttyp);
                    return true;
                }
                usb_boot_confirm_ctx.state = USB_BOOT_WAIT_E;
                return true;
            case USB_BOOT_WAIT_E:
                if (ch != 'E') {
                    usb_boot_confirm_abort(ttyp);
                    return true;
                }
                usb_boot_confirm_ctx.state = USB_BOOT_WAIT_S;
                return true;
            case USB_BOOT_WAIT_S:
                if (ch != 'S') {
                    usb_boot_confirm_abort(ttyp);
                    return true;
                }
                usb_boot_confirm_ctx.state = USB_BOOT_WAIT_ENTER;
                return true;
            case USB_BOOT_WAIT_ENTER:
                if (ch != '\r') {
                    usb_boot_confirm_abort(ttyp);
                    return true;
                }
                cmd_pending_state = CMD_PENDING_IDLE;
                cmd_pending_ttyp = NULL;
                memset(&usb_boot_confirm_ctx, 0, sizeof(usb_boot_confirm_ctx));
                tty_write_str(ttyp, "Entering USB bootloader...\r\n");
                reset_usb_boot(0, 0);
                return true;
            default:
                usb_boot_confirm_abort(ttyp);
                return true;
        }
    }

    return false;
}

void cmd_poll(void)
{
    if (cmd_pending_state != CMD_PENDING_USB_BOOT_CONFIRM) return;
    if (!usb_boot_confirm_ctx.ttyp) return;
    if ((int32_t)(tnc_time() - usb_boot_confirm_ctx.deadline_tick) < 0) return;
    usb_boot_confirm_abort(usb_boot_confirm_ctx.ttyp);
}


void cmd(tty_t *ttyp, uint8_t *buf, int len)
{
#if 0
    tud_cdc_write(buf, len);
    tud_cdc_write("\r\n", 2);
    tud_cdc_write_flush();
#endif

    uint8_t *top;
    int i;

    for (i = 0; i < len; i++) {
        if (buf[i] != ' ') break;
    }
    top = &buf[i];
    int n = len - i;

    if (n <= 0) return;

    uint8_t *param = strchr(top, ' ');
    int param_len = 0;

    if (param) {
        n = param - top;
        param_len = len - (param - buf);

        for (i = 0; i < param_len; i++) {
            if (param[i] != ' ') break;
        }
        param += i;
        param_len -= i;
    }

    cmd_t const *cp = &cmd_list[0], *mp;
    int matched = 0;

    while (cp->name) {

#if 0
        tud_cdc_write(cp->name, cp->len);
        tud_cdc_write("\r\n", 2);
        tud_cdc_write_flush();
#endif
     
        if (cp->len >= n && !strncasecmp(top, cp->name, n)) {
            ++matched;
            mp = cp;
        }
        cp++;
    }

    if (matched == 1) {

        if (mp->func(ttyp, param, param_len)) {
            if (!(converse_mode | calibrate_mode)) {
                if (!(mp->func == cmd_help && help_is_response_pending()) &&
                    !cmd_has_pending_input()) {
                    tty_write_str(ttyp, "\r\nOK\r\n");
                }
            }
            return;
        }
    }

    tty_write_str(ttyp, "\r\n?\r\n");
}
