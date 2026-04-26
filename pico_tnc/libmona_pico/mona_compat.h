/*
 * Copyright (c) 2026 Daisuke JA1UMW / CQAKIBA.TOKYO
 * Released under the MIT License.
 * See LICENSE for details.
 */

#ifndef MONA_COMPAT_H
#define MONA_COMPAT_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MONA_SECRET_SIZE 32
#define MONA_PUBKEY_COMPRESSED_SIZE 33
#define MONA_SIGNATURE_COMPACT_SIZE 65
#define MONA_ADDRESS_MAX 128
#define MONA_WIF_MAX 64
#define MONA_SIG_B64_MAX 96
#define MONA_MESSAGE_MAX 512

#define MONA_WIF_PREFIX 0xB0u
#define MONA_P2PKH_PREFIX 0x32u
#define MONA_P2SH_PREFIX  0x37u

/* Electrum/Monacoin script type hint carried in WIF prefix string or compact header. */
typedef enum {
    MONA_TXIN_P2PKH = 0,
    MONA_TXIN_P2WPKH_P2SH = 1,
    MONA_TXIN_P2WPKH = 2,
    MONA_TXIN_UNKNOWN = 255
} mona_txin_type_t;

typedef enum {
    MONA_OK = 0,
    MONA_ERR_ARGS,
    MONA_ERR_RANGE,
    MONA_ERR_FORMAT,
    MONA_ERR_CHECKSUM,
    MONA_ERR_BUFFER,
    MONA_ERR_RNG,
    MONA_ERR_HASH,
    MONA_ERR_ECC,
    MONA_ERR_ADDRESS_MISMATCH,
    MONA_ERR_VERIFY_FAILED
} mona_err_t;

typedef struct {
    uint8_t secret[MONA_SECRET_SIZE];
    bool compressed;
    mona_txin_type_t txin_type;
} mona_privkey_t;

typedef struct {
    char privkey_wif[MONA_WIF_MAX];
    char privkey_raw_hex[MONA_SECRET_SIZE * 2 + 1];
    char addr_mona1[MONA_ADDRESS_MAX];
    char addr_M[MONA_ADDRESS_MAX];
    char addr_P[MONA_ADDRESS_MAX];
} mona_address_info_t;

typedef struct {
    char address[MONA_ADDRESS_MAX];
    char recovered_addr_mona1[MONA_ADDRESS_MAX];
    char recovered_addr_M[MONA_ADDRESS_MAX];
    char recovered_addr_P[MONA_ADDRESS_MAX];
    char recovered_pubkey_hex[MONA_PUBKEY_COMPRESSED_SIZE * 2 + 1];
    uint8_t header;
    uint8_t recid;
    bool compressed;
    mona_txin_type_t txin_type_guess;
    bool valid;
} mona_verify_result_t;

typedef struct {
    /* Hash/HMAC */
    int (*sha256)(const uint8_t *data, size_t len, uint8_t out32[32]);
    int (*ripemd160)(const uint8_t *data, size_t len, uint8_t out20[20]);
    int (*hmac_sha256)(const uint8_t *key, size_t key_len,
                       const uint8_t *data, size_t data_len,
                       uint8_t out32[32]);

    /* Random bytes for createnewaddress() */
    int (*random_bytes)(uint8_t *out, size_t len);

    /* secp256k1 backend */
    int (*secp_pubkey_create_compressed)(const uint8_t secret32[32], uint8_t out33[33]);
    int (*secp_sign_recoverable_compact)(const uint8_t msg32[32],
                                         const uint8_t secret32[32],
                                         uint8_t out64[64],
                                         int *out_recid);
    int (*secp_recover_pubkey_compressed)(const uint8_t msg32[32],
                                          const uint8_t sig64[64],
                                          int recid,
                                          uint8_t out33[33]);
    int (*secp_verify_compact)(const uint8_t msg32[32],
                               const uint8_t sig64[64],
                               const uint8_t pub33[33]);
} mona_crypto_vtable_t;

/* Library setup */
void mona_set_crypto(const mona_crypto_vtable_t *vt);
const mona_crypto_vtable_t *mona_get_crypto(void);
const char *mona_strerror(mona_err_t err);

/* Core helpers */
mona_err_t mona_decode_wif_any(const char *wif_or_prefixed, mona_privkey_t *out);
mona_err_t mona_encode_wif(const mona_privkey_t *priv, char *out, size_t out_sz);
mona_err_t mona_keypair_from_secret(const uint8_t secret32[32], mona_address_info_t *out);
mona_err_t mona_message_hash(const uint8_t *msg, size_t msg_len, uint8_t out32[32]);

/* User-facing functions corresponding to the PHP library. */
mona_err_t mona_createnewaddress(mona_address_info_t *out, mona_privkey_t *out_priv);
mona_err_t mona_signmessage(const char *message,
                            const char *wif_or_prefixed,
                            char *out_sig_b64,
                            size_t out_sig_b64_sz,
                            mona_address_info_t *out_addrs);
mona_err_t mona_verifymessage(const char *address,
                              const char *message,
                              const char *signature_b64,
                              mona_verify_result_t *out);

#ifdef __cplusplus
}
#endif

#endif
