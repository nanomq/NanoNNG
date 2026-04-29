#ifndef NNG_SUPPLEMENTAL_AES_AES_H
#define NNG_SUPPLEMENTAL_AES_AES_H

#include <stddef.h>
#include <stdint.h>

// Refer to nanomq/extern/aes_gcm.c.
const char *aes_gcm_get_method_by_key_len(size_t key_len);

uint8_t *nni_aes_gcm_decrypt(uint8_t *cipher, int cipher_len, uint8_t *key,
    size_t key_len, int *plain_lenp, const char *encrypt_method);

uint8_t *nni_aes_gcm_encrypt(uint8_t *plain, int plain_len, char *key,
    size_t key_len, int *cipher_lenp, const char *encrypt_method);

#endif
