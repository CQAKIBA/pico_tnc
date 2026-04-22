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
#include <string.h>
#include "pico/stdlib.h"
#include "class/cdc/cdc_device.h"
#include "pico/sync.h"
#include "hardware/uart.h"

#include "tnc.h"
#include "cmd.h"
#include "usb_output.h"
#include "tnc.h"
#include "usb_input.h"
#include "serial.h"
#include "unproto.h"
#include "kiss.h"

#define CONVERSE_PORT 0

// usb echo flag
//uint8_t usb_echo = 1; // on

// tty info
tty_t tty[TTY_N];

static const enum TTY_MODE tty_mode[] = {
    TTY_TERMINAL,
    TTY_TERMINAL,
    TTY_GPS,
};

static const enum TTY_SERIAL tty_serial[] = {
    TTY_USB,
    TTY_UART0,
    TTY_UART1,
};

#define BS '\b'
#define CR '\r'
#define DEL '\x7f'
#define BELL '\a'
#define CTRL_A '\x01'
#define CTRL_B '\x02'
#define CTRL_C '\x03'
#define CTRL_E '\x05'
#define CTRL_F '\x06'
#define CTRL_H '\x08'
#define CTRL_N '\x0e'
#define CTRL_P '\x10'
#define ESC '\x1b'
#define FEND 0xc0
#define SP ' '

#define KISS_TIMEOUT (1 * 100) // 1 sec
 
#define CAL_DATA_MAX 3
#define CMD_HISTORY_SLOTS 8
#define CMD_PROMPT "cmd: "

static const uint8_t calibrate_data[CAL_DATA_MAX] = {
    0x00, 0xff, 0x55,
};

static const char *calibrate_str[CAL_DATA_MAX] = {
    "send space (2200Hz)\r\n",
    "send mark  (1200Hz)\r\n",
    "send 0x55  (1200/2200Hz)\r\n",
};

static uint8_t cmd_history[CMD_HISTORY_SLOTS][CMD_BUF_LEN + 1];
static uint16_t cmd_history_len[CMD_HISTORY_SLOTS];
static uint8_t cmd_history_head = 0;
static uint8_t cmd_history_count = 0;

static uint8_t history_nav_active[TTY_N];
static int8_t history_nav_index[TTY_N];
static uint8_t history_nav_saved[TTY_N][CMD_BUF_LEN + 1];
static uint16_t history_nav_saved_len[TTY_N];

static void tty_rewind_and_clear_cmdline(tty_t *ttyp)
{
    int prompt_len = (int)strlen(CMD_PROMPT);
    int rewind = prompt_len + ttyp->cmd_cursor;
    int clear_len = prompt_len + ttyp->cmd_idx;

    while (rewind-- > 0) tty_write_char(ttyp, BS);
    while (clear_len-- > 0) tty_write_char(ttyp, SP);

    clear_len = prompt_len + ttyp->cmd_idx;
    while (clear_len-- > 0) tty_write_char(ttyp, BS);
}

static void tty_refresh_cmdline(tty_t *ttyp)
{
    if (!param.echo) return;

    tty_rewind_and_clear_cmdline(ttyp);
    tty_write_str(ttyp, CMD_PROMPT);
    tty_write(ttyp, ttyp->cmd_buf, ttyp->cmd_idx);
    tty_write_str(ttyp, "\x1b[K");
    if (ttyp->cmd_idx > ttyp->cmd_cursor) {
        char seq[24];
        int n = snprintf(seq, sizeof(seq), "\x1b[%dD", ttyp->cmd_idx - ttyp->cmd_cursor);
        if (n > 0) tty_write(ttyp, (uint8_t *)seq, n);
    }
}

static void tty_set_cmdline(tty_t *ttyp, uint8_t const *line, int len)
{
    if (len < 0) len = 0;
    if (len > CMD_BUF_LEN) len = CMD_BUF_LEN;
    if (len > 0 && line) memcpy(ttyp->cmd_buf, line, len);
    ttyp->cmd_idx = len;
    ttyp->cmd_cursor = len;
    ttyp->cmd_buf[len] = '\0';
    tty_refresh_cmdline(ttyp);
}

static void tty_move_cursor_left(tty_t *ttyp, int count)
{
    while (count-- > 0) tty_write_char(ttyp, BS);
}

static void tty_history_store(uint8_t const *line, int len)
{
    if (!line || len <= 0) return;
    if (len > CMD_BUF_LEN) len = CMD_BUF_LEN;

    if (cmd_history_count > 0) {
        uint8_t last = (cmd_history_head + CMD_HISTORY_SLOTS - 1) % CMD_HISTORY_SLOTS;
        if (cmd_history_len[last] == len &&
            memcmp(cmd_history[last], line, len) == 0) {
            return;
        }
    }

    memcpy(cmd_history[cmd_history_head], line, len);
    cmd_history[cmd_history_head][len] = '\0';
    cmd_history_len[cmd_history_head] = len;
    cmd_history_head = (cmd_history_head + 1) % CMD_HISTORY_SLOTS;
    if (cmd_history_count < CMD_HISTORY_SLOTS) cmd_history_count++;
}

void tty_history_push(uint8_t const *line, int len)
{
    tty_history_store(line, len);
}

static void tty_history_reset_nav(tty_t *ttyp)
{
    history_nav_active[ttyp->num] = 0;
    history_nav_index[ttyp->num] = -1;
    history_nav_saved_len[ttyp->num] = 0;
}

static void tty_history_prev(tty_t *ttyp)
{
    int tty_id = ttyp->num;
    uint8_t oldest;
    uint8_t idx;

    if (cmd_history_count == 0) {
        if (param.echo) tty_write_char(ttyp, BELL);
        return;
    }

    if (!history_nav_active[tty_id]) {
        history_nav_active[tty_id] = 1;
        history_nav_index[tty_id] = cmd_history_head;
        history_nav_saved_len[tty_id] = ttyp->cmd_idx;
        memcpy(history_nav_saved[tty_id], ttyp->cmd_buf, ttyp->cmd_idx);
        history_nav_saved[tty_id][ttyp->cmd_idx] = '\0';
    }

    oldest = (cmd_history_head + CMD_HISTORY_SLOTS - cmd_history_count) % CMD_HISTORY_SLOTS;
    if (history_nav_index[tty_id] == oldest) {
        if (param.echo) tty_write_char(ttyp, BELL);
        return;
    }

    history_nav_index[tty_id] = (history_nav_index[tty_id] + CMD_HISTORY_SLOTS - 1) % CMD_HISTORY_SLOTS;
    idx = (uint8_t)history_nav_index[tty_id];
    tty_set_cmdline(ttyp, cmd_history[idx], cmd_history_len[idx]);
}

static void tty_history_next(tty_t *ttyp)
{
    int tty_id = ttyp->num;
    uint8_t idx;

    if (!history_nav_active[tty_id]) {
        if (param.echo) tty_write_char(ttyp, BELL);
        return;
    }

    if (history_nav_index[tty_id] == cmd_history_head) {
        if (param.echo) tty_write_char(ttyp, BELL);
        return;
    }

    history_nav_index[tty_id] = (history_nav_index[tty_id] + 1) % CMD_HISTORY_SLOTS;
    if (history_nav_index[tty_id] == cmd_history_head) {
        tty_set_cmdline(ttyp, history_nav_saved[tty_id], history_nav_saved_len[tty_id]);
        history_nav_active[tty_id] = 0;
        return;
    }

    idx = (uint8_t)history_nav_index[tty_id];
    tty_set_cmdline(ttyp, cmd_history[idx], cmd_history_len[idx]);
}

static void tty_insert_char(tty_t *ttyp, uint8_t ch)
{
    int old_cursor;
    int tail_len;
    int move_back;

    if (!(ch >= ' ' && ch <= '~')) {
        if (param.echo) tty_write_char(ttyp, BELL);
        return;
    }
    if (ttyp->cmd_idx >= CMD_BUF_LEN) return;

    if (ttyp->cmd_cursor == ttyp->cmd_idx) {
        ttyp->cmd_buf[ttyp->cmd_idx++] = ch;
        ttyp->cmd_cursor = ttyp->cmd_idx;
        ttyp->cmd_buf[ttyp->cmd_idx] = '\0';
        if (param.echo) tty_write_char(ttyp, ch);
        return;
    }

    old_cursor = ttyp->cmd_cursor;
    memmove(&ttyp->cmd_buf[old_cursor + 1],
            &ttyp->cmd_buf[old_cursor],
            ttyp->cmd_idx - old_cursor);
    ttyp->cmd_buf[old_cursor] = ch;
    ttyp->cmd_idx++;
    ttyp->cmd_cursor = old_cursor + 1;
    ttyp->cmd_buf[ttyp->cmd_idx] = '\0';

    if (param.echo) {
        tail_len = ttyp->cmd_idx - old_cursor;
        move_back = ttyp->cmd_idx - ttyp->cmd_cursor;
        tty_write(ttyp, &ttyp->cmd_buf[old_cursor], tail_len);
        tty_move_cursor_left(ttyp, move_back);
    }
}
static void tty_backspace(tty_t *ttyp)
{
    if (ttyp->cmd_cursor <= 0) {
        if (param.echo) tty_write_char(ttyp, BELL);
        return;
    }

    if (ttyp->cmd_cursor == ttyp->cmd_idx) {
        --ttyp->cmd_cursor;
        --ttyp->cmd_idx;
        ttyp->cmd_buf[ttyp->cmd_idx] = '\0';
        if (param.echo) tty_write_str(ttyp, "\b \b");
        return;
    }

    if (param.echo) {
        /* Move physical cursor to the deleted character position first. */
        tty_write_str(ttyp, "\b");
    }

    memmove(&ttyp->cmd_buf[ttyp->cmd_cursor - 1],
            &ttyp->cmd_buf[ttyp->cmd_cursor],
            ttyp->cmd_idx - ttyp->cmd_cursor);

    --ttyp->cmd_cursor;
    --ttyp->cmd_idx;
    ttyp->cmd_buf[ttyp->cmd_idx] = '\0';

    if (param.echo) {
        int tail_len = ttyp->cmd_idx - ttyp->cmd_cursor;

        if (tail_len > 0) {
            tty_write(ttyp, &ttyp->cmd_buf[ttyp->cmd_cursor], tail_len);
        }
        tty_write_char(ttyp, SP);
        tty_move_cursor_left(ttyp, tail_len + 1);
    }
}
static void tty_delete_at_cursor(tty_t *ttyp)
{
    if (ttyp->cmd_cursor >= ttyp->cmd_idx) {
        if (param.echo) tty_write_char(ttyp, BELL);
        return;
    }

    memmove(&ttyp->cmd_buf[ttyp->cmd_cursor],
            &ttyp->cmd_buf[ttyp->cmd_cursor + 1],
            ttyp->cmd_idx - ttyp->cmd_cursor - 1);

    --ttyp->cmd_idx;
    ttyp->cmd_buf[ttyp->cmd_idx] = '\0';

    if (param.echo) {
        int tail_len = ttyp->cmd_idx - ttyp->cmd_cursor;

        if (tail_len > 0) {
            tty_write(ttyp, &ttyp->cmd_buf[ttyp->cmd_cursor], tail_len);
        }
        tty_write_char(ttyp, SP);
        tty_move_cursor_left(ttyp, tail_len + 1);
    }
}
static bool tty_handle_ansi_sequence(tty_t *ttyp, int ch)
{
    if (ttyp->esc_state == 0) return false;

    if (ttyp->esc_state == 1) {
        if (ch == '[' || ch == 'O') {
            ttyp->esc_state = 2;
            ttyp->esc_prefix = ch;
            ttyp->esc_param = 0;
            return true;
        }
        ttyp->esc_state = 0;
        return false;
    }

    if (ttyp->esc_state != 2) {
        ttyp->esc_state = 0;
        return false;
    }

    if (ch >= '0' && ch <= '9') {
        ttyp->esc_param = (uint8_t)(ttyp->esc_param * 10 + (ch - '0'));
        return true;
    }
    if (ch == ';') return true;

    switch (ch) {
        case 'A':
            tty_history_prev(ttyp);
            break;
        case 'B':
            tty_history_next(ttyp);
            break;
        case 'C':
            if (ttyp->cmd_cursor < ttyp->cmd_idx) {
                ttyp->cmd_cursor++;
                if (param.echo) tty_write_str(ttyp, "\x1b[C");
            } else if (param.echo) tty_write_char(ttyp, BELL);
            break;
        case 'D':
            if (ttyp->cmd_cursor > 0) {
                ttyp->cmd_cursor--;
                if (param.echo) tty_write_str(ttyp, "\x1b[D");
            } else if (param.echo) tty_write_char(ttyp, BELL);
            break;
        case 'H':
            ttyp->cmd_cursor = 0;
            tty_refresh_cmdline(ttyp);
            break;
        case 'F':
            ttyp->cmd_cursor = ttyp->cmd_idx;
            tty_refresh_cmdline(ttyp);
            break;
        case '~':
            if (ttyp->esc_param == 1 || ttyp->esc_param == 7) {
                ttyp->cmd_cursor = 0;
                tty_refresh_cmdline(ttyp);
            } else if (ttyp->esc_param == 4 || ttyp->esc_param == 8) {
                ttyp->cmd_cursor = ttyp->cmd_idx;
                tty_refresh_cmdline(ttyp);
            } else if (ttyp->esc_param == 3) {
                tty_delete_at_cursor(ttyp);
            }
            break;
        default:
            break;
    }

    ttyp->esc_state = 0;
    return true;
}

void tty_init(void)
{
    memset(history_nav_index, -1, sizeof(history_nav_index));

    for (int i = 0; i < TTY_N; i++) {
        tty_t *ttyp = &tty[i];

        ttyp->num = i;

        ttyp->tty_mode = tty_mode[i];
        ttyp->tty_serial = tty_serial[i];

        ttyp->kiss_mode = false;
        ttyp->cmd_idx = 0;
        ttyp->cmd_cursor = 0;
        ttyp->esc_state = 0;
        ttyp->esc_prefix = 0;
        ttyp->esc_param = 0;
    }
}

void tty_write(tty_t *ttyp, uint8_t const *data, int len)
{
    if (ttyp->tty_serial == TTY_USB) {
        usb_write(data, len);
        return;
    }

    if (ttyp->tty_serial == TTY_UART0) serial_write(data, len);
}

void tty_write_char(tty_t *ttyp, uint8_t ch)
{
    if (ttyp->tty_serial == TTY_USB) {
        usb_write_char(ch);
        return;
    }

    if (ttyp->tty_serial == TTY_UART0) serial_write_char(ch);
}

void tty_write_str(tty_t *ttyp, uint8_t const *str)
{
    int len = strlen((char const *)str);

    tty_write(ttyp, str, len);
}

void tty_input(tty_t *ttyp, int ch)
{
    if (ttyp->kiss_state != KISS_OUTSIDE) {

        // inside KISS frame
        if (tnc_time() - ttyp->kiss_timeout < KISS_TIMEOUT) {
            kiss_input(ttyp, ch);
            return;
        }
        // timeout, exit kiss frame
        ttyp->kiss_state = KISS_OUTSIDE;
    }

    // calibrate mode
    if (calibrate_mode) {
        tnc_t *tp = &tnc[0];

        switch (ch) {
            case SP: // toggle mark/space
                if (++calibrate_idx >= CAL_DATA_MAX) calibrate_idx = 0;
                tp->cal_data = calibrate_data[calibrate_idx];
                tty_write_str(ttyp, (uint8_t const *)calibrate_str[calibrate_idx]);
                tp->cal_time = tnc_time();
                break;

            case CTRL_C:
                tp->send_state = SP_CALIBRATE_OFF;
                break;

            default:
                tty_write_char(ttyp, BELL);
        }
        return;
    }

    if (cmd_has_pending_input()) {
        if (cmd_consume_pending_input(ttyp, ch)) {
            ttyp->cmd_idx = 0;
            ttyp->cmd_cursor = 0;
            return;
        }
    }

    if (ttyp->esc_state && tty_handle_ansi_sequence(ttyp, ch)) return;

    switch (ch) {
        case FEND: // KISS frame end
            kiss_input(ttyp, ch);
            break;

        case ESC:
            ttyp->esc_state = 1;
            break;

        case BS: //(BS|CTRL_H)
        case DEL:
            tty_history_reset_nav(ttyp);
            tty_backspace(ttyp);
            break;

        case CTRL_P:
            tty_history_prev(ttyp);
            break;

        case CTRL_N:
            tty_history_next(ttyp);
            break;

        case CTRL_B:
            if (ttyp->cmd_cursor > 0) {
                ttyp->cmd_cursor--;
                if (param.echo) tty_write_str(ttyp, (uint8_t const *)"\x1b[D");
            } else if (param.echo) tty_write_char(ttyp, BELL);
            break;

        case CTRL_F:
            if (ttyp->cmd_cursor < ttyp->cmd_idx) {
                ttyp->cmd_cursor++;
                if (param.echo) tty_write_str(ttyp, (uint8_t const *)"\x1b[C");
            } else if (param.echo) tty_write_char(ttyp, BELL);
            break;

        case CTRL_A:
            ttyp->cmd_cursor = 0;
            tty_refresh_cmdline(ttyp);
            break;

        case CTRL_E:
            ttyp->cmd_cursor = ttyp->cmd_idx;
            tty_refresh_cmdline(ttyp);
            break;

        case CR:
            if (param.echo) tty_write_str(ttyp, (uint8_t const *)"\r\n");
            if (ttyp->cmd_idx > 0) {
                ttyp->cmd_buf[ttyp->cmd_idx] = '\0';
                tty_history_store(ttyp->cmd_buf, ttyp->cmd_idx);
                if (converse_mode) {
                    send_unproto(&tnc[CONVERSE_PORT], ttyp->cmd_buf, ttyp->cmd_idx); // send UI packet
                } else {
                    cmd(ttyp, ttyp->cmd_buf, ttyp->cmd_idx);
                }
            }
            cmd_emit_prompt_if_idle(ttyp);
            ttyp->cmd_idx = 0;
            ttyp->cmd_cursor = 0;
            tty_history_reset_nav(ttyp);
            break;

        case CTRL_C:
            if (converse_mode) {
                converse_mode = false;
            }
            tty_write_str(ttyp, (uint8_t const *)"\r\n");
            cmd_emit_prompt_if_idle(ttyp);
            ttyp->cmd_idx = 0;
            ttyp->cmd_cursor = 0;
            tty_history_reset_nav(ttyp);
            break;

        default:
            tty_history_reset_nav(ttyp);
            tty_insert_char(ttyp, ch);
            break;
    }
}
