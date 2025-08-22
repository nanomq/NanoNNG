#ifndef NNG_SUPPLEMENTAL_AES_AES_H
#define NNG_SUPPLEMENTAL_AES_AES_H

// Refer to nanomq/extern/aes_gcm.c.
char* nni_aes_gcm_decrypt(char *cipher, int cipher_len, char *key, int *plain_lenp);
char* nni_aes_gcm_encrypt(char *plain, int plain_len, char *key, int *cipher_lenp);

#endif
