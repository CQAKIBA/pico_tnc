#include "sjis_level1_tablehelp.h"

#include <stdbool.h>
#include <stddef.h>

#include "sjis_level1_table.h"

const char *const sjis_help_lines_ja_utf8[] = {
    "",
    "コマンドは大文字小文字を区別しません",
    "訂正には Backspace キー(BS)を使います",
    "DISP コマンドで全ての設定を表示できます",
    "APRS運用時は GPS を接続してください (GP4/GP5/9600bps)",
    "コマンド入力は USBシリアル または GP0/GP1/115200bps を使います",
    "",
    "コマンド一覧 (設定例):",
    "MYCALL (mycall jn1dff-2)",
    "UNPROTO (unproto jn1dff-14 v jn1dff-1) - デジピータは最大3局",
    "BTEXT (btext Bob) - 最大100文字",
    "BEACON (beacon every n) - n=0で停止, 1<n<60分",
    "MONitor (mon all, mon me, または mon off)",
    "DIGIpeat (digi on または digi off)",
    "MYALIAS (myalias RELAY)",
    "PERM (perm)",
    "ECHO (echo on または echo off)",
    "GPS (gps $GPGGA / gps $GPGLL / gps $GPRMC)",
    "TRace (tr xmit / tr rcv) - デバッグ専用",
    "TXDELAY (txdelay n 0<n<201, nは送信前フラグ数)",
    "CALIBRATE (calibrateモード - テスト専用)",
    "CONverse (con)",
    "",
    NULL,
};

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

static bool encode_non_kanji_to_sjis(uint16_t unicode, uint16_t *sjis)
{
    if (unicode < 0x80) {
        *sjis = unicode;
        return true;
    }

    if (unicode >= 0x3041 && unicode <= 0x3093) {
        *sjis = (uint16_t)(0x829F + (unicode - 0x3041));
        return true;
    }

    switch (unicode) {
        case 0x3001: *sjis = 0x8141; return true; // 、
        case 0x3002: *sjis = 0x8142; return true; // 。
        case 0x30FC: *sjis = 0x815B; return true; // ー
        case 0x30FB: *sjis = 0x8145; return true; // ・
        case 0x300C: *sjis = 0x8175; return true; // 「
        case 0x300D: *sjis = 0x8176; return true; // 」
        case 0x00D7: *sjis = 0x817E; return true; // ×
        default:
            break;
    }

    return false;
}

int sjis_level1_utf8_to_sjis_line(const char *utf8, uint8_t *sjis_buf, int sjis_buf_len)
{
    int in_idx = 0;
    int out_idx = 0;

    if (sjis_buf_len <= 0) return 0;

    while (utf8[in_idx] && out_idx < sjis_buf_len - 1) {
        uint16_t unicode;
        uint16_t sjis;
        int consumed = decode_utf8_char(&utf8[in_idx], &unicode);

        in_idx += consumed;

        sjis = lookup_sjis_from_level1(unicode);
        if (!sjis && !encode_non_kanji_to_sjis(unicode, &sjis)) {
            sjis = '?';
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
