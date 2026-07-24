#ifndef NNG_TLS_EXTERN_PRIVATE_KEY_H
#define NNG_TLS_EXTERN_PRIVATE_KEY_H

#ifdef __cplusplus
extern "C" {
#endif

int getPrivatekeyToSign(const char *vendor, const unsigned char *digest,
    int digest_len, unsigned char *signature, int signature_capacity);

#ifdef __cplusplus
}
#endif

#endif