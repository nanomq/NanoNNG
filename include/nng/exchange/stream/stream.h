// Copyright 2024 NanoMQ Team, Inc. <jaylin@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
#ifndef STREAM_H
#define STREAM_H
#include "nng/supplemental/util/idhash.h"
#include "nng/supplemental/nanolib/log.h"

typedef struct stream_node {
	char *name;
	uint8_t id;
	void *(*decode)(void *, uint32_t, uint32_t *);
	void *(*encode)(void *, uint32_t, uint32_t *);
} stream_node;

int stream_register(char *name, uint8_t id,
					void *(*decode)(void *, uint32_t, uint32_t *),
					void *(*encode)(void *, uint32_t, uint32_t *));
int stream_unregister(uint8_t id);
void *stream_decode(uint8_t id, void *buf, uint32_t len, uint32_t *outlen);
void *stream_encode(uint8_t id, void *buf, uint32_t len, uint32_t *outlen);
int stream_sys_init(void);
void stream_sys_fini(void);

#endif
