#include "help.h"

#include <ctype.h>
#include <strings.h>
#include <string.h>

#include "sjis_level1_tablehelp.h"

static const char *const help_lines_en[] = {
    "",
    "JAPANESE HELP: help ja sjis | help ja utf8",
    "Commands are Case Insensitive",
    "Use Backspace Key (BS) for Correction",
    "Use the DISP command to desplay all options",
    "Connect GPS for APRS Operation, (GP4/GP5/9600bps)",
    "Connect to Terminal for Command Interpreter, (USB serial or GP0/GP1/115200bps)",
    "",
    "Commands (with example):",
    "MYCALL (mycall jn1dff-2)",
    "UNPROTO (unproto jn1dff-14 v jn1dff-1) - 3 digis max",
    "BTEXT (btext Bob)-100 chars max",
    "BEACON (beacon every n)- n=0 is off and 1<n<60 mins",
    "MONitor (mon all,mon me, or mon off)",
    "DIGIpeat (digi on or digi off)",
    "MYALIAS (myalias RELAY)",
    "PERM (PERM)",
    "ECHO (echo on or echo off)",
    "GPS (gps $GPGGA or gps $GPGLL or gps $GPRMC)",
    "TRace (tr xmit or tr rcv) - For debugging only",
    "TXDELAY (txdelay n 0<n<201 n is number of delay flags to send)",
    "CALIBRATE (Calibrate Mode - Testing Only)",
    "CONverse (con)",
    "",
    NULL,
};

static tty_t *help_ttyp;
static int help_line_idx;
static bool help_active;
static bool help_ok_pending;
static bool help_use_sjis;
static const char *const *help_lines;

static bool is_eol_or_space(int ch)
{
    return ch == '\0' || isspace(ch);
}

static uint8_t *skip_spaces(uint8_t *p)
{
    while (*p && isspace(*p)) p++;
    return p;
}

bool help_handle_command(tty_t *ttyp, uint8_t *buf, int len)
{
    (void)len;

    help_lines = help_lines_en;
    help_use_sjis = false;

    if (buf && *buf) {
        uint8_t *p = skip_spaces(buf);

        if (strncasecmp((const char *)p, "ja", 2) || !is_eol_or_space(p[2])) {
            return false;
        }

        p += 2;
        p = skip_spaces(p);

        help_lines = sjis_help_lines_ja_utf8;
        help_use_sjis = true;

        if (*p) {
            if (!strcasecmp((const char *)p, "sjis")) {
                help_use_sjis = true;
            } else if (!strcasecmp((const char *)p, "utf8")) {
                help_use_sjis = false;
            } else {
                return false;
            }
        }
    }

    help_ttyp = ttyp;
    help_line_idx = 0;
    help_active = true;
    help_ok_pending = false;

    return true;
}

bool cmd_help(tty_t *ttyp, uint8_t *buf, int len)
{
    return help_handle_command(ttyp, buf, len);
}

void help_poll(void)
{
    if (help_active) {
        const char *line = help_lines[help_line_idx];

        if (line) {
            if (help_use_sjis) {
                uint8_t sjis_line_buf[256];
                int sjis_len = sjis_level1_utf8_to_sjis_line(line, sjis_line_buf, sizeof(sjis_line_buf));
                tty_write(help_ttyp, sjis_line_buf, sjis_len);
            } else {
                tty_write_str(help_ttyp, line);
            }
            tty_write_str(help_ttyp, "\r\n");
            help_line_idx++;
            return;
        }

        help_active = false;
        help_ok_pending = true;
        return;
    }

    if (help_ok_pending) {
        tty_write_str(help_ttyp, "\r\nOK\r\n");
        help_ok_pending = false;
        help_ttyp = NULL;
    }
}

bool help_is_response_pending(void)
{
    return help_active || help_ok_pending;
}
