// Copyright 2026 NanoMQ Team, Inc. <jaylin@emqx.io>
// SPDX-License-Identifier: MIT

#include "nng/exchange/stream/stream.h"
#include "nng/supplemental/nanolib/parquet.h"
#include <nuts.h>
#include <new>

/* Fail only this test thread's Nth nothrow allocation. Other allocator
 * calls, including NNG's malloc-based packet allocations, remain real. */
static thread_local unsigned fail_after;
static thread_local bool allocation_failed;

void *
operator new(std::size_t size, const std::nothrow_t &) noexcept
{
	if (fail_after != 0 && --fail_after == 0) {
		allocation_failed = true;
		return nullptr;
	}
	try {
		return ::operator new(size);
	} catch (...) {
		return nullptr;
	}
}

static parquet_data *
make_data(void)
{
	static uint64_t keys[] = { 1, 2 };
	static char ts[] = "ts", data[] = "data";
	static char *schema[] = { ts, data };
	static uint8_t payload[] = { 1, 2, 3 };
	auto columns = static_cast<parquet_data_packet ***>(
	    nng_alloc(sizeof(parquet_data_packet **)));
	auto packets = static_cast<parquet_data_packet *>(
	    nng_zalloc(2 * sizeof(parquet_data_packet)));
	NUTS_TRUE(columns != nullptr && packets != nullptr);
	columns[0] = static_cast<parquet_data_packet **>(
	    nng_alloc(2 * sizeof(parquet_data_packet *)));
	NUTS_TRUE(columns[0] != nullptr);
	for (unsigned r = 0; r < 2; r++) {
		packets[r].data = payload;
		packets[r].size = sizeof(payload);
		columns[0][r] = &packets[r];
	}
	parquet_data *out = parquet_data_alloc(schema, columns, keys, 1, 2);
	if (out == nullptr) {
		nng_free(packets, 2 * sizeof(*packets));
		nng_free(columns[0], 2 * sizeof(*columns[0]));
		nng_free(columns, sizeof(*columns));
		return nullptr;
	}
	parquet_data_payloads_borrowed(out);
	parquet_data_ts_borrowed(out);
	parquet_data_schema_borrowed(out);
	parquet_data_packets_contiguous(out);
	return out;
}

static void
test_data_allocation_failure(void)
{
	fail_after = 1;
	allocation_failed = false;
	parquet_data *data = make_data();
	fail_after = 0;
	NUTS_TRUE(allocation_failed);
	NUTS_TRUE(data == nullptr);
	parquet_data_free(data);
}

static void
check_object_allocation_failure(unsigned nth)
{
	parquet_data *data = make_data();
	NUTS_TRUE(data != nullptr);
	fail_after = nth;
	allocation_failed = false;
	parquet_object *obj = parquet_object_alloc(
	    data, WRITE_RAW, nullptr, nullptr, nullptr);
	fail_after = 0;
	NUTS_TRUE(allocation_failed);
	NUTS_TRUE(obj == nullptr);
	if (obj != nullptr) {
		delete obj->ranges;
		delete obj;
	}
	/* Both failure points must leave the encoded data owned by the caller. */
	auto view = reinterpret_cast<struct stream_data_out *>(data);
	NUTS_TRUE(view->ts[0] == 1 && view->payload_arr[0][1]->data[0] == 1);

	/* A retry using that same encoded data must succeed. */
	obj = parquet_object_alloc(data, WRITE_RAW, nullptr, nullptr, nullptr);
	NUTS_TRUE(obj != nullptr && obj->data == data && obj->ranges != nullptr);
	NUTS_TRUE(obj->ranges->size == 0 && obj->ranges->range == nullptr);
	delete obj->ranges;
	delete obj;
	parquet_data_free(data);
}

static void test_object_allocation_failure(void)
{
	check_object_allocation_failure(1);
}

static void test_ranges_allocation_failure(void)
{
	check_object_allocation_failure(2);
}

NUTS_TESTS = {
	{ "parquet data allocation failure", test_data_allocation_failure },
	{ "parquet object allocation failure", test_object_allocation_failure },
	{ "parquet ranges allocation failure", test_ranges_allocation_failure },
	{ nullptr, nullptr },
};
