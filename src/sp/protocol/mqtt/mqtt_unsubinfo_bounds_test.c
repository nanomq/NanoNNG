// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt). A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.

#include <nuts.h>

#include "core/nng_impl.h"
#include "core/sockimpl.h"
#include "nng/protocol/mqtt/mqtt_parser.h"

static struct subinfo *
seed_list(nni_list *list)
{
	struct subinfo *seed = nng_zalloc(sizeof(*seed));
	NUTS_ASSERT(seed != NULL);
	seed->topic = nng_strdup("key");
	NUTS_ASSERT(seed->topic != NULL);
	seed->qos = 1;
	seed->subid = 11;
	NNI_LIST_INIT(list, struct subinfo, node);
	NNI_LIST_NODE_INIT(&seed->node);
	nni_list_append(list, seed);
	return seed;
}

static void
check_unchanged(nni_list *list, struct subinfo *seed)
{
	NUTS_ASSERT(nni_list_first(list) == seed);
	NUTS_ASSERT(nni_list_next(list, seed) == NULL);
	NUTS_MATCH(seed->topic, "key");
	NUTS_TRUE(seed->qos == 1 && seed->subid == 11);
}

static void
free_seed(nni_list *list, struct subinfo *seed)
{
	nni_list_remove(list, seed);
	nng_strfree(seed->topic);
	nng_free(seed, sizeof(*seed));
}

static void
test_oversized_property_span(void)
{
	nng_msg *msg;
	NUTS_PASS(nng_msg_alloc(&msg, 1024));
	// Allocate directly at a power of two: no tail slack may hide the read.
	NUTS_ASSERT(nng_msg_capacity(msg) == 1024);
	uint8_t *body = nng_msg_body(msg);
	memset(body, 0, 1024);
	body[1] = 1;
	// 204 empty User Properties advance the old scanner from 4 to 1024.
	for (size_t pos = 4; pos < 1024; pos += 5) {
		body[pos] = USER_PROPERTY;
	}

	nni_list list;
	struct subinfo *seed = seed_list(&list);
	// Each length fits in the whole body but exceeds the available span.
	for (uint32_t prop_len = 1024; prop_len > 1020; prop_len--) {
		NUTS_ASSERT(put_var_integer(body + 2, prop_len) == 2);
		NUTS_TRUE(nmq_unsubinfo_decode(msg, &list,
		              MQTT_PROTOCOL_VERSION_v5) == -1);
		check_unchanged(&list, seed);
	}
	free_seed(&list, seed);
	NUTS_TRUE(nmq_unsubinfo_decode(msg, &list,
	              MQTT_PROTOCOL_VERSION_v5) == -1);
	NUTS_TRUE(nni_list_empty(&list));
	nng_msg_free(msg);
}

static void
test_truncated_user_property(void)
{
	static const struct {
		size_t  len;
		uint8_t props[6];
	} cases[] = {
		{ 1, { USER_PROPERTY } },
		{ 2, { USER_PROPERTY, 0 } },
		{ 3, { USER_PROPERTY, 0, 0 } },
		{ 4, { USER_PROPERTY, 0, 0, 0 } },
		{ 5, { USER_PROPERTY, 0, 1, 'k', 0 } },
		{ 6, { USER_PROPERTY, 0, 1, 'k', 0, 1 } },
	};
	const uint8_t filter[] = { 0, 3, 'k', 'e', 'y' };
	for (size_t i = 0; i < NNI_NUM_ELEMENTS(cases); i++) {
		nng_msg *msg;
		uint8_t  header[] = { 0, 1, (uint8_t) cases[i].len };
		NUTS_PASS(nng_msg_alloc(&msg, 0));
		NUTS_PASS(nng_msg_append(msg, header, sizeof(header)));
		NUTS_PASS(nng_msg_append(msg, cases[i].props, cases[i].len));
		NUTS_PASS(nng_msg_append(msg, filter, sizeof(filter)));
		nni_list list;
		struct subinfo *seed = seed_list(&list);
		// Neither string may borrow bytes from the following topic payload.
		NUTS_TRUE(nmq_unsubinfo_decode(msg, &list,
		              MQTT_PROTOCOL_VERSION_v5) < 0);
		check_unchanged(&list, seed);
		free_seed(&list, seed);
		nng_msg_free(msg);
	}
}

static void
test_valid_user_properties(void)
{
	nng_msg *msg;
	NUTS_PASS(nng_msg_alloc(&msg, 1024));
	NUTS_ASSERT(nng_msg_capacity(msg) == 1024);
	uint8_t *body = nng_msg_body(msg);
	memset(body, 0, 1024);
	body[1] = 1;
	// 203 empty User Properties leave exactly five bytes for topic "key".
	NUTS_ASSERT(put_var_integer(body + 2, 1015) == 2);
	for (size_t pos = 4; pos < 1019; pos += 5) {
		body[pos] = USER_PROPERTY;
	}
	const uint8_t filter[] = { 0, 3, 'k', 'e', 'y' };
	memcpy(body + 1019, filter, sizeof(filter));

	nni_list list;
	seed_list(&list);
	NUTS_TRUE(nmq_unsubinfo_decode(msg, &list,
	              MQTT_PROTOCOL_VERSION_v5) == 1);
	NUTS_TRUE(nni_list_empty(&list));
	NUTS_TRUE(nmq_unsubinfo_decode(msg, &list,
	              MQTT_PROTOCOL_VERSION_v5) == 1);
	NUTS_TRUE(nni_list_empty(&list));
	nng_msg_free(msg);
}

NUTS_TESTS = {
	{ "unsubinfo oversized property span", test_oversized_property_span },
	{ "unsubinfo truncated user property", test_truncated_user_property },
	{ "unsubinfo valid user properties", test_valid_user_properties },
	{ NULL, NULL },
};
