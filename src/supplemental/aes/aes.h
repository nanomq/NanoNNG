#ifndef NNG_SUPPLEMENTAL_AES_AES_H
#define NNG_SUPPLEMENTAL_AES_AES_H

// Refer to nanomq/extern/aes_gcm.c.
char* nni_aes_gcm_decrypt(char *ciphertext, int ciphertext_len,
		char *key, char *tag, int *plaintext_lenp);
char* nni_aes_gcm_encrypt(char *plain, int plainsz, char *key,
		char **tagp, int *cipher_lenp);

#endif
