#include "help.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <strings.h>
#include <string.h>

#include "sjis_level1_table.h"
#include "tnc.h"

static const char *const help_lines_en[] = {
    "",
    "JAPANESE HELP: help ja sjis | help ja utf8",
    "",
    "Commands are Case Insensitive",
    "Use Backspace Key (BS) for Correction",
    "Use the DISP command to desplay all options",
    "Connect GPS for APRS Operation, (GP4/GP5/9600bps)",
    "Connect to Terminal for Command Interpreter, (USB serial or GP0/GP1/115200bps)",
    "",
    "Commands (with example):",
    "",
    "=== Station ===",
    "MYCALL (mycall jn1dff-2)",
    "MYALIAS (myalias RELAY)",
    "",
    "=== Network ===",
    "UNPROTO (unproto jn1dff-14 v jn1dff-1) - 3 digis max",
    "MONitor (mon all,mon me, or mon off)",
    "",
    "=== Operation ===",
    "CONverse (con)",
    "ABOUT (about) - Version Information",
    "",
    "=== Auto Operation ===",
    "BTEXT (btext Bob)-100 chars max",
    "BEACON (beacon every n)- n=0 is off and 1<n<60 mins",
    "DIGIpeat (digi on or digi off)",
    "",
    "=== GPS / Sensor ===",
    "GPS (gps $GPGGA or gps $GPGLL or gps $GPRMC)",
    "",
    "=== Signature / QSL ===",
    "PRIVKEY (privkey show, privkey gen [m|p|mona1|p2pkh|p2sh|p2wpkh], privkey set [m|p|mona1|p2pkh|p2sh|p2wpkh|WIF|RAW])",
    "SIGN (sign msg <text>, sign qsl ...)",
    "",
    "=== Hardware ===",
    "TXDELAY (txdelay n|nms|ns, 0..1000ms; unitless n keeps legacy 10ms units)",
    "AXDELAY (axdelay n|nms|ns, 0..1000ms; unitless n keeps legacy 10ms units)",
    "AXHANG (axhang n|nms|ns, 0..1000ms; unitless n keeps legacy 10ms units)",
    "SYSTEM (system usb_bootloader) - guarded reboot to USB BOOTSEL mode",
    "",
    "=== Diagnostics ===",
    "CALIBRATE (Calibrate Mode - Testing Only)",
    "TERMTEST (termtest) - Raw terminal byte inspector (Ctrl+C to exit)",
    "ECHO (echo on or echo off)",
    "PERM (PERM)",
    NULL,
};

static const char *const help_lines_ja_utf8[] = {
    "",
    "コマンドは大文字小文字を区別しません",
    "訂正には Backspace キー(BS)を使います",
    "DISP コマンドで全ての設定を表示できます",
    "APRS運用時は GPS を接続してください (GP4/GP5/9600bps)",
    "コマンド入力は USBシリアル または GP0/GP1/115200bps を使います",
    "",
    "コマンド一覧 (設定例):",
    "",
    "=== Station ===",
    "MYCALL (mycall jn1dff-2) # 自局名",
    "MYALIAS (myalias RELAY) # 自局名のエイリアス",
    "",
    "=== Network ===",
    "UNPROTO (unproto jn1dff-14 v jn1dff-1) # 宛先.経路設定 デジピータは最大3局",
    "MONitor (mon all, mon me, または mon off) # 受信パケットフィルタ",
    "",
    "=== Operation ===",
    "CONverse (con) # UIフレーム テキストチャット",
    "",
    "=== Auto Operation ===",
    "BTEXT (btext Bob) # ビーコンテキスト 最大100文字",
    "BEACON (beacon every n) # 自局の存在を広告するためのビーコン n=0で停止, 1<n<60分",
    "DIGIpeat (digi on または digi off) # 中継機能",
    "",
    "=== GPS / Sensor ===",
    "GPS (gps $GPGGA / gps $GPGLL / gps $GPRMC) # GPS(APRS)パケット設定",
    "",
    "=== Signature / QSL ===",
    "PRIVKEY GEN  (privkey gen [m|p|mona1|p2pkh|p2sh|p2wpkh]) # 秘密鍵の生成",
    "PRIVKEY SET  (privkey set [m|p|mona1|p2pkh|p2sh|p2wpkh|WIFまたはRAW]) # 秘密鍵のインポート",
    "PRIVKEY SHOW (privkey show) # 秘密鍵の表示",
    "SIGN MSG     (sign msg <text>) # テキスト署名して送信待機",
    "SIGN QSL     (sign qsl [args]) # QSLテンプレート署名(引数またはウィザード)",
    "",
    "=== Hardware ===",
    "TXDELAY (txdelay n|nms|ns, 0..1000ms; 単位なしnは従来10ms単位) # 信号波送出前のPTT保持時間",
    "AXDELAY (axdelay n|nms|ns, 0..1000ms; 単位なしnは従来10ms単位) # プリアンブル送出時間",
    "AXHANG (axhang n|nms|ns, 0..1000ms; 単位なしnは従来10ms単位)   # 信号波送出後のPTT保持時間",
    "SYSTEM (system usb_bootloader) # 10秒以内に Y/E/S/Enter を順に入力すると USB BOOTSEL へ移行",
    "",
    "=== Diagnostics ===",
    "CALIBRATE (calibrate) # 送信調整用トーン出力)",
    "TERMTEST (termtest) # 受信バイトを生表示する端末検査モード (Ctrl+Cで終了)",
    "ECHO (echo on または echo off) # 入力文字のエコーバック",
    "PERM (perm) # 設定値をFlashROMに保存",
    "ABOUT (about) # バージョン情報",
    NULL,
};

static const char *const help_warning_lines_en[] = {
    "Warning: Please set MYCALL and UNPROTO.",
    "MYCALL is your radio station's call sign.",
    "UNPROTO is the destination; if not specified, use CQ.",
    NULL,
};

static const char *const help_warning_lines_ja_utf8[] = {
    "警告: MYCALL と UNPROTO を設定してください。",
    "MYCALL あなたの無線局のコールサイン。",
    "UNPROTO 宛先、指定無き場合はCQ。",
    NULL,
};

static tty_t *help_ttyp;
static int help_line_idx;
static int help_warning_line_idx;
static bool help_active;
static bool help_ok_pending;
static bool help_use_sjis;
static bool help_show_config_warning;
static const char *const *help_lines;
static const char *const *help_warning_lines;
static bool help_warning_gap_pending = false;

static bool is_eol_or_space(int ch)
{
    return ch == '\0' || isspace(ch);
}

static uint8_t *skip_spaces(uint8_t *p)
{
    while (*p && isspace(*p)) p++;
    return p;
}

static uint16_t lookup_sjis_from_level1(uint16_t unicode)
{
    for (size_t i = 0; i < utf8_to_sjis_level1_table_count; i++) {
        if (utf8_to_sjis_level1_table[i].unicode == unicode) {
            return utf8_to_sjis_level1_table[i].sjis;
        }
    }

    return 0;
}

static int decode_utf8_char(const char *s, uint16_t *codepoint)
{
    const uint8_t *p = (const uint8_t *)s;

    if (p[0] < 0x80) {
        *codepoint = p[0];
        return 1;
    }

    if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        *codepoint = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
        return 2;
    }

    if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        *codepoint = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        return 3;
    }

    *codepoint = '?';
    return 1;
}

static int utf8_to_sjis_line(const char *utf8, uint8_t *sjis_buf, int sjis_buf_len)
{
    int in_idx = 0;
    int out_idx = 0;

    if (sjis_buf_len <= 0) return 0;

    while (utf8[in_idx] && out_idx < sjis_buf_len - 1) {
        uint16_t unicode;
        uint16_t sjis;
        int consumed = decode_utf8_char(&utf8[in_idx], &unicode);

        in_idx += consumed;

        if (unicode < 0x80) {
            sjis = unicode;
        } else {
            sjis = lookup_sjis_from_level1(unicode);
            if (!sjis) {
                sjis = '?';
            }
        }

        if (sjis <= 0xFF) {
            sjis_buf[out_idx++] = (uint8_t)sjis;
        } else {
            if (out_idx + 2 > sjis_buf_len - 1) break;
            sjis_buf[out_idx++] = (uint8_t)((sjis >> 8) & 0xFF);
            sjis_buf[out_idx++] = (uint8_t)(sjis & 0xFF);
        }
    }

    sjis_buf[out_idx] = '\0';

    return out_idx;
}

bool help_handle_command(tty_t *ttyp, uint8_t *buf, int len)
{
    (void)len;

    help_lines = help_lines_en;
    help_warning_lines = help_warning_lines_en;
    help_use_sjis = false;

    if (buf && *buf) {
        uint8_t *p = skip_spaces(buf);

        if (strncasecmp((const char *)p, "ja", 2) || !is_eol_or_space(p[2])) {
            return false;
        }

        p += 2;
        p = skip_spaces(p);

        help_lines = help_lines_ja_utf8;
        help_warning_lines = help_warning_lines_ja_utf8;
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
    help_warning_line_idx = 0;
    help_active = true;
    help_ok_pending = false;
    help_warning_gap_pending = false;
    help_show_config_warning = !param.mycall.call[0] || !param.unproto[0].call[0];

    return true;
}

bool cmd_help(tty_t *ttyp, uint8_t *buf, int len)
{
    return help_handle_command(ttyp, buf, len);
}

void help_poll(void)
{
    if (help_active) {
        const char *line;

        line = help_lines[help_line_idx];
        if (line) {
            if (help_use_sjis) {
                uint8_t sjis_line_buf[256];
                int sjis_len = utf8_to_sjis_line(line, sjis_line_buf, sizeof(sjis_line_buf));
                tty_write(help_ttyp, sjis_line_buf, sjis_len);
            } else {
                tty_write_str(help_ttyp, line);
            }
            tty_write_str(help_ttyp, "\r\n");
            help_line_idx++;
            return;
        }

        if (help_show_config_warning && !help_warning_gap_pending) {
            help_warning_gap_pending = true;
            tty_write_str(help_ttyp, "\r\n");
            return;
        }

        if (help_show_config_warning) {
            line = help_warning_lines[help_warning_line_idx];
            if (line) {
                if (help_use_sjis) {
                    uint8_t sjis_line_buf[256];
                    int sjis_len = utf8_to_sjis_line(line, sjis_line_buf, sizeof(sjis_line_buf));
                    tty_write(help_ttyp, sjis_line_buf, sjis_len);
                } else {
                    tty_write_str(help_ttyp, line);
                }
                tty_write_str(help_ttyp, "\r\n");
                help_warning_line_idx++;
                return;
            }

            help_show_config_warning = false;
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
