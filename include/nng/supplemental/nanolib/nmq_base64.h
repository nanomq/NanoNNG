#ifndef NMQ_BASE64_H
#define NMQ_BASE64_H

#include "nng/nng.h"

static inline size_t
nmq_base64_encode_out_size(size_t size)
{
	uint64_t groups;

	if ((uint64_t) size > UINT64_MAX - 2) {
		return 0;
	}
	groups = ((uint64_t) size + 2) / 3;
	if (groups > (((uint64_t) SIZE_MAX - 1) / 4)) {
		return 0;
	}
	return (size_t) (groups * 4 + 1);
}

#define BASE64_ENCODE_OUT_SIZE(s) nmq_base64_encode_out_size((size_t) (s))

#define BASE64_DECODE_OUT_SIZE(s)                                           \
	(((uint64_t) (s) > (UINT64_MAX - 3)) ? 0 :                              \
	    ((((((uint64_t) (s) + 3) / 4) * 3) > (uint64_t) SIZE_MAX) ? 0 :       \
	        (size_t) (((((uint64_t) (s) + 3) / 4) * 3))))

/*
 * Encodes in_len bytes into a NUL-terminated Base64 string.
 * Returns the number of characters written, or (size_t)-1 on failure.
 */
NNG_DECL size_t
nmq_base64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_len);

/*
 * Decodes in_len Base64 characters.
 * Returns the number of bytes written, or (size_t)-1 on failure.
 */
NNG_DECL size_t
nmq_base64_decode(const char *in, size_t in_len, uint8_t *out, size_t out_len);

/*
 * Strictly validates and decodes padded Base64 without whitespace.
 * Returns the number of bytes written, or (size_t)-1 on invalid input or
 * insufficient output space.
 */
NNG_DECL size_t
nmq_base64_decode_strict(
	const char *in, size_t in_len, uint8_t *out, size_t out_len);

#endif /* NMQ_BASE64_H */
