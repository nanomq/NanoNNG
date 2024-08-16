// Copyright 2024 NanoMQ Team, Inc. <jaylin@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.

#include "string.h"
//#include "core/nng_impl.h"
#include "nng/exchange/stream/stream.h"
#include "nng/exchange/stream/raw_stream.h"

nng_id_map *stream_node_map = NULL;

int stream_register(char *name,
					uint8_t id,
					void *(*decode)(void *, uint32_t, uint32_t *),
					void *(*encode)(void *, uint32_t, uint32_t *))
{
	stream_node *snode = NULL;
	snode = nng_id_get(stream_node_map, id);
	if (snode != NULL) {
		return NNG_EEXIST;
	}

	snode = nng_alloc(sizeof(*snode));
	if (snode == NULL) {
		return NNG_ENOMEM;
	}

	snode->name   = name;
	snode->id     = id;
	snode->decode = decode;
	snode->encode = encode;

	nng_id_set(stream_node_map, id, snode);

	return 0;
}

int stream_unregister(uint8_t id)
{
	stream_node *snode = NULL;
	snode = nng_id_get(stream_node_map, id);
	if (snode == NULL) {
		return NNG_ENOENT;
	}

	nng_id_remove(stream_node_map, id);

	nng_free(snode->name, strlen(snode->name) + 1);
	nng_free(snode, sizeof(*snode));

	return 0;
}

void *stream_decode(uint8_t id, void *buf, uint32_t len, uint32_t *outlen)
{
	stream_node *snode = NULL;
	snode = nng_id_get(stream_node_map, id);
	if (snode == NULL) {
		return NULL;
	}

	return (snode->decode(buf, len, outlen));
}

void *stream_encode(uint8_t id, void *buf, uint32_t len, uint32_t *outlen)
{
	log_error("stream_encode");

	stream_node *snode = NULL;
	snode = nng_id_get(stream_node_map, id);
	if (snode == NULL) {
		return NULL;
	}

	log_error("stream_encode 2");
	return (snode->encode(buf, len, outlen));
}

#define UNUSED(x) ((void) x)

int stream_node_destory(void *id, void *value)
{
	UNUSED(id);
	stream_node *snode = value;

	nng_free(snode->name, strlen(snode->name) + 1);
	nng_free(snode, sizeof(*snode));

	return 0;
}

int stream_sys_init(void)
{
	int ret = 0;

	ret = nng_id_map_alloc(&stream_node_map, 0, 0, false);
	if (ret != 0) {
		return ret;
	}

	raw_stream_register();

	return 0;
}

void stream_sys_fini(void)
{
	stream_node *snode = NULL;

	nng_id_map_foreach(stream_node_map, stream_node_destory);
	nng_id_map_free(stream_node_map);

	return;
}
