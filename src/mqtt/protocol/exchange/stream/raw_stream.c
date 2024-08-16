// Copyright 2024 NanoMQ Team, Inc. <jaylin@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.

#include "string.h"
#include "nng/exchange/stream/raw_stream.h"

void *raw_stream_decode(void *data, uint32_t len, uint32_t *outlen)
{
	log_error("raw_stream_decode\n");
	*outlen = len;
	return data;
}

void *raw_stream_encode(void *data, uint32_t len, uint32_t *outlen)
{
	log_error("raw_stream_encode\n");
	*outlen = len;
	return data;
}

int raw_stream_register()
{
	int ret = 0;
	char *name = NULL;
	name = nng_alloc(strlen(RAW_STREAM_NAME) + 1);
	if (name == NULL) {
		return NNG_ENOMEM;
	}

	strcpy(name, RAW_STREAM_NAME);

	ret = stream_register(name, RAW_STREAM_ID, raw_stream_decode, raw_stream_encode);
	if (ret != 0) {
		nng_free(name, strlen(name) + 1);
		return ret;
	}

	return 0;
}
