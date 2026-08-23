//
// Copyright 2026 NanoMQ Team, Inc. <jaylin@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include "nng/supplemental/nanolib/nmq_base64.h"
#include "supplemental/base64/base64.h"
#include <nng/nng.h>

size_t
nmq_base64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_len)
{
	return nni_base64_encode(in, in_len, out, out_len);
}

size_t
nmq_base64_decode(const char *in, size_t in_len, uint8_t *out, size_t out_len)
{
	return nni_base64_decode(in, in_len, out, out_len);
}

static int
base64_value(char ch)
{
	if (ch >= 'A' && ch <= 'Z') {
		return ch - 'A';
	}
	if (ch >= 'a' && ch <= 'z') {
		return ch - 'a' + 26;
	}
	if (ch >= '0' && ch <= '9') {
		return ch - '0' + 52;
	}
	if (ch == '+') {
		return 62;
	}
	if (ch == '/') {
		return 63;
	}
	return -1;
}

size_t
nmq_base64_decode_strict(
	const char *in, size_t in_len, uint8_t *out, size_t out_len)
{
	size_t padding = 0;

	if (in == NULL || (in_len != 0 && out == NULL) || (in_len & 3) != 0) {
		return (size_t) -1;
	}

	for (size_t i = 0; i < in_len; i++) {
		char ch = in[i];

		if (ch == '=') {
			if (padding == 0) {
				padding = 1;
			} else if (padding == 1 && i == in_len - 1) {
				padding = 2;
			} else {
				return (size_t) -1;
			}
		} else if (base64_value(ch) >= 0) {
			if (padding != 0) {
				return (size_t) -1;
			}
		} else {
			return (size_t) -1;
		}
	}

	if (padding == 1 &&
	    (base64_value(in[in_len - 2]) & 0x03) != 0) {
		return (size_t) -1;
	}
	if (padding == 2 &&
	    (base64_value(in[in_len - 3]) & 0x0f) != 0) {
		return (size_t) -1;
	}

	return nni_base64_decode(in, in_len, out, out_len);
}
