#ifndef QSL_CARD_H
#define QSL_CARD_H

#include <stdbool.h>

#include "tty.h"

#define QSL_CARD_EXT_MAX 4

typedef struct {
    char to_call[17];
    char report[16];
    char date[24];
    char time[24];
    char freq[24];
    char mode[16];
    char qth[40];
    char ext[QSL_CARD_EXT_MAX][48];
    int ext_n;
    bool has_qsl;
} qsl_card_t;

bool qsl_card_parse(const char *json, qsl_card_t *card);
void qsl_card_render(tty_t *ttyp, const qsl_card_t *card, const char *from, const char *addr, const char *sig_b64, const char *status);

#endif
