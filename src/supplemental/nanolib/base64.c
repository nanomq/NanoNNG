
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