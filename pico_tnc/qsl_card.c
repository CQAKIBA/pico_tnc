#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "qsl_card.h"

#define QSL_CARD_LINE_BUF 64

static uint8_t qsl_line_buf[QSL_CARD_LINE_BUF];

static const char *skip_ws(const char *p)
{
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

static void card_write_line(tty_t *ttyp, const char *text)
{
    int n = snprintf((char *)qsl_line_buf, QSL_CARD_LINE_BUF, "  | %-46.46s |\r\n", text ? text : "");
    tty_write(ttyp, qsl_line_buf, n);
}

static const char *find_qsl_object(const char *json)
{
    const char *p = json;
    bool in_string = false;
    bool esc = false;

    while (*p) {
        if (in_string) {
            if (esc) esc = false;
            else if (*p == '\\') esc = true;
            else if (*p == '"') in_string = false;
            p++;
            continue;
        }
        if (*p == '"') {
            if (!strncmp(p, "\"QSL\"", 5)) {
                const char *q = skip_ws(p + 5);
                if (*q != ':') {
                    p++;
                    continue;
                }
                q = skip_ws(q + 1);
                if (*q == '{') return q;
            }
            in_string = true;
        }
        p++;
    }
    return NULL;
}

static const char *json_copy_string(const char *p, char *out, int out_sz)
{
    int n = 0;
    bool esc = false;

    if (*p != '"') return NULL;
    p++;
    while (*p) {
        char ch = *p++;
        if (esc) {
            if (n < out_sz - 1) out[n++] = ch;
            esc = false;
            continue;
        }
        if (ch == '\\') {
            esc = true;
            continue;
        }
        if (ch == '"') {
            out[n] = '\0';
            return p;
        }
        if (n < out_sz - 1) out[n++] = ch;
    }
    return NULL;
}

static const char *json_value_end(const char *p)
{
    int depth = 0;
    bool in_string = false;
    bool esc = false;

    while (*p) {
        char ch = *p;
        if (in_string) {
            if (esc) esc = false;
            else if (ch == '\\') esc = true;
            else if (ch == '"') in_string = false;
            p++;
            continue;
        }
        if (ch == '"') {
            in_string = true;
            p++;
            continue;
        }
        if (ch == '{' || ch == '[') depth++;
        else if (ch == '}' || ch == ']') {
            if (depth == 0) return p;
            depth--;
        } else if (ch == ',' && depth == 0) return p;
        p++;
    }
    return p;
}

static void qsl_value_set(char *dst, int dst_sz, const char *src)
{
    if (!dst || dst_sz <= 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_sz, "%s", src);
}

bool qsl_card_parse(const char *json, qsl_card_t *card)
{
    const char *p;

    if (!json || !card) return false;

    memset(card, 0, sizeof(*card));
    p = find_qsl_object(json);
    if (!p) return false;

    p++;
    while (*p) {
        char key[8];
        char val[48];
        const char *vend;

        p = skip_ws(p);
        if (*p == '}') {
            card->has_qsl = true;
            return true;
        }
        if (*p != '"') return false;
        p = json_copy_string(p, key, sizeof(key));
        if (!p) return false;
        p = skip_ws(p);
        if (*p != ':') return false;
        p = skip_ws(p + 1);
        if (*p == '"') {
            p = json_copy_string(p, val, sizeof(val));
            if (!p) return false;
        } else {
            vend = json_value_end(p);
            snprintf(val, sizeof(val), "%.*s", (int)(vend - p), p);
            p = vend;
        }

        if (!strcmp(key, "C")) qsl_value_set(card->to_call, sizeof(card->to_call), val);
        else if (!strcmp(key, "S")) qsl_value_set(card->report, sizeof(card->report), val);
        else if (!strcmp(key, "D")) qsl_value_set(card->date, sizeof(card->date), val);
        else if (!strcmp(key, "T")) qsl_value_set(card->time, sizeof(card->time), val);
        else if (!strcmp(key, "F")) qsl_value_set(card->freq, sizeof(card->freq), val);
        else if (!strcmp(key, "M")) qsl_value_set(card->mode, sizeof(card->mode), val);
        else if (!strcmp(key, "P")) qsl_value_set(card->qth, sizeof(card->qth), val);
        else if (card->ext_n < QSL_CARD_EXT_MAX) {
            snprintf(card->ext[card->ext_n], sizeof(card->ext[card->ext_n]), "%s: %s", key, val);
            card->ext_n++;
        }

        p = skip_ws(p);
        if (*p == ',') p++;
    }

    return false;
}

void qsl_card_render(tty_t *ttyp, const qsl_card_t *card, const char *from, const char *addr, const char *sig_b64, const char *status)
{
    char line[80];

    if (!ttyp || !card || !from || !addr || !sig_b64 || !status) return;

    tty_write_str(ttyp, "\r\n");
    tty_write_str(ttyp, "  +------------------------------------------------+\r\n");
    card_write_line(ttyp, "Digitally Signed QSL Card");
    tty_write_str(ttyp, "  +------------------------------------------------+\r\n");
    snprintf(line, sizeof(line), "From     : %s", from);
    card_write_line(ttyp, line);
    snprintf(line, sizeof(line), "To Call  : %s", card->to_call);
    card_write_line(ttyp, line);
    snprintf(line, sizeof(line), "Report   : %s", card->report);
    card_write_line(ttyp, line);
    snprintf(line, sizeof(line), "Date     : %s", card->date);
    card_write_line(ttyp, line);
    snprintf(line, sizeof(line), "Time     : %s", card->time);
    card_write_line(ttyp, line);
    snprintf(line, sizeof(line), "Freq     : %s", card->freq);
    card_write_line(ttyp, line);
    snprintf(line, sizeof(line), "Mode     : %s", card->mode);
    card_write_line(ttyp, line);
    snprintf(line, sizeof(line), "QTH      : %s", card->qth);
    card_write_line(ttyp, line);
    tty_write_str(ttyp, "  +------------------------------------------------+\r\n");
    card_write_line(ttyp, "Extended entries");
    for (int i = 0; i < card->ext_n; i++) {
        card_write_line(ttyp, card->ext[i]);
    }
    if (card->ext_n == 0) card_write_line(ttyp, "");
    tty_write_str(ttyp, "  +------------------------------------------------+\r\n");
    card_write_line(ttyp, "Signature");
    snprintf(line, sizeof(line), "%.45s", sig_b64);
    card_write_line(ttyp, line);
    snprintf(line, sizeof(line), "%s", sig_b64 + 45);
    card_write_line(ttyp, line);
    tty_write_str(ttyp, "  +------------------------------------------------+\r\n");
    card_write_line(ttyp, "Signed ID (Mona address)");
    card_write_line(ttyp, addr);
    tty_write_str(ttyp, "  +------------------------------------------------+\r\n");
    snprintf(line, sizeof(line), "Status   : %s", status);
    card_write_line(ttyp, line);
    tty_write_str(ttyp, "  +------------------------------------------------+\r\n");
    tty_write_str(ttyp, "\r\n");
}
