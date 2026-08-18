#ifndef BASE64_H
#define BASE64_H
#include "nng/nng.h"

#define BASE64_ENCODE_OUT_SIZE(s)                                           \
    (((uint64_t)(s) > (((uint64_t)SIZE_MAX - 1) / 4) * 3 - 2)               \
        ? 0                                                                 \
        : (size_t) ((((((uint64_t)(s)) + 2) / 3) * 4) + 1))

#define BASE64_DECODE_OUT_SIZE(s)                                           \
    ((size_t) (((((uint64_t)(s))) / 4) * 3))
NNG_DECL unsigned int
base64_encode(const unsigned char *in, unsigned int inlen, char *out);

/*
 * return values is out length
 */
NNG_DECL unsigned int
base64_decode(const char *in, unsigned int inlen, unsigned char *out);

#endif /* BASE64_H */