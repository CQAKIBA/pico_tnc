/*
 * Copyright (c) 2026 Daisuke JA1UMW / CQAKIBA.TOKYO
 * Released under the MIT License.
 * See LICENSE for details.
 */

#include "mona_compat.h"

#include <string.h>
#include <stdio.h>

static const mona_crypto_vtable_t *g_crypto = NULL;

void mona_set_crypto(const mona_crypto_vtable_t *vt) { g_crypto = vt; }
const mona_crypto_vtable_t *mona_get_crypto(void) { return g_crypto; }

const char *mona_strerror(mona_err_t err) {
    switch (err) {
        case MONA_OK: return "ok";
        case MONA_ERR_ARGS: return "invalid arguments";
        case MONA_ERR_RANGE: return "value out of range";
        case MONA_ERR_FORMAT: return "invalid format";
        case MONA_ERR_CHECKSUM: return "checksum mismatch";
        case MONA_ERR_BUFFER: return "buffer too small";
        case MONA_ERR_RNG: return "random generator failed";
        case MONA_ERR_HASH: return "hash function failed";
        case MONA_ERR_ECC: return "secp256k1 operation failed";
        case MONA_ERR_ADDRESS_MISMATCH: return "address mismatch";
        case MONA_ERR_VERIFY_FAILED: return "signature verification failed";
        default: return "unknown error";
    }
}

static const char B58_ALPHABET[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
static const char BECH32_CHARSET[] = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
static const char *MONA_HRP = "mona";

static void memzero(void *p, size_t n) {
    volatile uint8_t *v = (volatile uint8_t *)p;
    while (n--) *v++ = 0;
}

static void bytes_to_hex(const uint8_t *in, size_t len, char *out) {
    static const char *hex = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        out[i * 2 + 0] = hex[in[i] >> 4];
        out[i * 2 + 1] = hex[in[i] & 0x0F];
    }
    out[len * 2] = '\0';
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static mona_err_t crypto_required(void) {
    if (!g_crypto) return MONA_ERR_ARGS;
    if (!g_crypto->sha256 || !g_crypto->ripemd160 || !g_crypto->hmac_sha256 ||
        !g_crypto->secp_pubkey_create_compressed || !g_crypto->secp_sign_recoverable_compact ||
        !g_crypto->secp_recover_pubkey_compressed || !g_crypto->secp_verify_compact) {
        return MONA_ERR_ARGS;
    }
    return MONA_OK;
}

static mona_err_t sha256_once(const uint8_t *data, size_t len, uint8_t out32[32]) {
    if (crypto_required() != MONA_OK) return MONA_ERR_ARGS;
    return g_crypto->sha256(data, len, out32) ? MONA_OK : MONA_ERR_HASH;
}

static mona_err_t sha256d(const uint8_t *data, size_t len, uint8_t out32[32]) {
    uint8_t t[32];
    mona_err_t err = sha256_once(data, len, t);
    if (err != MONA_OK) return err;
    err = sha256_once(t, sizeof(t), out32);
    memzero(t, sizeof(t));
    return err;
}

static mona_err_t hash160(const uint8_t *data, size_t len, uint8_t out20[20]) {
    uint8_t t[32];
    mona_err_t err = sha256_once(data, len, t);
    if (err != MONA_OK) return err;
    err = g_crypto->ripemd160(t, sizeof(t), out20) ? MONA_OK : MONA_ERR_HASH;
    memzero(t, sizeof(t));
    return err;
}

static void write_le(uint64_t v, uint8_t *out, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        out[i] = (uint8_t)(v & 0xFFu);
        v >>= 8;
    }
}

static size_t varint_encode(size_t v, uint8_t out[9]) {
    if (v < 0xFDu) {
        out[0] = (uint8_t)v;
        return 1;
    }
    if (v <= 0xFFFFu) {
        out[0] = 0xFD;
        write_le((uint64_t)v, out + 1, 2);
        return 3;
    }
    if (v <= 0xFFFFFFFFu) {
        out[0] = 0xFE;
        write_le((uint64_t)v, out + 1, 4);
        return 5;
    }
    out[0] = 0xFF;
    write_le((uint64_t)v, out + 1, 8);
    return 9;
}

mona_err_t mona_message_hash(const uint8_t *msg, size_t msg_len, uint8_t out32[32]) {
    static const uint8_t prefix[] = {0x19,
        'M','o','n','a','c','o','i','n',' ','S','i','g','n','e','d',' ','M','e','s','s','a','g','e',':','\n'};
    uint8_t varint[9];
    uint8_t buf[1 + sizeof("Monacoin Signed Message:\n") - 1 + 9 + MONA_MESSAGE_MAX];
    size_t pos = 0;
    size_t vi_len;

    if (!msg || !out32) return MONA_ERR_ARGS;
    if (msg_len > MONA_MESSAGE_MAX) return MONA_ERR_RANGE;

    memcpy(buf + pos, prefix, sizeof(prefix));
    pos += sizeof(prefix);
    vi_len = varint_encode(msg_len, varint);
    memcpy(buf + pos, varint, vi_len);
    pos += vi_len;
    memcpy(buf + pos, msg, msg_len);
    pos += msg_len;

    return sha256d(buf, pos, out32);
}

/* ---------------- Base58Check ---------------- */

static int b58_map(char c) {
    for (int i = 0; B58_ALPHABET[i]; ++i) if (B58_ALPHABET[i] == c) return i;
    return -1;
}

static mona_err_t base58_encode(const uint8_t *data, size_t data_len, char *out, size_t out_sz) {
    size_t zeros = 0, size, length = 0;
    uint8_t tmp[128];
    if (!data || !out) return MONA_ERR_ARGS;
    if (data_len > sizeof(tmp)) return MONA_ERR_RANGE;
    while (zeros < data_len && data[zeros] == 0) zeros++;
    size = (data_len - zeros) * 138 / 100 + 1;
    if (size > sizeof(tmp)) return MONA_ERR_RANGE;
    memset(tmp, 0, size);

    for (size_t i = zeros; i < data_len; ++i) {
        int carry = data[i];
        for (ssize_t j = (ssize_t)size - 1; j >= 0; --j) {
            carry += 256 * tmp[j];
            tmp[j] = (uint8_t)(carry % 58);
            carry /= 58;
        }
    }
    size_t it = 0;
    while (it < size && tmp[it] == 0) it++;
    if (zeros + (size - it) + 1 > out_sz) return MONA_ERR_BUFFER;
    for (size_t i = 0; i < zeros; ++i) out[length++] = '1';
    while (it < size) out[length++] = B58_ALPHABET[tmp[it++]];
    if (length == 0) out[length++] = '1';
    out[length] = '\0';
    memzero(tmp, sizeof(tmp));
    return MONA_OK;
}

static mona_err_t base58_decode(const char *s, uint8_t *out, size_t *out_len) {
    size_t slen, zeros = 0, size;
    uint8_t tmp[128];
    if (!s || !out || !out_len) return MONA_ERR_ARGS;
    slen = strlen(s);
    if (slen == 0) return MONA_ERR_FORMAT;
    while (s[zeros] == '1') zeros++;
    size = (slen - zeros) * 733 / 1000 + 1;
    if (size > sizeof(tmp)) return MONA_ERR_RANGE;
    memset(tmp, 0, size);

    for (size_t i = zeros; i < slen; ++i) {
        int val = b58_map(s[i]);
        if (val < 0) return MONA_ERR_FORMAT;
        int carry = val;
        for (ssize_t j = (ssize_t)size - 1; j >= 0; --j) {
            carry += 58 * tmp[j];
            tmp[j] = (uint8_t)(carry & 0xFF);
            carry >>= 8;
        }
        if (carry != 0) return MONA_ERR_RANGE;
    }

    size_t it = 0;
    while (it < size && tmp[it] == 0) it++;
    size_t needed = zeros + (size - it);
    if (*out_len < needed) {
        *out_len = needed;
        return MONA_ERR_BUFFER;
    }
    memset(out, 0, zeros);
    memcpy(out + zeros, tmp + it, size - it);
    *out_len = needed;
    memzero(tmp, sizeof(tmp));
    return MONA_OK;
}

static mona_err_t base58check_encode(const uint8_t *payload, size_t payload_len, char *out, size_t out_sz) {
    uint8_t raw[128];
    uint8_t cs[32];
    if (payload_len + 4 > sizeof(raw)) return MONA_ERR_RANGE;
    memcpy(raw, payload, payload_len);
    mona_err_t err = sha256d(payload, payload_len, cs);
    if (err != MONA_OK) return err;
    memcpy(raw + payload_len, cs, 4);
    err = base58_encode(raw, payload_len + 4, out, out_sz);
    memzero(raw, sizeof(raw));
    memzero(cs, sizeof(cs));
    return err;
}

static mona_err_t base58check_decode(const char *s, uint8_t *payload, size_t *payload_len) {
    uint8_t raw[128], cs[32];
    size_t raw_len = sizeof(raw);
    mona_err_t err = base58_decode(s, raw, &raw_len);
    if (err != MONA_OK) return err;
    if (raw_len < 5) return MONA_ERR_FORMAT;
    err = sha256d(raw, raw_len - 4, cs);
    if (err != MONA_OK) return err;
    if (memcmp(raw + raw_len - 4, cs, 4) != 0) return MONA_ERR_CHECKSUM;
    if (*payload_len < raw_len - 4) {
        *payload_len = raw_len - 4;
        return MONA_ERR_BUFFER;
    }
    memcpy(payload, raw, raw_len - 4);
    *payload_len = raw_len - 4;
    memzero(raw, sizeof(raw));
    memzero(cs, sizeof(cs));
    return MONA_OK;
}

/* ---------------- Bech32 ---------------- */

static uint32_t bech32_polymod(const uint8_t *values, size_t len) {
    static const uint32_t gen[5] = {0x3b6a57b2u,0x26508e6du,0x1ea119fau,0x3d4233ddu,0x2a1462b3u};
    uint32_t chk = 1;
    for (size_t i = 0; i < len; ++i) {
        uint32_t top = chk >> 25;
        chk = ((chk & 0x1ffffffu) << 5) ^ values[i];
        for (int j = 0; j < 5; ++j) if ((top >> j) & 1u) chk ^= gen[j];
    }
    return chk;
}

static size_t bech32_hrp_expand(const char *hrp, uint8_t *out) {
    size_t len = strlen(hrp), p = 0;
    for (size_t i = 0; i < len; ++i) out[p++] = (uint8_t)(hrp[i] >> 5);
    out[p++] = 0;
    for (size_t i = 0; i < len; ++i) out[p++] = (uint8_t)(hrp[i] & 31);
    return p;
}

static mona_err_t convert_bits(const uint8_t *in, size_t in_len, int from_bits, int to_bits,
                               bool pad, uint8_t *out, size_t *out_len) {
    uint32_t acc = 0;
    int bits = 0;
    uint32_t maxv = ((uint32_t)1u << to_bits) - 1u;
    size_t out_pos = 0;
    for (size_t i = 0; i < in_len; ++i) {
        uint32_t v = in[i];
        if ((v >> from_bits) != 0) return MONA_ERR_FORMAT;
        acc = (acc << from_bits) | v;
        bits += from_bits;
        while (bits >= to_bits) {
            bits -= to_bits;
            if (out_pos >= *out_len) return MONA_ERR_BUFFER;
            out[out_pos++] = (uint8_t)((acc >> bits) & maxv);
        }
    }
    if (pad) {
        if (bits) {
            if (out_pos >= *out_len) return MONA_ERR_BUFFER;
            out[out_pos++] = (uint8_t)((acc << (to_bits - bits)) & maxv);
        }
    } else {
        if (bits >= from_bits) return MONA_ERR_FORMAT;
        if (((acc << (to_bits - bits)) & maxv) != 0) return MONA_ERR_FORMAT;
    }
    *out_len = out_pos;
    return MONA_OK;
}

static mona_err_t bech32_encode_segwit(const char *hrp, uint8_t witver,
                                       const uint8_t *prog, size_t prog_len,
                                       char *out, size_t out_sz) {
    uint8_t data[84], values[128], checksum[6];
    size_t data_len = sizeof(data), hrp_exp_len, p = 0;
    if (witver > 16) return MONA_ERR_RANGE;
    if (prog_len < 2 || prog_len > 40) return MONA_ERR_RANGE;
    if (witver == 0 && prog_len != 20 && prog_len != 32) return MONA_ERR_RANGE;

    data[0] = witver;
    if (convert_bits(prog, prog_len, 8, 5, true, data + 1, &data_len) != MONA_OK) return MONA_ERR_FORMAT;
    data_len += 1;

    hrp_exp_len = bech32_hrp_expand(hrp, values);
    memcpy(values + hrp_exp_len, data, data_len);
    memset(values + hrp_exp_len + data_len, 0, 6);
    uint32_t pm = bech32_polymod(values, hrp_exp_len + data_len + 6) ^ 1u;
    for (int i = 0; i < 6; ++i) checksum[i] = (uint8_t)((pm >> (5 * (5 - i))) & 31u);

    size_t needed = strlen(hrp) + 1 + data_len + 6 + 1;
    if (needed > out_sz) return MONA_ERR_BUFFER;
    strcpy(out, hrp);
    p = strlen(hrp);
    out[p++] = '1';
    for (size_t i = 0; i < data_len; ++i) out[p++] = BECH32_CHARSET[data[i]];
    for (size_t i = 0; i < 6; ++i) out[p++] = BECH32_CHARSET[checksum[i]];
    out[p] = '\0';
    return MONA_OK;
}

/* ---------------- Monacoin helpers ---------------- */

mona_err_t mona_encode_wif(const mona_privkey_t *priv, char *out, size_t out_sz) {
    uint8_t payload[34];
    size_t len = 0;
    if (!priv || !out) return MONA_ERR_ARGS;
    payload[len++] = MONA_WIF_PREFIX;
    memcpy(payload + len, priv->secret, 32);
    len += 32;
    if (priv->compressed) payload[len++] = 0x01;
    return base58check_encode(payload, len, out, out_sz);
}

mona_err_t mona_decode_wif_any(const char *wif_or_prefixed, mona_privkey_t *out) {
    uint8_t payload[64];
    size_t payload_len = sizeof(payload);
    char bare[MONA_WIF_MAX];
    const char *wif = wif_or_prefixed;
    if (!wif_or_prefixed || !out) return MONA_ERR_ARGS;
    memset(out, 0, sizeof(*out));
    out->compressed = false;
    out->txin_type = MONA_TXIN_P2PKH;

    const char *colon = strchr(wif_or_prefixed, ':');
    if (colon) {
        size_t type_len = (size_t)(colon - wif_or_prefixed);
        if (type_len >= sizeof(bare)) return MONA_ERR_FORMAT;
        if (strncmp(wif_or_prefixed, "p2pkh", type_len) == 0) out->txin_type = MONA_TXIN_P2PKH;
        else if (strncmp(wif_or_prefixed, "p2wpkh-p2sh", type_len) == 0) out->txin_type = MONA_TXIN_P2WPKH_P2SH;
        else if (strncmp(wif_or_prefixed, "p2wpkh", type_len) == 0) out->txin_type = MONA_TXIN_P2WPKH;
        else out->txin_type = MONA_TXIN_UNKNOWN;
        snprintf(bare, sizeof(bare), "%s", colon + 1);
        wif = bare;
    }

    mona_err_t err = base58check_decode(wif, payload, &payload_len);
    if (err != MONA_OK) return err;
    if (payload_len != 33 && payload_len != 34) return MONA_ERR_FORMAT;
    if (payload[0] != MONA_WIF_PREFIX) return MONA_ERR_FORMAT;
    memcpy(out->secret, payload + 1, 32);
    if (payload_len == 34) {
        if (payload[33] != 0x01) return MONA_ERR_FORMAT;
        out->compressed = true;
    }
    if ((out->txin_type == MONA_TXIN_P2WPKH || out->txin_type == MONA_TXIN_P2WPKH_P2SH) && !out->compressed) {
        return MONA_ERR_FORMAT;
    }
    return MONA_OK;
}

static mona_err_t pubkey_to_addr_M(const uint8_t pub33[33], char *out, size_t out_sz) {
    uint8_t payload[21];
    uint8_t h160[20];
    mona_err_t err = hash160(pub33, 33, h160);
    if (err != MONA_OK) return err;
    payload[0] = MONA_P2PKH_PREFIX;
    memcpy(payload + 1, h160, 20);
    return base58check_encode(payload, sizeof(payload), out, out_sz);
}

static mona_err_t pubkey_to_addr_mona1(const uint8_t pub33[33], char *out, size_t out_sz) {
    uint8_t h160[20];
    mona_err_t err = hash160(pub33, 33, h160);
    if (err != MONA_OK) return err;
    return bech32_encode_segwit(MONA_HRP, 0, h160, sizeof(h160), out, out_sz);
}

static mona_err_t pubkey_to_addr_P(const uint8_t pub33[33], char *out, size_t out_sz) {
    uint8_t h160[20], redeem[22], redeem_h160[20], payload[21];
    mona_err_t err = hash160(pub33, 33, h160);
    if (err != MONA_OK) return err;
    redeem[0] = 0x00; redeem[1] = 0x14;
    memcpy(redeem + 2, h160, 20);
    err = hash160(redeem, sizeof(redeem), redeem_h160);
    if (err != MONA_OK) return err;
    payload[0] = MONA_P2SH_PREFIX;
    memcpy(payload + 1, redeem_h160, 20);
    return base58check_encode(payload, sizeof(payload), out, out_sz);
}

mona_err_t mona_keypair_from_secret(const uint8_t secret32[32], mona_address_info_t *out) {
    uint8_t pub33[33];
    mona_privkey_t priv;
    mona_err_t err;
    if (!secret32 || !out) return MONA_ERR_ARGS;
    err = crypto_required();
    if (err != MONA_OK) return err;
    if (!g_crypto->secp_pubkey_create_compressed(secret32, pub33)) return MONA_ERR_ECC;

    memset(&priv, 0, sizeof(priv));
    memcpy(priv.secret, secret32, 32);
    priv.compressed = true;
    priv.txin_type = MONA_TXIN_P2PKH;

    bytes_to_hex(secret32, 32, out->privkey_raw_hex);
    err = mona_encode_wif(&priv, out->privkey_wif, sizeof(out->privkey_wif));
    if (err != MONA_OK) return err;
    err = pubkey_to_addr_mona1(pub33, out->addr_mona1, sizeof(out->addr_mona1));
    if (err != MONA_OK) return err;
    err = pubkey_to_addr_M(pub33, out->addr_M, sizeof(out->addr_M));
    if (err != MONA_OK) return err;
    err = pubkey_to_addr_P(pub33, out->addr_P, sizeof(out->addr_P));
    if (err != MONA_OK) return err;
    memzero(&priv, sizeof(priv));
    return MONA_OK;
}

mona_err_t mona_createnewaddress(mona_address_info_t *out, mona_privkey_t *out_priv) {
    mona_err_t err = crypto_required();
    if (err != MONA_OK) return err;
    if (!out || !out_priv || !g_crypto->random_bytes) return MONA_ERR_ARGS;

    for (;;) {
        if (!g_crypto->random_bytes(out_priv->secret, 32)) return MONA_ERR_RNG;
        out_priv->compressed = true;
        out_priv->txin_type = MONA_TXIN_P2PKH;
        err = mona_keypair_from_secret(out_priv->secret, out);
        if (err == MONA_OK) return MONA_OK;
    }
}

static uint8_t header_from_recid(int recid, bool compressed, mona_txin_type_t txin_type, bool electrum_style) {
    uint8_t h = (uint8_t)(27 + recid);
    if (!compressed) return h;
    if (!electrum_style) return (uint8_t)(h + 4);
    switch (txin_type) {
        case MONA_TXIN_P2WPKH_P2SH: return (uint8_t)(h + 8);
        case MONA_TXIN_P2WPKH:      return (uint8_t)(h + 12);
        case MONA_TXIN_P2PKH:
        default:                   return (uint8_t)(h + 4);
    }
}

static mona_err_t compact_sig_to_b64(uint8_t header, const uint8_t sig64[64], char *out, size_t out_sz) {
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint8_t in[65];
    size_t olen = 0;
    in[0] = header;
    memcpy(in + 1, sig64, 64);
    if (out_sz < 89) return MONA_ERR_BUFFER;
    for (size_t i = 0; i < sizeof(in); i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        v |= (i + 1 < sizeof(in)) ? ((uint32_t)in[i + 1] << 8) : 0;
        v |= (i + 2 < sizeof(in)) ? (uint32_t)in[i + 2] : 0;
        out[olen++] = tbl[(v >> 18) & 63u];
        out[olen++] = tbl[(v >> 12) & 63u];
        out[olen++] = (i + 1 < sizeof(in)) ? tbl[(v >> 6) & 63u] : '=';
        out[olen++] = (i + 2 < sizeof(in)) ? tbl[v & 63u] : '=';
    }
    out[olen] = '\0';
    return MONA_OK;
}

static int b64v(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    if (c == '=') return -2;
    return -1;
}

static mona_err_t b64_to_compact_sig(const char *b64, uint8_t *out65) {
    size_t len = strlen(b64), pos = 0, o = 0;
    if ((len % 4) != 0) return MONA_ERR_FORMAT;
    while (pos < len) {
        int a = b64v(b64[pos++]), b = b64v(b64[pos++]);
        int c = b64v(b64[pos++]), d = b64v(b64[pos++]);
        if (a < 0 || b < 0 || c < -2 || d < -2) return MONA_ERR_FORMAT;
        uint32_t v = ((uint32_t)a << 18) | ((uint32_t)b << 12) |
                     ((uint32_t)(c < 0 ? 0 : c) << 6) | (uint32_t)(d < 0 ? 0 : d);
        if (o < 65) out65[o++] = (uint8_t)((v >> 16) & 0xFF);
        if (c != -2 && o < 65) out65[o++] = (uint8_t)((v >> 8) & 0xFF);
        if (d != -2 && o < 65) out65[o++] = (uint8_t)(v & 0xFF);
    }
    return (o == 65) ? MONA_OK : MONA_ERR_FORMAT;
}

mona_err_t mona_signmessage(const char *message,
                            const char *wif_or_prefixed,
                            char *out_sig_b64,
                            size_t out_sig_b64_sz,
                            mona_address_info_t *out_addrs) {
    uint8_t msg32[32], sig64[64];
    int recid = 0;
    mona_privkey_t priv;
    mona_err_t err;
    if (!message || !wif_or_prefixed || !out_sig_b64) return MONA_ERR_ARGS;
    err = mona_decode_wif_any(wif_or_prefixed, &priv);
    if (err != MONA_OK) return err;
    err = mona_message_hash((const uint8_t *)message, strlen(message), msg32);
    if (err != MONA_OK) return err;
    if (!g_crypto->secp_sign_recoverable_compact(msg32, priv.secret, sig64, &recid)) return MONA_ERR_ECC;

    /* Default: match current PHP implementation exactly (compressed => +4).
       If later you want Electrum-style segwit headers, switch the last argument to true. */
    err = compact_sig_to_b64(header_from_recid(recid, priv.compressed, priv.txin_type, false),
                             sig64, out_sig_b64, out_sig_b64_sz);
    if (err != MONA_OK) return err;
    if (out_addrs) {
        err = mona_keypair_from_secret(priv.secret, out_addrs);
        if (err != MONA_OK) return err;
    }
    memzero(&priv, sizeof(priv));
    memzero(msg32, sizeof(msg32));
    memzero(sig64, sizeof(sig64));
    return MONA_OK;
}

static mona_err_t decode_sig_header(uint8_t header, uint8_t *recid, bool *compressed, mona_txin_type_t *guess) {
    int nV = header;
    if (header < 27 || header > 42) return MONA_ERR_FORMAT;
    *compressed = true;
    *guess = MONA_TXIN_UNKNOWN;
    if (nV >= 39) {
        nV -= 12;
        *guess = MONA_TXIN_P2WPKH;
    } else if (nV >= 35) {
        nV -= 8;
        *guess = MONA_TXIN_P2WPKH_P2SH;
    } else if (nV >= 31) {
        nV -= 4;
        *guess = MONA_TXIN_P2PKH;
    } else {
        *compressed = false;
        *guess = MONA_TXIN_P2PKH;
    }
    if (nV < 27 || nV > 30) return MONA_ERR_FORMAT;
    *recid = (uint8_t)(nV - 27);
    return MONA_OK;
}

mona_err_t mona_verifymessage(const char *address,
                              const char *message,
                              const char *signature_b64,
                              mona_verify_result_t *out) {
    uint8_t sig65[65], sig64[64], msg32[32], pub33[33];
    mona_err_t err;
    if (!address || !message || !signature_b64 || !out) return MONA_ERR_ARGS;
    memset(out, 0, sizeof(*out));
    snprintf(out->address, sizeof(out->address), "%s", address);

    err = b64_to_compact_sig(signature_b64, sig65);
    if (err != MONA_OK) return err;
    out->header = sig65[0];
    err = decode_sig_header(sig65[0], &out->recid, &out->compressed, &out->txin_type_guess);
    if (err != MONA_OK) return err;
    memcpy(sig64, sig65 + 1, 64);
    err = mona_message_hash((const uint8_t *)message, strlen(message), msg32);
    if (err != MONA_OK) return err;
    if (!g_crypto->secp_recover_pubkey_compressed(msg32, sig64, out->recid, pub33)) return MONA_ERR_ECC;
    if (!g_crypto->secp_verify_compact(msg32, sig64, pub33)) return MONA_ERR_VERIFY_FAILED;

    bytes_to_hex(pub33, 33, out->recovered_pubkey_hex);
    err = pubkey_to_addr_mona1(pub33, out->recovered_addr_mona1, sizeof(out->recovered_addr_mona1));
    if (err != MONA_OK) return err;
    err = pubkey_to_addr_M(pub33, out->recovered_addr_M, sizeof(out->recovered_addr_M));
    if (err != MONA_OK) return err;
    err = pubkey_to_addr_P(pub33, out->recovered_addr_P, sizeof(out->recovered_addr_P));
    if (err != MONA_OK) return err;

    switch (out->txin_type_guess) {
        case MONA_TXIN_P2WPKH:
            if (strcmp(address, out->recovered_addr_mona1) != 0) return MONA_ERR_ADDRESS_MISMATCH;
            break;
        case MONA_TXIN_P2WPKH_P2SH:
            if (strcmp(address, out->recovered_addr_P) != 0) return MONA_ERR_ADDRESS_MISMATCH;
            break;
        case MONA_TXIN_P2PKH:
        case MONA_TXIN_UNKNOWN:
        default:
            if (strcmp(address, out->recovered_addr_M) != 0 &&
                strcmp(address, out->recovered_addr_mona1) != 0 &&
                strcmp(address, out->recovered_addr_P) != 0) {
                return MONA_ERR_ADDRESS_MISMATCH;
            }
            break;
    }
    out->valid = true;
    memzero(sig65, sizeof(sig65));
    memzero(sig64, sizeof(sig64));
    memzero(msg32, sizeof(msg32));
    return MONA_OK;
}
