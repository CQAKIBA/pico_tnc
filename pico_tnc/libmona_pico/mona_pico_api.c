/*
 * Copyright (c) 2026 Daisuke JA1UMW / CQAKIBA.TOKYO
 * Released under the MIT License.
 * See LICENSE for details.
 */

#include "mona_pico_api.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static mona_err_t hex32_to_bytes(const char *s, uint8_t out[32]) {
    size_t len;
    if (!s || !out) return MONA_ERR_ARGS;
    len = strlen(s);
    if (len != 64) return MONA_ERR_FORMAT;
    for (size_t i = 0; i < 32; ++i) {
        int hi = hex_nibble(s[i * 2]);
        int lo = hex_nibble(s[i * 2 + 1]);
        if (hi < 0 || lo < 0) return MONA_ERR_FORMAT;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return MONA_OK;
}

const char *mona_addr_type_name(mona_addr_type_t type) {
    switch (type) {
        case MONA_ADDR_P2PKH: return "p2pkh";
        case MONA_ADDR_P2SH: return "p2sh";
        case MONA_ADDR_P2WPKH: return "p2wpkh";
        default: return "unknown";
    }
}

const char *mona_addr_type_alias(mona_addr_type_t type) {
    switch (type) {
        case MONA_ADDR_P2PKH: return "M";
        case MONA_ADDR_P2SH: return "P";
        case MONA_ADDR_P2WPKH: return "mona1";
        default: return "?";
    }
}

mona_err_t mona_parse_addr_type(const char *s, mona_addr_type_t *out) {
    if (!s || !out) return MONA_ERR_ARGS;
    if (strcmp(s, "p2pkh") == 0 || strcmp(s, "m") == 0 || strcmp(s, "M") == 0) {
        *out = MONA_ADDR_P2PKH; return MONA_OK;
    }
    if (strcmp(s, "p2sh") == 0 || strcmp(s, "p") == 0 || strcmp(s, "P") == 0) {
        *out = MONA_ADDR_P2SH; return MONA_OK;
    }
    if (strcmp(s, "p2wpkh") == 0 || strcmp(s, "mona1") == 0 || strcmp(s, "bech32") == 0) {
        *out = MONA_ADDR_P2WPKH; return MONA_OK;
    }
    return MONA_ERR_FORMAT;
}

mona_txin_type_t mona_addr_type_to_txin_type(mona_addr_type_t type) {
    switch (type) {
        case MONA_ADDR_P2PKH: return MONA_TXIN_P2PKH;
        case MONA_ADDR_P2SH: return MONA_TXIN_P2WPKH_P2SH;
        case MONA_ADDR_P2WPKH: return MONA_TXIN_P2WPKH;
        default: return MONA_TXIN_UNKNOWN;
    }
}

mona_err_t mona_keyslot_init_from_secret(mona_keyslot_t *slot,
                                         const uint8_t secret32[32],
                                         mona_addr_type_t active_type) {
    if (!slot || !secret32) return MONA_ERR_ARGS;
    memset(slot, 0, sizeof(*slot));
    memcpy(slot->secret, secret32, 32);
    slot->compressed = true;
    slot->active_type = active_type;
    slot->valid = true;
    return MONA_OK;
}

mona_err_t mona_keyslot_init_from_input(mona_keyslot_t *slot,
                                        const char *wif_or_raw,
                                        bool keep_active_type_if_untyped) {
    mona_privkey_t priv;
    mona_err_t err;
    mona_addr_type_t prev = MONA_ADDR_P2PKH;
    if (!slot || !wif_or_raw) return MONA_ERR_ARGS;
    if (slot->valid) prev = slot->active_type;

    err = mona_decode_wif_any(wif_or_raw, &priv);
    if (err == MONA_OK) {
        memcpy(slot->secret, priv.secret, 32);
        slot->compressed = priv.compressed;
        if (strchr(wif_or_raw, ':')) {
            switch (priv.txin_type) {
                case MONA_TXIN_P2PKH: slot->active_type = MONA_ADDR_P2PKH; break;
                case MONA_TXIN_P2WPKH_P2SH: slot->active_type = MONA_ADDR_P2SH; break;
                case MONA_TXIN_P2WPKH: slot->active_type = MONA_ADDR_P2WPKH; break;
                default: slot->active_type = keep_active_type_if_untyped ? prev : MONA_ADDR_P2PKH; break;
            }
        } else {
            slot->active_type = keep_active_type_if_untyped ? prev : MONA_ADDR_P2PKH;
        }
        slot->valid = true;
        return MONA_OK;
    }

    err = hex32_to_bytes(wif_or_raw, slot->secret);
    if (err != MONA_OK) return err;
    slot->compressed = true;
    slot->active_type = keep_active_type_if_untyped ? prev : MONA_ADDR_P2PKH;
    slot->valid = true;
    return MONA_OK;
}

mona_err_t mona_keyslot_get_all(mona_keyslot_t const *slot,
                                mona_address_info_t *out_info,
                                char *out_wif_typed,
                                size_t out_wif_typed_sz) {
    mona_privkey_t priv;
    mona_err_t err;
    if (!slot || !slot->valid || !out_info) return MONA_ERR_ARGS;
    err = mona_keypair_from_secret(slot->secret, out_info);
    if (err != MONA_OK) return err;
    if (out_wif_typed && out_wif_typed_sz) {
        char tmp[MONA_WIF_MAX];
        priv.compressed = slot->compressed;
        priv.txin_type = mona_addr_type_to_txin_type(slot->active_type);
        memcpy(priv.secret, slot->secret, 32);
        err = mona_encode_wif(&priv, tmp, sizeof(tmp));
        if (err != MONA_OK) return err;
        if (snprintf(out_wif_typed, out_wif_typed_sz, "%s:%s", mona_addr_type_name(slot->active_type), tmp) >= (int)out_wif_typed_sz) {
            return MONA_ERR_BUFFER;
        }
    }
    return MONA_OK;
}

mona_err_t mona_keyslot_get_active_address(mona_keyslot_t const *slot,
                                           char *out,
                                           size_t out_sz) {
    mona_address_info_t info;
    mona_err_t err = mona_keyslot_get_all(slot, &info, NULL, 0);
    if (err != MONA_OK) return err;
    const char *src = info.addr_M;
    if (slot->active_type == MONA_ADDR_P2SH) src = info.addr_P;
    else if (slot->active_type == MONA_ADDR_P2WPKH) src = info.addr_mona1;
    if (snprintf(out, out_sz, "%s", src) >= (int)out_sz) return MONA_ERR_BUFFER;
    return MONA_OK;
}

mona_err_t mona_keyslot_sign_message(mona_keyslot_t const *slot,
                                     const char *message,
                                     char *out_sig_b64,
                                     size_t out_sig_b64_sz,
                                     char *out_active_addr,
                                     size_t out_active_addr_sz) {
    mona_privkey_t priv;
    char wif[MONA_WIF_MAX];
    mona_address_info_t info;
    mona_err_t err;
    if (!slot || !slot->valid || !message || !out_sig_b64) return MONA_ERR_ARGS;
    memset(&priv, 0, sizeof(priv));
    memcpy(priv.secret, slot->secret, 32);
    priv.compressed = slot->compressed;
    priv.txin_type = mona_addr_type_to_txin_type(slot->active_type);
    err = mona_encode_wif(&priv, wif, sizeof(wif));
    if (err != MONA_OK) return err;
    err = mona_signmessage(message, wif, out_sig_b64, out_sig_b64_sz, &info);
    if (err != MONA_OK) return err;
    if (out_active_addr && out_active_addr_sz) {
        const char *src = info.addr_M;
        if (slot->active_type == MONA_ADDR_P2SH) src = info.addr_P;
        else if (slot->active_type == MONA_ADDR_P2WPKH) src = info.addr_mona1;
        if (snprintf(out_active_addr, out_active_addr_sz, "%s", src) >= (int)out_active_addr_sz) return MONA_ERR_BUFFER;
    }
    return MONA_OK;
}
