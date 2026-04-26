/*
 * Copyright (c) 2026 Daisuke JA1UMW / CQAKIBA.TOKYO
 * Released under the MIT License.
 * See LICENSE for details.
 */

#include "mona_compat.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/obj_mac.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/bn.h>

#include <string.h>

static int sha256_impl(const uint8_t *data, size_t len, uint8_t out32[32]) {
    unsigned int out_len = 0;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return 0;
    int ok = EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) == 1 &&
             EVP_DigestUpdate(ctx, data, len) == 1 &&
             EVP_DigestFinal_ex(ctx, out32, &out_len) == 1 &&
             out_len == 32;
    EVP_MD_CTX_free(ctx);
    return ok;
}

static int ripemd160_impl(const uint8_t *data, size_t len, uint8_t out20[20]) {
    unsigned int out_len = 0;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return 0;
    int ok = EVP_DigestInit_ex(ctx, EVP_ripemd160(), NULL) == 1 &&
             EVP_DigestUpdate(ctx, data, len) == 1 &&
             EVP_DigestFinal_ex(ctx, out20, &out_len) == 1 &&
             out_len == 20;
    EVP_MD_CTX_free(ctx);
    return ok;
}

static int hmac_sha256_impl(const uint8_t *key, size_t key_len,
                            const uint8_t *data, size_t data_len,
                            uint8_t out32[32]) {
    unsigned int out_len = 0;
    return HMAC(EVP_sha256(), key, (int)key_len, data, data_len, out32, &out_len) != NULL && out_len == 32;
}

static int random_bytes_impl(uint8_t *out, size_t len) {
    return RAND_bytes(out, (int)len) == 1;
}

static EC_GROUP *group_new(void) {
    EC_GROUP *group = EC_GROUP_new_by_curve_name(NID_secp256k1);
    if (group) EC_GROUP_set_asn1_flag(group, OPENSSL_EC_NAMED_CURVE);
    return group;
}

static int pubkey_create_compressed_impl(const uint8_t secret32[32], uint8_t out33[33]) {
    int ok = 0;
    BN_CTX *ctx = NULL;
    EC_GROUP *group = NULL;
    EC_POINT *pub = NULL;
    BIGNUM *priv = NULL;

    ctx = BN_CTX_new();
    group = group_new();
    pub = EC_POINT_new(group);
    priv = BN_bin2bn(secret32, 32, NULL);
    if (!ctx || !group || !pub || !priv) goto done;

    if (EC_POINT_mul(group, pub, priv, NULL, NULL, ctx) != 1) goto done;
    if (EC_POINT_point2oct(group, pub, POINT_CONVERSION_COMPRESSED, out33, 33, ctx) != 33) goto done;
    ok = 1;

 done:
    BN_free(priv);
    EC_POINT_free(pub);
    EC_GROUP_free(group);
    BN_CTX_free(ctx);
    return ok;
}

static int recover_pubkey_internal(const uint8_t msg32[32], const uint8_t sig64[64], int recid, uint8_t out33[33]) {
    int ok = 0;
    BN_CTX *ctx = NULL;
    EC_GROUP *group = NULL;
    EC_POINT *R = NULL, *sR = NULL, *eG = NULL, *Q = NULL;
    BIGNUM *r = NULL, *s = NULL, *e = NULL, *x = NULL, *order = NULL, *field = NULL, *rinv = NULL, *j = NULL;

    if (recid < 0 || recid > 3) return 0;

    ctx = BN_CTX_new();
    group = group_new();
    if (!ctx || !group) goto done;
    BN_CTX_start(ctx);

    r = BN_bin2bn(sig64, 32, NULL);
    s = BN_bin2bn(sig64 + 32, 32, NULL);
    e = BN_bin2bn(msg32, 32, NULL);
    x = BN_new();
    order = BN_new();
    field = BN_new();
    rinv = BN_new();
    j = BN_new();
    R = EC_POINT_new(group);
    sR = EC_POINT_new(group);
    eG = EC_POINT_new(group);
    Q = EC_POINT_new(group);
    if (!r || !s || !e || !x || !order || !field || !rinv || !j || !R || !sR || !eG || !Q) goto done;

    if (EC_GROUP_get_order(group, order, ctx) != 1) goto done;
    if (EC_GROUP_get_curve(group, field, NULL, NULL, ctx) != 1) goto done;

    if (BN_is_zero(r) || BN_is_zero(s) || BN_cmp(r, order) >= 0 || BN_cmp(s, order) >= 0) goto done;

    if (!BN_set_word(j, (unsigned int)(recid / 2))) goto done;
    if (!BN_copy(x, order)) goto done;
    if (!BN_mul(x, x, j, ctx)) goto done;
    if (!BN_add(x, x, r)) goto done;
    if (BN_cmp(x, field) >= 0) goto done;

    if (EC_POINT_set_compressed_coordinates(group, R, x, recid & 1, ctx) != 1) goto done;
    if (EC_POINT_is_on_curve(group, R, ctx) != 1) goto done;
    if (EC_POINT_mul(group, Q, NULL, R, order, ctx) != 1) goto done;
    if (!EC_POINT_is_at_infinity(group, Q)) goto done;

    if (BN_mod_inverse(rinv, r, order, ctx) == NULL) goto done;
    if (EC_POINT_mul(group, sR, NULL, R, s, ctx) != 1) goto done;
    if (EC_POINT_mul(group, eG, e, NULL, NULL, ctx) != 1) goto done;
    EC_POINT_invert(group, eG, ctx);
    if (EC_POINT_add(group, Q, sR, eG, ctx) != 1) goto done;
    if (EC_POINT_mul(group, Q, NULL, Q, rinv, ctx) != 1) goto done;
    if (EC_POINT_point2oct(group, Q, POINT_CONVERSION_COMPRESSED, out33, 33, ctx) != 33) goto done;

    ok = 1;
 done:
    BN_free(r); BN_free(s); BN_free(e); BN_free(x); BN_free(order); BN_free(field); BN_free(rinv); BN_free(j);
    EC_POINT_free(R); EC_POINT_free(sR); EC_POINT_free(eG); EC_POINT_free(Q);
    if (ctx) { BN_CTX_end(ctx); BN_CTX_free(ctx); }
    EC_GROUP_free(group);
    return ok;
}

static int sign_recoverable_compact_impl(const uint8_t msg32[32],
                                         const uint8_t secret32[32],
                                         uint8_t out64[64],
                                         int *out_recid) {
    int ok = 0;
    EC_KEY *ec = NULL;
    BIGNUM *priv = NULL;
    EC_POINT *pub = NULL;
    const BIGNUM *r = NULL, *s = NULL;
    ECDSA_SIG *sig = NULL;
    uint8_t pub33[33], rec33[33];

    ec = EC_KEY_new_by_curve_name(NID_secp256k1);
    priv = BN_bin2bn(secret32, 32, NULL);
    if (!ec || !priv) goto done;
    if (EC_KEY_set_private_key(ec, priv) != 1) goto done;

    const EC_GROUP *group = EC_KEY_get0_group(ec);
    pub = EC_POINT_new(group);
    if (!pub) goto done;
    if (EC_POINT_mul(group, pub, priv, NULL, NULL, NULL) != 1) goto done;
    if (EC_KEY_set_public_key(ec, pub) != 1) goto done;
    if (EC_POINT_point2oct(group, pub, POINT_CONVERSION_COMPRESSED, pub33, 33, NULL) != 33) goto done;

    sig = ECDSA_do_sign(msg32, 32, ec);
    if (!sig) goto done;
    ECDSA_SIG_get0(sig, &r, &s);
    if (BN_bn2binpad(r, out64, 32) != 32) goto done;
    if (BN_bn2binpad(s, out64 + 32, 32) != 32) goto done;

    for (int recid = 0; recid < 4; ++recid) {
        if (!recover_pubkey_internal(msg32, out64, recid, rec33)) continue;
        if (memcmp(rec33, pub33, 33) == 0) {
            *out_recid = recid;
            ok = 1;
            break;
        }
    }

 done:
    ECDSA_SIG_free(sig);
    EC_POINT_free(pub);
    BN_free(priv);
    EC_KEY_free(ec);
    return ok;
}

static int recover_pubkey_compressed_impl(const uint8_t msg32[32],
                                          const uint8_t sig64[64],
                                          int recid,
                                          uint8_t out33[33]) {
    return recover_pubkey_internal(msg32, sig64, recid, out33);
}

static int verify_compact_impl(const uint8_t msg32[32],
                               const uint8_t sig64[64],
                               const uint8_t pub33[33]) {
    int ok = 0;
    EC_KEY *ec = NULL;
    ECDSA_SIG *sig = NULL;
    EC_POINT *pub = NULL;
    BIGNUM *r = NULL, *s = NULL;
    const EC_GROUP *group;

    ec = EC_KEY_new_by_curve_name(NID_secp256k1);
    if (!ec) goto done;
    group = EC_KEY_get0_group(ec);
    pub = EC_POINT_new(group);
    r = BN_bin2bn(sig64, 32, NULL);
    s = BN_bin2bn(sig64 + 32, 32, NULL);
    sig = ECDSA_SIG_new();
    if (!pub || !r || !s || !sig) goto done;
    if (EC_POINT_oct2point(group, pub, pub33, 33, NULL) != 1) goto done;
    if (EC_KEY_set_public_key(ec, pub) != 1) goto done;
    if (ECDSA_SIG_set0(sig, r, s) != 1) goto done;
    r = NULL; s = NULL;
    ok = ECDSA_do_verify(msg32, 32, sig, ec) == 1;

 done:
    BN_free(r); BN_free(s);
    ECDSA_SIG_free(sig);
    EC_POINT_free(pub);
    EC_KEY_free(ec);
    return ok;
}

static const mona_crypto_vtable_t g_vt = {
    .sha256 = sha256_impl,
    .ripemd160 = ripemd160_impl,
    .hmac_sha256 = hmac_sha256_impl,
    .random_bytes = random_bytes_impl,
    .secp_pubkey_create_compressed = pubkey_create_compressed_impl,
    .secp_sign_recoverable_compact = sign_recoverable_compact_impl,
    .secp_recover_pubkey_compressed = recover_pubkey_compressed_impl,
    .secp_verify_compact = verify_compact_impl,
};

void mona_use_openssl_backend(void) {
    mona_set_crypto(&g_vt);
}
