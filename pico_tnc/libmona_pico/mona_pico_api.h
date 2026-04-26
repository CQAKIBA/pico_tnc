/*
 * Copyright (c) 2026 Daisuke JA1UMW / CQAKIBA.TOKYO
 * Released under the MIT License.
 * See LICENSE for details.
 */

#ifndef MONA_PICO_API_H
#define MONA_PICO_API_H

#include "mona_compat.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MONA_ADDR_P2PKH = 0,
    MONA_ADDR_P2SH = 1,
    MONA_ADDR_P2WPKH = 2
} mona_addr_type_t;

typedef struct {
    uint8_t secret[32];
    bool compressed;
    mona_addr_type_t active_type;
    bool valid;
} mona_keyslot_t;

const char *mona_addr_type_name(mona_addr_type_t type);
const char *mona_addr_type_alias(mona_addr_type_t type);
mona_err_t mona_parse_addr_type(const char *s, mona_addr_type_t *out);
mona_txin_type_t mona_addr_type_to_txin_type(mona_addr_type_t type);

mona_err_t mona_keyslot_init_from_secret(mona_keyslot_t *slot,
                                         const uint8_t secret32[32],
                                         mona_addr_type_t active_type);

mona_err_t mona_keyslot_init_from_input(mona_keyslot_t *slot,
                                        const char *wif_or_raw,
                                        bool keep_active_type_if_untyped);

mona_err_t mona_keyslot_get_all(mona_keyslot_t const *slot,
                                mona_address_info_t *out_info,
                                char *out_wif_typed,
                                size_t out_wif_typed_sz);

mona_err_t mona_keyslot_get_active_address(mona_keyslot_t const *slot,
                                           char *out,
                                           size_t out_sz);

mona_err_t mona_keyslot_sign_message(mona_keyslot_t const *slot,
                                     const char *message,
                                     char *out_sig_b64,
                                     size_t out_sig_b64_sz,
                                     char *out_active_addr,
                                     size_t out_active_addr_sz);

#ifdef __cplusplus
}
#endif

#endif
