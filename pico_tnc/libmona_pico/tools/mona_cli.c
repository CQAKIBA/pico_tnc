/*
 * Copyright (c) 2026 Daisuke JA1UMW / CQAKIBA.TOKYO
 * Released under the MIT License.
 * See LICENSE for details.
 */

#include "mona_compat.h"
#include "mona_backend_openssl.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage:\n"
        "  %s createnewaddress\n"
        "  %s signmessage <wif-or-prefixed-wif> <message>\n"
        "  %s verifymessage <address> <message> <base64-signature>\n",
        prog, prog, prog);
}

static const char *txin_name(mona_txin_type_t t) {
    switch (t) {
        case MONA_TXIN_P2PKH: return "p2pkh";
        case MONA_TXIN_P2WPKH_P2SH: return "p2wpkh-p2sh";
        case MONA_TXIN_P2WPKH: return "p2wpkh";
        default: return "unknown";
    }
}

int main(int argc, char **argv) {
    mona_use_openssl_backend();

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "createnewaddress") == 0) {
        mona_address_info_t info;
        mona_privkey_t priv;
        mona_err_t err = mona_createnewaddress(&info, &priv);
        if (err != MONA_OK) {
            fprintf(stderr, "error: %s\n", mona_strerror(err));
            return 2;
        }
        printf("privkey_wif=%s\n", info.privkey_wif);
        printf("privkey_raw_hex=%s\n", info.privkey_raw_hex);
        printf("address_mona1=%s\n", info.addr_mona1);
        printf("address_M=%s\n", info.addr_M);
        printf("address_P=%s\n", info.addr_P);
        (void)priv;
        return 0;
    }

    if (strcmp(argv[1], "signmessage") == 0) {
        if (argc != 4) {
            usage(argv[0]);
            return 1;
        }
        char sig[MONA_SIG_B64_MAX];
        mona_address_info_t info;
        mona_err_t err = mona_signmessage(argv[3], argv[2], sig, sizeof(sig), &info);
        if (err != MONA_OK) {
            fprintf(stderr, "error: %s\n", mona_strerror(err));
            return 2;
        }
        printf("signature=%s\n", sig);
        printf("address_mona1=%s\n", info.addr_mona1);
        printf("address_M=%s\n", info.addr_M);
        printf("address_P=%s\n", info.addr_P);
        return 0;
    }

    if (strcmp(argv[1], "verifymessage") == 0) {
        if (argc != 5) {
            usage(argv[0]);
            return 1;
        }
        mona_verify_result_t vr;
        mona_err_t err = mona_verifymessage(argv[2], argv[3], argv[4], &vr);
        if (err != MONA_OK) {
            fprintf(stderr, "error: %s\n", mona_strerror(err));
            return 2;
        }
        printf("valid=%s\n", vr.valid ? "true" : "false");
        printf("header=%u\n", (unsigned)vr.header);
        printf("recid=%u\n", (unsigned)vr.recid);
        printf("compressed=%s\n", vr.compressed ? "true" : "false");
        printf("txin_type_guess=%s\n", txin_name(vr.txin_type_guess));
        printf("recovered_pubkey_hex=%s\n", vr.recovered_pubkey_hex);
        printf("recovered_address_mona1=%s\n", vr.recovered_addr_mona1);
        printf("recovered_address_M=%s\n", vr.recovered_addr_M);
        printf("recovered_address_P=%s\n", vr.recovered_addr_P);
        return 0;
    }

    usage(argv[0]);
    return 1;
}
