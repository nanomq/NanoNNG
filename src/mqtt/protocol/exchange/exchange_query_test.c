// Copyright 2026 NanoMQ Team, Inc.
// SPDX-License-Identifier: MIT

#include "nng/nng.h"
#include "nng/exchange/exchange.h"
#include "nng/exchange/exchange_client.h"
#include "nng/exchange/stream/someip_stream.h"
#include "nng/protocol/pair0/pair.h"
#include "stream/someip_batch.h"
#include <nuts.h>
#include <pthread.h>

/* Supply the batch a file covering keys 10..20 would return. The actual
 * codec and Exchange sync/async reply paths run unchanged. */
static pthread_mutex_t result_mtx = PTHREAD_MUTEX_INITIALIZER;
static unsigned freed_results, empty_results;
static bool corrupt_batch;

void __real_stream_decoded_data_free(struct stream_decoded_data *);

void
__wrap_stream_decoded_data_free(struct stream_decoded_data *data)
{
	if (data != NULL) {
		pthread_mutex_lock(&result_mtx);
		freed_results++;
		if (data->len == 0) empty_results++;
		pthread_mutex_unlock(&result_mtx);
	}
	__real_stream_decoded_data_free(data);
}

parquet_filename_range **
__wrap_parquet_get_file_ranges(uint64_t start, uint64_t end, char *topic)
{
	(void) topic;
	parquet_filename_range **ranges = nng_zalloc(2 * sizeof(*ranges));
	ranges[0] = nng_zalloc(sizeof(*ranges[0]));
	ranges[0]->filename = nng_strdup("someip_10~20.parquet");
	ranges[0]->keys[0] = start;
	ranges[0]->keys[1] = end;
	return ranges;
}

parquet_data_ret **
__wrap_parquet_get_data_packets_in_range_by_column(parquet_filename_range *range,
    const char *topic, const char **schema, uint16_t schema_len, uint32_t *size)
{
	(void) topic;
	(void) schema;
	(void) schema_len;
	uint8_t frame[16] = { [7] = 8, [12] = 1 };
	someip_record records[] = {
		{ .batch_index = 10, .frame = frame, .size = sizeof(frame) },
		{ .batch_index = 20, .frame = frame, .size = sizeof(frame) },
	};
	parquet_data *encoded = someip_encode_records(records, 2, true);
	struct stream_data_out *view = (struct stream_data_out *) encoded;
	someip_batch *batch = view->someip_data;
	batch->start_key = range->keys[0];
	batch->end_key = range->keys[1];
	if (corrupt_batch) batch->ts[0]++;
	parquet_data_ret **results = nng_zalloc(sizeof(*results));
	results[0] = nng_zalloc(sizeof(*results[0]));
	results[0]->row_len = 2;
	results[0]->col_len = 5;
	results[0]->someip_data = batch;
	view->someip_data = NULL;
	/* ts aliases the batch's key column; the generic destructor must not
	 * release it after the batch has moved to the reader result. */
	view->ts = NULL;
	parquet_data_free(encoded);
	*size = 1;
	return results;
}

static void
test_query_results_impl(bool close_pair_first)
{
	NUTS_PASS(stream_sys_init());
	nng_socket exchange, server, client;
	NUTS_PASS(nng_pair0_open(&server));
	NUTS_PASS(nng_pair0_open(&client));
	NUTS_PASS(nng_socket_set_ms(client, NNG_OPT_RECVTIMEO, 3000));
	NUTS_PASS(nng_listen(server, "inproc://exchange-query-results", NULL, 0));
	NUTS_PASS(nng_dial(client, "inproc://exchange-query-results", NULL, 0));
	exchange.data = &server;
	NUTS_PASS(nng_exchange_client_open(&exchange));
	ringBuffer_node ring = { .name = "query", .cap = 8, .fullOp = RB_FULL_NONE };
	ringBuffer_node *rings[] = { &ring };
	conf_exchange_node config = { .name = "query", .topic = "someip/query",
		.streamType = SOMEIP_CHECKED_STREAM_ID, .rbufs = rings, .rbufs_sz = 1 };
	NUTS_PASS(nng_socket_set_ptr(exchange, NNG_OPT_EXCHANGE_BIND, &config));
	for (unsigned sync = 0; sync < 2; sync++) {
		for (unsigned mode = 0; mode < 3; mode++) {
			pthread_mutex_lock(&result_mtx);
			freed_results = empty_results = 0;
			corrupt_batch = mode == 2;
			pthread_mutex_unlock(&result_mtx);
			bool empty = mode == 0;
			char command[32];
			snprintf(command, sizeof(command), "%s-%u-%u",
			    sync ? "sync" : "async", empty ? 11 : 10, empty ? 19 : 20);
			NUTS_PASS(nng_send(client, command, strlen(command) + 1, 0));
			if (sync || mode == 1) {
				nng_msg *data;
				NUTS_PASS(nng_recvmsg(client, &data, 0));
				if (mode == 1) NUTS_TRUE(nng_msg_len(data) > 0);
				else NUTS_TRUE(nng_msg_len(data) == 0);
				nng_msg_free(data);
			}
			nng_msg *eof;
			NUTS_PASS(nng_recvmsg(client, &eof, 0));
			const uint8_t ok[] = { 0x0b, 0xad, '0', ',', '1' };
			const uint8_t bad[] = { 0x0b, 0xad, '8', ',', '1' };
			NUTS_TRUE(nng_msg_len(eof) == sizeof(ok));
			NUTS_TRUE(memcmp(nng_msg_body(eof), mode == 2 ? bad : ok, sizeof(ok)) == 0);
			nng_msg_free(eof);
			pthread_mutex_lock(&result_mtx);
			NUTS_TRUE(freed_results == (mode == 2 ? 0 : 1));
			NUTS_TRUE(empty_results == (empty ? 1 : 0));
			pthread_mutex_unlock(&result_mtx);
		}
	}
	/* A wire command need not contain a trailing NUL. Keep its parser and
	 * debug logging within the received message, and reject embedded NULs. */
	pthread_mutex_lock(&result_mtx);
	corrupt_batch = false;
	pthread_mutex_unlock(&result_mtx);
	const char command[] = "sync-10-20";
	NUTS_PASS(nng_send(client, (void *) command, sizeof(command) - 1, 0));
	nng_msg *reply;
	NUTS_PASS(nng_recvmsg(client, &reply, 0));
	NUTS_TRUE(nng_msg_len(reply) > 0);
	nng_msg_free(reply);
	NUTS_PASS(nng_recvmsg(client, &reply, 0));
	const uint8_t ok[] = { 0x0b, 0xad, '0', ',', '1' };
	NUTS_TRUE(nng_msg_len(reply) == sizeof(ok));
	NUTS_TRUE(memcmp(nng_msg_body(reply), ok, sizeof(ok)) == 0);
	nng_msg_free(reply);

	const char embedded[] = "sync-10-20\0ignored";
	const uint8_t bad_key[] = { 0x0b, 0xad, '4', ',', '4' };
	for (unsigned empty = 0; empty < 2; ++empty) {
		NUTS_PASS(nng_send(client, (void *) embedded,
		    empty ? 0 : sizeof(embedded), 0));
		NUTS_PASS(nng_recvmsg(client, &reply, 0));
		NUTS_TRUE(nng_msg_len(reply) == sizeof(bad_key));
		NUTS_TRUE(memcmp(nng_msg_body(reply), bad_key, sizeof(bad_key)) == 0);
		nng_msg_free(reply);
	}
	/* A parser failure must also release the command and rearm receive. */
	const char invalid[] = "sync-";
	const uint8_t bad_cmd[] = { 0x0b, 0xad, '5', ',', '5' };
	NUTS_PASS(nng_send(client, (void *) invalid, sizeof(invalid) - 1, 0));
	NUTS_PASS(nng_recvmsg(client, &reply, 0));
	NUTS_TRUE(nng_msg_len(reply) == sizeof(bad_cmd));
	NUTS_TRUE(memcmp(nng_msg_body(reply), bad_cmd, sizeof(bad_cmd)) == 0);
	nng_msg_free(reply);

	/* With a receive pending, both cancellation of the Exchange and failure
	 * from closing its pair socket must finish without revisiting old data. */
	if (close_pair_first) NUTS_PASS(nng_close(server));
	NUTS_PASS(nng_close(exchange));
	if (!close_pair_first) NUTS_PASS(nng_close(server));
	NUTS_PASS(nng_close(client));
	stream_sys_fini();
}

static void test_query_results(void) { test_query_results_impl(false); }
static void test_query_transport_close(void) { test_query_results_impl(true); }

NUTS_TESTS = {
	{ "SOME/IP empty, populated and corrupt sync/async queries", test_query_results },
	{ "SOME/IP query transport closes with receive pending", test_query_transport_close },
	{ NULL, NULL },
};
