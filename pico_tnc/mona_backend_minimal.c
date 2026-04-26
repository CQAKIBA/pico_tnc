/*
 * Copyright (c) 2026 Daisuke JA1UMW / CQAKIBA.TOKYO
 * Released under the MIT License.
 * See LICENSE for details.
 */

#include "mona_backend_minimal.h"

#include <stdint.h>
#include <string.h>

#include "libmona_pico/mona_compat.h"
#include "secp256k1.h"
#include "secp256k1_recovery.h"

typedef struct {
    uint32_t state[8];
    uint64_t bit_len;
    uint8_t data[64];
    uint32_t data_len;
} sha256_ctx_t;

static const uint32_t k256[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

static inline uint32_t rotr32(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }
static inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
static inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
static inline uint32_t bsig0(uint32_t x) { return rotr32(x, 2) ^ rotr32(x, 13) ^ rotr32(x, 22); }
static inline uint32_t bsig1(uint32_t x) { return rotr32(x, 6) ^ rotr32(x, 11) ^ rotr32(x, 25); }
static inline uint32_t ssig0(uint32_t x) { return rotr32(x, 7) ^ rotr32(x, 18) ^ (x >> 3); }
static inline uint32_t ssig1(uint32_t x) { return rotr32(x, 17) ^ rotr32(x, 19) ^ (x >> 10); }

static void sha256_transform(sha256_ctx_t *ctx, const uint8_t data[64])
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;

    for (int i = 0; i < 16; ++i) {
        w[i] = ((uint32_t)data[i * 4] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) |
               (uint32_t)data[i * 4 + 3];
    }
    for (int i = 16; i < 64; ++i) {
        w[i] = ssig1(w[i - 2]) + w[i - 7] + ssig0(w[i - 15]) + w[i - 16];
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (int i = 0; i < 64; ++i) {
        uint32_t t1 = h + bsig1(e) + ch(e, f, g) + k256[i] + w[i];
        uint32_t t2 = bsig0(a) + maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(sha256_ctx_t *ctx)
{
    ctx->data_len = 0;
    ctx->bit_len = 0;
    ctx->state[0] = 0x6a09e667u;
    ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u;
    ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu;
    ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu;
    ctx->state[7] = 0x5be0cd19u;
}

static void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        ctx->data[ctx->data_len++] = data[i];
        if (ctx->data_len == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->bit_len += 512;
            ctx->data_len = 0;
        }
    }
}

static void sha256_final(sha256_ctx_t *ctx, uint8_t out[32])
{
    uint32_t i = ctx->data_len;

    if (ctx->data_len < 56) {
        ctx->data[i++] = 0x80u;
        while (i < 56) ctx->data[i++] = 0x00u;
    } else {
        ctx->data[i++] = 0x80u;
        while (i < 64) ctx->data[i++] = 0x00u;
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    }

    ctx->bit_len += (uint64_t)ctx->data_len * 8u;
    ctx->data[63] = (uint8_t)(ctx->bit_len);
    ctx->data[62] = (uint8_t)(ctx->bit_len >> 8);
    ctx->data[61] = (uint8_t)(ctx->bit_len >> 16);
    ctx->data[60] = (uint8_t)(ctx->bit_len >> 24);
    ctx->data[59] = (uint8_t)(ctx->bit_len >> 32);
    ctx->data[58] = (uint8_t)(ctx->bit_len >> 40);
    ctx->data[57] = (uint8_t)(ctx->bit_len >> 48);
    ctx->data[56] = (uint8_t)(ctx->bit_len >> 56);
    sha256_transform(ctx, ctx->data);

    for (i = 0; i < 4; ++i) {
        out[i] = (uint8_t)((ctx->state[0] >> (24 - i * 8)) & 0xffu);
        out[i + 4] = (uint8_t)((ctx->state[1] >> (24 - i * 8)) & 0xffu);
        out[i + 8] = (uint8_t)((ctx->state[2] >> (24 - i * 8)) & 0xffu);
        out[i + 12] = (uint8_t)((ctx->state[3] >> (24 - i * 8)) & 0xffu);
        out[i + 16] = (uint8_t)((ctx->state[4] >> (24 - i * 8)) & 0xffu);
        out[i + 20] = (uint8_t)((ctx->state[5] >> (24 - i * 8)) & 0xffu);
        out[i + 24] = (uint8_t)((ctx->state[6] >> (24 - i * 8)) & 0xffu);
        out[i + 28] = (uint8_t)((ctx->state[7] >> (24 - i * 8)) & 0xffu);
    }
}

static int sha256_impl(const uint8_t *data, size_t len, uint8_t out32[32])
{
    sha256_ctx_t ctx;
    if (!data || !out32) return 0;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, out32);
    return 1;
}

typedef struct {
    uint32_t h[5];
    uint64_t total_len;
    uint8_t block[64];
    size_t block_len;
} ripemd160_ctx_t;

static inline uint32_t rol32(uint32_t x, unsigned s) { return (x << s) | (x >> (32 - s)); }

static uint32_t ripemd_f(int j, uint32_t x, uint32_t y, uint32_t z)
{
    if (j <= 15) return x ^ y ^ z;
    if (j <= 31) return (x & y) | (~x & z);
    if (j <= 47) return (x | ~y) ^ z;
    if (j <= 63) return (x & z) | (y & ~z);
    return x ^ (y | ~z);
}

static uint32_t ripemd_k(int j)
{
    if (j <= 15) return 0x00000000u;
    if (j <= 31) return 0x5A827999u;
    if (j <= 47) return 0x6ED9EBA1u;
    if (j <= 63) return 0x8F1BBCDCu;
    return 0xA953FD4Eu;
}

static uint32_t ripemd_kp(int j)
{
    if (j <= 15) return 0x50A28BE6u;
    if (j <= 31) return 0x5C4DD124u;
    if (j <= 47) return 0x6D703EF3u;
    if (j <= 63) return 0x7A6D76E9u;
    return 0x00000000u;
}

static const uint8_t RIP_R[80] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,
    7, 4,13, 1,10, 6,15, 3,12, 0, 9, 5, 2,14,11, 8,
    3,10,14, 4, 9,15, 8, 1, 2, 7, 0, 6,13,11, 5,12,
    1, 9,11,10, 0, 8,12, 4,13, 3, 7,15,14, 5, 6, 2,
    4, 0, 5, 9, 7,12, 2,10,14, 1, 3, 8,11, 6,15,13
};

static const uint8_t RIP_RP[80] = {
     5,14, 7, 0, 9, 2,11, 4,13, 6,15, 8, 1,10, 3,12,
     6,11, 3, 7, 0,13, 5,10,14,15, 8,12, 4, 9, 1, 2,
    15, 5, 1, 3, 7,14, 6, 9,11, 8,12, 2,10, 0, 4,13,
     8, 6, 4, 1, 3,11,15, 0, 5,12, 2,13, 9, 7,10,14,
    12,15,10, 4, 1, 5, 8, 7, 6, 2,13,14, 0, 3, 9,11
};

static const uint8_t RIP_S[80] = {
    11,14,15,12, 5, 8, 7, 9,11,13,14,15, 6, 7, 9, 8,
     7, 6, 8,13,11, 9, 7,15, 7,12,15, 9,11, 7,13,12,
    11,13, 6, 7,14, 9,13,15,14, 8,13, 6, 5,12, 7, 5,
    11,12,14,15,14,15, 9, 8, 9,14, 5, 6, 8, 6, 5,12,
     9,15, 5,11, 6, 8,13,12, 5,12,13,14,11, 8, 5, 6
};

static const uint8_t RIP_SP[80] = {
     8, 9, 9,11,13,15,15, 5, 7, 7, 8,11,14,14,12, 6,
     9,13,15, 7,12, 8, 9,11, 7, 7,12, 7, 6,15,13,11,
     9, 7,15,11, 8, 6, 6,14,12,13, 5,14,13,13, 7, 5,
    15, 5, 8,11,14,14, 6,14, 6, 9,12, 9,12, 5,15, 8,
     8, 5,12, 9,12, 5,14, 6, 8,13, 6, 5,15,13,11,11
};

static void ripemd160_compress(ripemd160_ctx_t *ctx, const uint8_t block[64])
{
    uint32_t x[16];
    uint32_t al, bl, cl, dl, el;
    uint32_t ar, br, cr, dr, er;
    uint32_t t;
    int j;

    for (j = 0; j < 16; ++j) {
        x[j] = (uint32_t)block[j * 4] |
               ((uint32_t)block[j * 4 + 1] << 8) |
               ((uint32_t)block[j * 4 + 2] << 16) |
               ((uint32_t)block[j * 4 + 3] << 24);
    }

    al = ar = ctx->h[0];
    bl = br = ctx->h[1];
    cl = cr = ctx->h[2];
    dl = dr = ctx->h[3];
    el = er = ctx->h[4];

    for (j = 0; j < 80; ++j) {
        t = rol32(al + ripemd_f(j, bl, cl, dl) + x[RIP_R[j]] + ripemd_k(j), RIP_S[j]) + el;
        al = el; el = dl; dl = rol32(cl, 10); cl = bl; bl = t;
        t = rol32(ar + ripemd_f(79 - j, br, cr, dr) + x[RIP_RP[j]] + ripemd_kp(j), RIP_SP[j]) + er;
        ar = er; er = dr; dr = rol32(cr, 10); cr = br; br = t;
    }

    t = ctx->h[1] + cl + dr;
    ctx->h[1] = ctx->h[2] + dl + er;
    ctx->h[2] = ctx->h[3] + el + ar;
    ctx->h[3] = ctx->h[4] + al + br;
    ctx->h[4] = ctx->h[0] + bl + cr;
    ctx->h[0] = t;
}

static void ripemd160_init(ripemd160_ctx_t *ctx)
{
    ctx->h[0] = 0x67452301u;
    ctx->h[1] = 0xEFCDAB89u;
    ctx->h[2] = 0x98BADCFEu;
    ctx->h[3] = 0x10325476u;
    ctx->h[4] = 0xC3D2E1F0u;
    ctx->total_len = 0;
    ctx->block_len = 0;
}

static void ripemd160_update(ripemd160_ctx_t *ctx, const uint8_t *data, size_t len)
{
    ctx->total_len += len;
    while (len > 0) {
        size_t take = 64 - ctx->block_len;
        if (take > len) take = len;
        memcpy(ctx->block + ctx->block_len, data, take);
        ctx->block_len += take;
        data += take;
        len -= take;
        if (ctx->block_len == 64) {
            ripemd160_compress(ctx, ctx->block);
            ctx->block_len = 0;
        }
    }
}

static void ripemd160_final(ripemd160_ctx_t *ctx, uint8_t out20[20])
{
    uint64_t bits = ctx->total_len * 8u;
    size_t i;

    ctx->block[ctx->block_len++] = 0x80u;
    if (ctx->block_len > 56) {
        while (ctx->block_len < 64) ctx->block[ctx->block_len++] = 0;
        ripemd160_compress(ctx, ctx->block);
        ctx->block_len = 0;
    }
    while (ctx->block_len < 56) ctx->block[ctx->block_len++] = 0;
    for (i = 0; i < 8; ++i) {
        ctx->block[56 + i] = (uint8_t)(bits >> (8 * i));
    }
    ripemd160_compress(ctx, ctx->block);

    for (i = 0; i < 5; ++i) {
        out20[i * 4] = (uint8_t)(ctx->h[i] & 0xffu);
        out20[i * 4 + 1] = (uint8_t)((ctx->h[i] >> 8) & 0xffu);
        out20[i * 4 + 2] = (uint8_t)((ctx->h[i] >> 16) & 0xffu);
        out20[i * 4 + 3] = (uint8_t)((ctx->h[i] >> 24) & 0xffu);
    }
}

static int ripemd160_impl(const uint8_t *data, size_t len, uint8_t out20[20])
{
    ripemd160_ctx_t ctx;
    if (!data || !out20) return 0;
    ripemd160_init(&ctx);
    ripemd160_update(&ctx, data, len);
    ripemd160_final(&ctx, out20);
    return 1;
}

static int not_supported_hmac(const uint8_t *key, size_t key_len,
                              const uint8_t *data, size_t data_len,
                              uint8_t out32[32])
{
    (void)key;
    (void)key_len;
    (void)data;
    (void)data_len;
    (void)out32;
    return 0;
}

static int not_supported_rng(uint8_t *out, size_t len)
{
    (void)out;
    (void)len;
    return 0;
}

static secp256k1_context *g_secp_ctx = NULL;

static secp256k1_context *secp_get_ctx(void)
{
    if (!g_secp_ctx) {
        g_secp_ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    }
    return g_secp_ctx;
}

static int secp_pubkey_create_compressed_impl(const uint8_t secret32[32], uint8_t out33[33])
{
    secp256k1_pubkey pubkey;
    size_t out_len = 33;
    secp256k1_context *ctx = secp_get_ctx();
    if (!ctx || !secret32 || !out33) return 0;
    if (!secp256k1_ec_pubkey_create(ctx, &pubkey, secret32)) return 0;
    return secp256k1_ec_pubkey_serialize(ctx, out33, &out_len, &pubkey, SECP256K1_EC_COMPRESSED);
}

static int secp_sign_recoverable_compact_impl(const uint8_t msg32[32],
                                              const uint8_t secret32[32],
                                              uint8_t out64[64],
                                              int *out_recid)
{
    secp256k1_ecdsa_recoverable_signature sig;
    secp256k1_context *ctx = secp_get_ctx();
    if (!ctx || !msg32 || !secret32 || !out64 || !out_recid) return 0;
    if (!secp256k1_ecdsa_sign_recoverable(ctx, &sig, msg32, secret32, NULL, NULL)) return 0;
    return secp256k1_ecdsa_recoverable_signature_serialize_compact(ctx, out64, out_recid, &sig);
}

static int secp_recover_pubkey_compressed_impl(const uint8_t msg32[32],
                                               const uint8_t sig64[64],
                                               int recid,
                                               uint8_t out33[33])
{
    secp256k1_ecdsa_recoverable_signature sig;
    secp256k1_pubkey pubkey;
    size_t out_len = 33;
    secp256k1_context *ctx = secp_get_ctx();
    if (!ctx || !msg32 || !sig64 || !out33) return 0;
    if (!secp256k1_ecdsa_recoverable_signature_parse_compact(ctx, &sig, sig64, recid)) return 0;
    if (!secp256k1_ecdsa_recover(ctx, &pubkey, &sig, msg32)) return 0;
    return secp256k1_ec_pubkey_serialize(ctx, out33, &out_len, &pubkey, SECP256K1_EC_COMPRESSED);
}

static int secp_verify_compact_impl(const uint8_t msg32[32],
                                    const uint8_t sig64[64],
                                    const uint8_t pub33[33])
{
    secp256k1_ecdsa_signature sig;
    secp256k1_pubkey pubkey;
    secp256k1_context *ctx = secp_get_ctx();
    if (!ctx || !msg32 || !sig64 || !pub33) return 0;
    if (!secp256k1_ecdsa_signature_parse_compact(ctx, &sig, sig64)) return 0;
    if (!secp256k1_ec_pubkey_parse(ctx, &pubkey, pub33, 33)) return 0;
    return secp256k1_ecdsa_verify(ctx, &sig, msg32, &pubkey);
}

static const mona_crypto_vtable_t g_vt = {
    .sha256 = sha256_impl,
    .ripemd160 = ripemd160_impl,
    .hmac_sha256 = not_supported_hmac,
    .random_bytes = not_supported_rng,
    .secp_pubkey_create_compressed = secp_pubkey_create_compressed_impl,
    .secp_sign_recoverable_compact = secp_sign_recoverable_compact_impl,
    .secp_recover_pubkey_compressed = secp_recover_pubkey_compressed_impl,
    .secp_verify_compact = secp_verify_compact_impl,
};

void mona_backend_minimal_init(void)
{
    if (mona_get_crypto() != &g_vt) {
        mona_set_crypto(&g_vt);
    }
}

bool mona_backend_minimal_has_full_crypto(void)
{
    return true;
}
