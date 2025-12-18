// Copyright 2024 NanoMQ Team, Inc. <jaylin@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.

#include <nng/nng.h>
#include <nng/supplemental/util/idhash.h>
#include <nuts.h>

void
test_id_count_empty_map(void)
{
	nng_id_map *map;
	int rv = nng_id_map_alloc(&map, 0, 100, 0);
	NUTS_PASS(rv);
	
	uint32_t count = nng_id_count(map);
	NUTS_TRUE(count == 0);
	
	nng_id_map_free(map);
}

void
test_id_count_single_element(void)
{
	nng_id_map *map;
	int rv = nng_id_map_alloc(&map, 0, 100, 0);
	NUTS_PASS(rv);
	
	int value = 42;
	rv = nng_id_set(map, 1, &value);
	NUTS_PASS(rv);
	
	uint32_t count = nng_id_count(map);
	NUTS_TRUE(count == 1);
	
	nng_id_map_free(map);
}

void
test_id_count_multiple_elements(void)
{
	nng_id_map *map;
	int rv = nng_id_map_alloc(&map, 0, 1000, 0);
	NUTS_PASS(rv);
	
	int values[10];
	for (int i = 0; i < 10; i++) {
		values[i] = i * 100;
		rv = nng_id_set(map, i, &values[i]);
		NUTS_PASS(rv);
		
		uint32_t count = nng_id_count(map);
		NUTS_TRUE(count == (uint32_t)(i + 1));
	}
	
	nng_id_map_free(map);
}

void
test_id_count_after_remove(void)
{
	nng_id_map *map;
	int rv = nng_id_map_alloc(&map, 0, 100, 0);
	NUTS_PASS(rv);
	
	int values[5];
	for (int i = 0; i < 5; i++) {
		values[i] = i;
		rv = nng_id_set(map, i, &values[i]);
		NUTS_PASS(rv);
	}
	
	uint32_t count = nng_id_count(map);
	NUTS_TRUE(count == 5);
	
	// Remove some elements
	rv = nng_id_remove(map, 2);
	NUTS_PASS(rv);
	count = nng_id_count(map);
	NUTS_TRUE(count == 4);
	
	rv = nng_id_remove(map, 0);
	NUTS_PASS(rv);
	count = nng_id_count(map);
	NUTS_TRUE(count == 3);
	
	rv = nng_id_remove(map, 4);
	NUTS_PASS(rv);
	count = nng_id_count(map);
	NUTS_TRUE(count == 2);
	
	nng_id_map_free(map);
}

void
test_id_count_after_clear(void)
{
	nng_id_map *map;
	int rv = nng_id_map_alloc(&map, 0, 100, 0);
	NUTS_PASS(rv);
	
	int values[10];
	for (int i = 0; i < 10; i++) {
		values[i] = i;
		rv = nng_id_set(map, i, &values[i]);
		NUTS_PASS(rv);
	}
	
	uint32_t count = nng_id_count(map);
	NUTS_TRUE(count == 10);
	
	// Remove all elements
	for (int i = 0; i < 10; i++) {
		rv = nng_id_remove(map, i);
		NUTS_PASS(rv);
	}
	
	count = nng_id_count(map);
	NUTS_TRUE(count == 0);
	
	nng_id_map_free(map);
}

void
test_id_count_with_gaps(void)
{
	nng_id_map *map;
	int rv = nng_id_map_alloc(&map, 0, 1000, 0);
	NUTS_PASS(rv);
	
	// Add elements with gaps
	int values[5];
	uint64_t ids[] = {1, 10, 100, 500, 999};
	
	for (int i = 0; i < 5; i++) {
		values[i] = i;
		rv = nng_id_set(map, ids[i], &values[i]);
		NUTS_PASS(rv);
	}
	
	uint32_t count = nng_id_count(map);
	NUTS_TRUE(count == 5);
	
	nng_id_map_free(map);
}

void
test_id_count_replace_element(void)
{
	nng_id_map *map;
	int rv = nng_id_map_alloc(&map, 0, 100, 0);
	NUTS_PASS(rv);
	
	int value1 = 42;
	int value2 = 84;
	
	rv = nng_id_set(map, 5, &value1);
	NUTS_PASS(rv);
	
	uint32_t count = nng_id_count(map);
	NUTS_TRUE(count == 1);
	
	// Replace the same ID
	rv = nng_id_set(map, 5, &value2);
	NUTS_PASS(rv);
	
	count = nng_id_count(map);
	NUTS_TRUE(count == 1); // Should still be 1
	
	nng_id_map_free(map);
}

void
test_id_count_large_map(void)
{
	nng_id_map *map;
	int rv = nng_id_map_alloc(&map, 0, 10000, 0);
	NUTS_PASS(rv);
	
	int *values = nng_alloc(sizeof(int) * 1000);
	NUTS_ASSERT(values != NULL);
	
	for (int i = 0; i < 1000; i++) {
		values[i] = i;
		rv = nng_id_set(map, i, &values[i]);
		NUTS_PASS(rv);
	}
	
	uint32_t count = nng_id_count(map);
	NUTS_TRUE(count == 1000);
	
	nng_free(values, sizeof(int) * 1000);
	nng_id_map_free(map);
}

void
test_id_count_boundary_ids(void)
{
	nng_id_map *map;
	int rv = nng_id_map_alloc(&map, 0, UINT32_MAX, 0);
	NUTS_PASS(rv);
	
	int values[3];
	
	// Test boundary IDs
	values[0] = 0;
	rv = nng_id_set(map, 0, &values[0]);
	NUTS_PASS(rv);
	
	values[1] = 1;
	rv = nng_id_set(map, UINT32_MAX / 2, &values[1]);
	NUTS_PASS(rv);
	
	values[2] = 2;
	rv = nng_id_set(map, UINT32_MAX, &values[2]);
	if (rv == 0) {
		uint32_t count = nng_id_count(map);
		NUTS_TRUE(count == 3);
	}
	
	nng_id_map_free(map);
}

void
test_id_count_concurrent_operations(void)
{
	nng_id_map *map;
	int rv = nng_id_map_alloc(&map, 0, 100, 0);
	NUTS_PASS(rv);
	
	int values[20];
	
	// Add 10 elements
	for (int i = 0; i < 10; i++) {
		values[i] = i;
		rv = nng_id_set(map, i, &values[i]);
		NUTS_PASS(rv);
	}
	
	uint32_t count = nng_id_count(map);
	NUTS_TRUE(count == 10);
	
	// Remove 5 and add 10 more
	for (int i = 0; i < 5; i++) {
		rv = nng_id_remove(map, i);
		NUTS_PASS(rv);
	}
	
	for (int i = 10; i < 20; i++) {
		values[i] = i;
		rv = nng_id_set(map, i, &values[i]);
		NUTS_PASS(rv);
	}
	
	count = nng_id_count(map);
	NUTS_TRUE(count == 15); // 5 remaining + 10 new
	
	nng_id_map_free(map);
}

NUTS_TESTS = {
	{ "id count empty map", test_id_count_empty_map },
	{ "id count single element", test_id_count_single_element },
	{ "id count multiple elements", test_id_count_multiple_elements },
	{ "id count after remove", test_id_count_after_remove },
	{ "id count after clear", test_id_count_after_clear },
	{ "id count with gaps", test_id_count_with_gaps },
	{ "id count replace element", test_id_count_replace_element },
	{ "id count large map", test_id_count_large_map },
	{ "id count boundary ids", test_id_count_boundary_ids },
	{ "id count concurrent operations", test_id_count_concurrent_operations },
	{ NULL, NULL },
};