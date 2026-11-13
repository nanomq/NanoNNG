// Copyright 2026 NanoMQ Team, Inc.
// SPDX-License-Identifier: MIT

#include "core/nng_impl.h"
#include <nuts.h>
#include <string.h>

static void
test_strndup_empty_fields(void)
{
	char *empty = nni_strndup(NULL, 0);
	NUTS_ASSERT(empty != NULL);
	NUTS_TRUE(empty[0] == '\0');
	nni_strfree(empty);

	empty = nni_strndup("ignored", 0);
	NUTS_ASSERT(empty != NULL);
	NUTS_TRUE(empty[0] == '\0');
	nni_strfree(empty);

	NUTS_TRUE(nni_strndup(NULL, 1) == NULL);
	NUTS_TRUE(nni_strndup(NULL, SIZE_MAX) == NULL);
}

static void
test_strndup_bounded_copy(void)
{
	char input[] = { 'a', 'b', 'c', 'd' };
	char *copy = nni_strndup(input, sizeof(input));
	NUTS_ASSERT(copy != NULL);
	NUTS_TRUE(strcmp(copy, "abcd") == 0);
	input[0] = 'z';
	NUTS_TRUE(copy[0] == 'a');
	nni_strfree(copy);

	copy = nni_strndup("abcdef", 3);
	NUTS_ASSERT(copy != NULL);
	NUTS_TRUE(strcmp(copy, "abc") == 0);
	nni_strfree(copy);

	char embedded[] = { 'x', '\0', 'y' };
	copy = nni_strndup(embedded, sizeof(embedded));
	NUTS_ASSERT(copy != NULL);
	NUTS_TRUE(strcmp(copy, "x") == 0);
	nni_strfree(copy);
}

NUTS_TESTS = {
	{ "strndup empty MQTT fields", test_strndup_empty_fields },
	{ "strndup bounded independent copy", test_strndup_bounded_copy },
	{ NULL, NULL },
};
