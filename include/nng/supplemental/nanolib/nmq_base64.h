#ifndef NMQ_BASE64_H
#define NMQ_BASE64_H
#include "nng/nng.h"

#define BASE64_ENCODE_OUT_SIZE(s)                                           \
    (((uint64_t)(s) > (((uint64_t)SIZE_MAX - 1) / 4) * 3 - 2)               \
        ? 0                                                                 \
        : (size_t) ((((((uint64_t)(s)) + 2) / 3) * 4) + 1))

 #define BASE64_DECODE_OUT_SIZE(s)                                           \
    ((size_t) ((((((uint64_t)(s)) + 3)) / 4) * 3))

/*
 * Encodes in_len bytes into a NUL terminated base64 string.
 * Returns the number of characters written, excluding the NUL,
 * or (size_t)-1 if out_len is too small.
 */
NNG_DECL size_t
nmq_base64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_len);

/*
 * Decodes in_len base64 characters.
 * Returns the number of bytes written, or (size_t)-1 if out_len is too small.
 */
NNG_DECL size_t
nmq_base64_decode(const char *in, size_t in_len, uint8_t *out, size_t out_len);

#endif /* NMQ_BASE64_H */