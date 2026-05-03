/*
Copyright (c) 2021, Kazuhisa Yokota, JN1DFF
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
/*
Modifications:
Copyright (c) 2026 Daisuke JA1UMW / CQAKIBA.TOKYO
Released under the MIT License.
See LICENSE and LICENSE-3RD-PARTY for details.
*/

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "pico/stdlib.h"
#include "hardware/uart.h"

#include "gps.h"
#include "tnc.h"
#include "unproto.h"
#include "serial.h"

#define GPS_LEN 127
#define GPS_INTERVAL (3 * 60 * 100)
#define GPS_PORT 0
#define GPS_SCAN_TICKS 360
#define GPS_DIAG_SCAN_TICKS 360
#define GPS_NMEA_STALE_TICKS 1000

typedef struct {
    bool enabled;
    uint32_t baud_setting; /* 0: AUTO */
    uint32_t active_baud;
    uint32_t last_good_baud;
    bool search_active;
    uint8_t scan_idx;
    uint32_t next_switch_tick;
    bool nmea_recent;
    bool fix_valid;
    uint32_t last_valid_tick;
} gps_runtime_t;

static const uint8_t *gps_str[] = {"$GPGGA", "$GPGLL", "$GPRMC"};
static const uint32_t gps_baud_candidates[] = {9600, 38400, 115200, 4800, 19200, 57600};

static uint8_t gps_buf[GPS_LEN + 1];
static int gps_idx = 0;
static gps_runtime_t gps_rt;

static bool gps_parse_fix_status(uint8_t const *line, int len, bool *has_fix_field, bool *fix_valid)
{
    int comma_count = 0;

    if (!line || len < 10 || line[0] != '$') return false;
    if (!has_fix_field || !fix_valid) return false;

    *has_fix_field = false;
    *fix_valid = false;

    if (!strncmp((char const *)line, "$GPGGA", 6) || !strncmp((char const *)line, "$GNGGA", 6)) {
        for (int i = 0; i < len; i++) {
            if (line[i] != ',') continue;
            comma_count++;
            if (comma_count == 6) {
                int q = (i + 1 < len) ? line[i + 1] : '0';
                *has_fix_field = true;
                *fix_valid = (q >= '1' && q <= '8');
                return true;
            }
        }
        return true;
    }

    if (!strncmp((char const *)line, "$GPRMC", 6) || !strncmp((char const *)line, "$GNRMC", 6)) {
        for (int i = 0; i < len; i++) {
            if (line[i] != ',') continue;
            comma_count++;
            if (comma_count == 2) {
                int s = (i + 1 < len) ? line[i + 1] : 'V';
                *has_fix_field = true;
                *fix_valid = (s == 'A');
                return true;
            }
        }
        return true;
    }

    return false;
}

static bool gps_checksum_ok(uint8_t const *line, int len) {
    if (len < 7 || line[0] != '$') return false;
    int star = -1;
    for (int i = 1; i < len; i++) if (line[i] == '*') { star = i; break; }
    if (star < 1 || star + 2 >= len) return false;
    uint8_t sum = 0;
    for (int i = 1; i < star; i++) sum ^= line[i];
    int h1 = toupper(line[star+1]);
    int h2 = toupper(line[star+2]);
    if (!isxdigit(h1) || !isxdigit(h2)) return false;
    uint8_t got = (uint8_t)((h1 <= '9' ? h1 - '0' : h1 - 'A' + 10) << 4);
    got |= (uint8_t)(h2 <= '9' ? h2 - '0' : h2 - 'A' + 10);
    return got == sum;
}

static bool gps_valid_baud(uint32_t baud) {
    switch (baud) {case 0: case 4800: case 9600: case 19200: case 38400: case 57600: case 115200: return true; default: return false;}
}

static void gps_apply_baud(uint32_t baud){ uart_set_baudrate(uart1, baud); gps_rt.active_baud = baud; }
static void gps_power_on(void){}
static void gps_power_off(void){}
static bool gps_diag_exit_requested(tty_t *ttyp)
{
    if (!ttyp) return false;

    if (ttyp->tty_serial == TTY_USB) {
        if (!tud_cdc_available()) return false;
        return tud_cdc_read_char() == 0x03;
    }

    if (ttyp->tty_serial == TTY_UART0) {
        if (!uart_is_readable(uart0)) return false;
        return uart_getc(uart0) == 0x03;
    }

    return false;
}

void gps_init_runtime(void){
    if (!gps_valid_baud(param.gps_baud)) param.gps_baud = 0;
    gps_rt.enabled = param.gps_enabled ? true : false;
    gps_rt.baud_setting = param.gps_baud;
    gps_rt.last_good_baud = gps_valid_baud(param.gps_last_good_baud) && param.gps_last_good_baud ? param.gps_last_good_baud : 0;
    if (gps_rt.enabled) {
        gps_power_on();
        if (gps_rt.baud_setting == 0) { gps_rt.search_active = true; gps_rt.next_switch_tick = 0; }
        else { gps_apply_baud(gps_rt.baud_setting); }
    }
}

static uint32_t gps_next_candidate(void){
    if (gps_rt.scan_idx == 0 && gps_rt.last_good_baud) { gps_rt.scan_idx = 1; return gps_rt.last_good_baud; }
    uint32_t baud = gps_baud_candidates[(gps_rt.scan_idx ? gps_rt.scan_idx - 1 : 0) % (sizeof(gps_baud_candidates)/sizeof(gps_baud_candidates[0]))];
    gps_rt.scan_idx++;
    if (gps_rt.scan_idx > sizeof(gps_baud_candidates)/sizeof(gps_baud_candidates[0])) gps_rt.scan_idx = 1;
    return baud;
}

void gps_poll(void){
    if (!gps_rt.enabled) return;
    uint32_t now = tnc_time();
    if (gps_rt.baud_setting == 0 && gps_rt.search_active && (int32_t)(now - gps_rt.next_switch_tick) >= 0) {
        uint32_t baud = gps_next_candidate();
        gps_apply_baud(baud);
        gps_rt.next_switch_tick = now + GPS_SCAN_TICKS;
    }
    if (gps_rt.nmea_recent && (int32_t)(now - gps_rt.last_valid_tick) > GPS_NMEA_STALE_TICKS) gps_rt.nmea_recent = false;
}

void gps_input(int ch){
    if (!gps_rt.enabled) return;
    if (ch == '$') gps_idx = 0;
    if (gps_idx < GPS_LEN) gps_buf[gps_idx++] = ch;
    if (ch == '\n') {
        if ((param.gps <= 2) && !strncmp((char *)gps_buf, (char *)gps_str[param.gps], 6)) {
            static uint32_t gps_timer = 0;
            if (tnc_time() - gps_timer >= GPS_INTERVAL) { send_unproto(&tnc[GPS_PORT], gps_buf, gps_idx); gps_timer = tnc_time(); }
        }
        if (gps_checksum_ok(gps_buf, gps_idx)) {
            bool has_fix_field = false;
            bool fix_valid = false;

            gps_rt.nmea_recent = true;
            gps_rt.last_valid_tick = tnc_time();
            if (gps_rt.active_baud) { gps_rt.last_good_baud = gps_rt.active_baud; param.gps_last_good_baud = gps_rt.last_good_baud; }
            gps_rt.search_active = false;
            if (gps_parse_fix_status(gps_buf, gps_idx, &has_fix_field, &fix_valid) && has_fix_field) {
                gps_rt.fix_valid = fix_valid;
            }
        }
        gps_idx = 0;
    }
}

bool gps_set_enabled(bool enabled){
    gps_rt.enabled = enabled; param.gps_enabled = enabled ? 1 : 0;
    if (!enabled) { gps_power_off(); gps_rt.search_active = false; gps_rt.active_baud = 0; return true; }
    gps_power_on();
    if (gps_rt.baud_setting == 0) { gps_rt.search_active = true; gps_rt.next_switch_tick = 0; gps_rt.scan_idx = 0; }
    else { gps_apply_baud(gps_rt.baud_setting); gps_rt.search_active = false; }
    return true;
}

bool gps_set_baud_setting(uint32_t baud){
    if (!gps_valid_baud(baud)) return false;
    gps_rt.baud_setting = baud; param.gps_baud = baud;
    if (gps_rt.enabled) {
        if (baud == 0) { gps_rt.search_active = true; gps_rt.scan_idx = 0; gps_rt.next_switch_tick = 0; }
        else { gps_apply_baud(baud); gps_rt.search_active = false; }
    }
    return true;
}

uint32_t gps_get_baud_setting(void){ return gps_rt.baud_setting; }
uint32_t gps_get_active_baud(void){ return gps_rt.enabled ? gps_rt.active_baud : 0; }
uint32_t gps_get_last_good_baud(void){ return gps_rt.last_good_baud; }
bool gps_is_enabled(void){ return gps_rt.enabled; }
char const *gps_get_nmea_status(void){ if (!gps_rt.enabled) return "-"; if (gps_rt.nmea_recent) return "OK"; if (gps_rt.baud_setting == 0 && gps_rt.search_active) return "search"; return "waiting"; }
char const *gps_get_fix_status(void){ if (!gps_rt.enabled) return "-"; return gps_rt.fix_valid ? "FIX" : "NO FIX"; }

bool gps_diag(tty_t *ttyp){
    tty_write_str(ttyp, "GPS NMEA Diagnosis\r\nPress CTRL+C to exit.\r\n");
    uint8_t line[GPS_LEN + 1]; int idx = 0; bool warned = false;
    while (1) {
        if (gps_diag_exit_requested(ttyp)) break;
        if (gps_rt.baud_setting == 0) {
            for (size_t i=0;i<sizeof(gps_baud_candidates)/sizeof(gps_baud_candidates[0]);i++) {
                uint32_t b = (i==0 && gps_rt.last_good_baud)?gps_rt.last_good_baud:gps_baud_candidates[i];
                tty_write_str(ttyp, "GPS: trying "); char tmp[16]; snprintf(tmp,sizeof(tmp),"%lu",(unsigned long)b); tty_write_str(ttyp,tmp); tty_write_str(ttyp,"...\r\n");
                gps_apply_baud(b);
                absolute_time_t end = make_timeout_time_ms(GPS_DIAG_SCAN_TICKS*10);
                while (!time_reached(end)) {
                    if (gps_diag_exit_requested(ttyp)) goto done;
                    while (uart_is_readable(uart1)) {
                        int ch = uart_getc(uart1);
                        if (ch == '$') idx = 0;
                        if (idx < GPS_LEN) line[idx++] = (uint8_t)ch;
                        if (ch == '\n') { tty_write(ttyp, line, idx); if (gps_checksum_ok(line, idx)) { tty_write_str(ttyp, "GPS: valid NMEA detected at "); tty_write_str(ttyp, tmp); tty_write_str(ttyp, " baud\r\n"); } idx = 0; }
                    }
                }
            }
            if (!warned) { tty_write_str(ttyp, "GPS: no NMEA detected. Some GPS modules output only after fix.\r\nGPS: scan continues. Press CTRL+C to exit.\r\n"); warned = true; }
        } else {
            uint32_t b = gps_rt.baud_setting; char tmp[16]; snprintf(tmp,sizeof(tmp),"%lu",(unsigned long)b);
            tty_write_str(ttyp,"GPS: baud="); tty_write_str(ttyp,tmp); tty_write_str(ttyp,"\r\nGPS: listening...\r\n"); gps_apply_baud(b);
            while (1) {
                if (gps_diag_exit_requested(ttyp)) goto done;
                while (uart_is_readable(uart1)) { int ch = uart_getc(uart1); if (ch=='$') idx=0; if (idx<GPS_LEN) line[idx++]=(uint8_t)ch; if (ch=='\n'){ tty_write(ttyp,line,idx); idx=0; } }
            }
        }
    }
done:
    return true;
}
