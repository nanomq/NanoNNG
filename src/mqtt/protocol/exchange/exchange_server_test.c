#include "nng/nng.h"
#include "core/nng_impl.h"
#include "nng/exchange/exchange_client.h"
#include "nng/exchange/exchange.h"
#include "nng/supplemental/nanolib/cvector.h"
#include "core/defs.h"
#include <nuts.h>

#define UNUSED(x) ((void) x)

static inline void free_msg_list(nng_msg **msgList, nng_msg *msg, uint32_t *lenp, int freeMsg)
{
	for (uint32_t i = 0; i < *lenp; i++) {
		if (freeMsg) {
			nng_msg_free(msgList[i]);
		}
	}

	if (msg != NULL) {
		nng_msg_free(msg);
	}
	if (msgList != NULL) {
		nng_free(msgList, sizeof(nng_msg *) * (*lenp));
	}
	if (lenp != NULL) {
		nng_free(lenp, sizeof(uint32_t));
	}
}

static inline void client_get_and_clean_msgs(nng_socket sock, uint32_t *lenp, nng_msg ***msgList)
{
	nni_aio *aio = NULL;
	NUTS_PASS(nng_aio_alloc(&aio, NULL, NULL));

	nng_msg *msg;
	nng_time *tss = NULL;
	nng_msg_alloc(&msg, 0);

	tss = nng_alloc(sizeof(nng_time) * 3);
	tss[2] = 1;
	nng_msg_set_proto_data(msg, NULL, (void *)tss);

	nni_aio_set_msg(aio, msg);
	nng_recv_aio(sock, aio);
	nng_aio_wait(aio);

	*msgList = (nng_msg **)nng_aio_get_msg(aio);
	*lenp = (uintptr_t)nng_aio_get_prov_data(aio);

	if (tss != NULL) {
		nng_free(tss, sizeof(nng_time) * 3);
	}

	nng_msg_free(msg);
	nng_aio_free(aio);
}

static inline void client_get_msgs(nng_socket sock, uint64_t startKey, uint64_t endKey, bool snapshot, uint32_t *lenp, nng_msg ***msgList)
{
	nni_aio *aio = NULL;
	NUTS_PASS(nng_aio_alloc(&aio, NULL, NULL));

	nng_msg *msg;
	nng_time *tss = NULL;
	nng_msg_alloc(&msg, 0);

	if (endKey == 0 && !snapshot) {
		nng_msg_set_timestamp(msg, startKey);
		nng_msg_set_proto_data(msg, NULL, NULL);
	} else {
		tss = nng_alloc(sizeof(nng_time) * 3);
		tss[0] = startKey;
		tss[1] = endKey;
		tss[2] = snapshot ? NNG_EXCHANGE_RECV_SNAPSHOT : 0;
		nng_msg_set_proto_data(msg, NULL, (void *)tss);
	}
	nni_aio_set_msg(aio, msg);
	nng_recv_aio(sock, aio);
	nng_aio_wait(aio);

	*msgList = (nng_msg **)nng_aio_get_msg(aio);
	*lenp = (uintptr_t)nng_aio_get_prov_data(aio);

	if (tss != NULL) {
		nng_free(tss, sizeof(nng_time) * 3);
	}

	nng_msg_free(msg);
	nng_aio_free(aio);
}

//
// Publish a message to the given topic and with the given QoS.
static int
client_publish(nng_socket sock, const char *topic, uint64_t key, uint8_t *payload,
    uint32_t payload_len, uint8_t qos, bool verbose)
{
	UNUSED(verbose);
	// create a PUBLISH message
	nng_msg *pubmsg;
	nng_mqtt_msg_alloc(&pubmsg, 0);

	uint8_t *header = nng_msg_header(pubmsg);
	*header = *header | CMD_PUBLISH;
	nng_mqtt_msg_set_packet_type(pubmsg, NNG_MQTT_PUBLISH);
	nng_mqtt_msg_set_publish_dup(pubmsg, 0);
	nng_mqtt_msg_set_publish_qos(pubmsg, qos);
	nng_mqtt_msg_set_publish_retain(pubmsg, 0);
	nng_mqtt_msg_set_publish_topic(pubmsg, topic);
	nng_mqtt_msg_set_publish_topic_len(pubmsg, strlen(topic));
	nng_mqtt_msg_set_publish_payload(
	    pubmsg, (uint8_t *) payload, payload_len);
	nni_aio *aio = NULL;
	NUTS_PASS(nng_aio_alloc(&aio, NULL, NULL));


	nng_msg_set_timestamp(pubmsg, key);
	nng_mqtt_msg_encode(pubmsg);
	nni_aio_set_msg(aio, pubmsg);

	nng_send_aio(sock, aio);
	nng_aio_wait(aio);

	uint32_t *lenp = NULL;
	nng_msg **msgList = (nng_msg **)nng_aio_get_prov_data(aio);
	nng_msg *msg = nng_aio_get_msg(aio);
	if (msgList != NULL && msg != NULL) {
		lenp = nng_msg_get_proto_data(msg);
		free_msg_list(msgList, msg, lenp, 1);
	}

	int rv = nng_aio_result(aio);
	nng_aio_free(aio);
	return rv;
}

static void
test_rollover_index(nng_socket sock, enum fullOption mode)
{
	nng_aio *aio;
	nng_msg *cmd;
	nng_time params[3] = { 0, mode, 2 };
	NUTS_PASS(nng_aio_alloc(&aio, NULL, NULL));
	NUTS_PASS(nng_msg_alloc(&cmd, 0));
	nng_msg_set_proto_data(cmd, NULL, params);
	nng_aio_set_msg(aio, cmd);
	nng_recv_aio(sock, aio);
	nng_aio_wait(aio);
	NUTS_PASS(nng_aio_result(aio));
	nng_msg_free(cmd);
	nng_aio_free(aio);

	nni_id_map *index;
	NUTS_PASS(nng_socket_get_ptr(sock, NNG_OPT_EXCHANGE_GET_RBMSGMAP, (void **) &index));
	uint64_t first = UINT64_C(1) << 32;
	for (uint64_t i = 0; i < 10; i++) {
		NUTS_PASS(client_publish(sock, "topic1", first + i, NULL, 0, 0, false));
	}
	nng_msg **snapshot;
	uint32_t count;
	client_get_msgs(sock, first + 3, first + 5, true, &count, &snapshot);
	NUTS_TRUE(count == 3 && snapshot != NULL);
	NUTS_FAIL(client_publish(sock, "topic1", first + 3, NULL, 0, 0, false), NNG_EINVAL);
	NUTS_TRUE(nni_id_count(index) == 10);
	int rv = client_publish(sock, "topic1", first + 10, NULL, 0, 0, false);
	if (mode == RB_FULL_FILE) {
		NUTS_FAIL(rv, NNG_EINVAL);
		NUTS_TRUE(nni_id_count(index) == 0);
	} else {
		NUTS_PASS(rv);
		NUTS_TRUE(nni_id_count(index) == 1 && nni_id_get(index, first + 10) != NULL);
	}
	NUTS_TRUE(nni_id_get(index, first + 3) == NULL);
	for (uint32_t i = 0; i < count; i++) {
		NUTS_TRUE(nng_msg_get_timestamp(snapshot[i]) == first + 3 + i);
		nng_msg_free(snapshot[i]);
	}
	nng_free(snapshot, count * sizeof(*snapshot));
	NUTS_PASS(client_publish(sock, "topic1", first + 3, NULL, 0, 0, false));
	/* The reused key is out of order. A point lookup still finds it. */
	client_get_msgs(sock, first + 3, first + 3, true, &count, &snapshot);
	NUTS_TRUE(count == 1 && nng_msg_get_timestamp(snapshot[0]) == first + 3);
	nng_msg_free(snapshot[0]);
	nng_free(snapshot, sizeof(*snapshot));
	nng_msg **drained;
	client_get_and_clean_msgs(sock, &count, &drained);
	for (uint32_t i = 0; i < count; i++) {
		nng_msg_free(drained[i]);
	}
	nng_free(drained, count * sizeof(*drained));
	NUTS_TRUE(nni_id_count(index) == 0);
}

void
test_exchange_client(void)
{
	int rv = 0;
	uint64_t key = 0;
	nng_socket sock;

	nng_socket pair0_sock;
	nng_rep0_open(&pair0_sock);
	sock.data = &pair0_sock;

	NUTS_TRUE(nng_exchange_client_open(&sock) == 0);

	conf_exchange_node *conf = NULL;
	conf = nng_alloc(sizeof(conf_exchange_node));
	NUTS_TRUE(conf != NULL);
	conf->name = "exchange1";
	conf->topic = "topic1";

	ringBuffer_node *rb_node = NNI_ALLOC_STRUCT(rb_node);
	NUTS_TRUE(rb_node != NULL);
	rb_node->name = "ringBuffer1";
	rb_node->cap = 10;
	rb_node->fullOp = RB_FULL_NONE;

	conf->rbufs = NULL;

	cvector_push_back(conf->rbufs, rb_node);
	conf->rbufs_sz = cvector_size(conf->rbufs);

	nng_socket_set_ptr(sock, NNG_OPT_EXCHANGE_BIND, conf);

	key = 0;
	uint8_t payload[] = "message";
	client_publish(sock, "topic1", key, payload, 0, 0, 0);

	nni_msg *msg = NULL;
	nni_sock *nsock = NULL;

	rv = nni_sock_find(&nsock, sock.id);
	NUTS_TRUE(rv == 0 && nsock != NULL);
	nni_sock_rele(nsock);

	uint32_t *lenp;
	nng_msg **msgList = NULL;

	rv = exchange_client_get_msg_by_key(nni_sock_proto_data(nsock), key, &msg);
	NUTS_TRUE(rv == 0 && msg != NULL);

	rv = exchange_client_get_msgs_by_key(nni_sock_proto_data(nsock), key, 1, &msgList);
	NUTS_TRUE(rv == 0 && msgList != NULL);
	lenp = nng_alloc(sizeof(uint32_t));
	*lenp = 1;
	free_msg_list(msgList, NULL, lenp, 0);
	msgList = NULL;

	/* Use aio recv to get msgs by key */
	lenp = nng_alloc(sizeof(uint32_t));
	*lenp = 0;
	client_get_msgs(sock, key, 0, false, lenp, &msgList);
	NUTS_TRUE(*lenp == 1 && msgList != NULL);
	NUTS_TRUE(!nni_msg_shared(msgList[0]));
	free_msg_list(msgList, NULL, lenp, 0);

	/* Only one element in ringbuffer */
	msgList = NULL;
	rv = exchange_client_get_msgs_by_key(nni_sock_proto_data(nsock), key, 2, &msgList);
	NUTS_TRUE(rv == -1 && msgList == NULL);

	/* fuzz search start */
	lenp = nng_alloc(sizeof(uint32_t));
	rv = exchange_client_get_msgs_fuzz(nni_sock_proto_data(nsock), 0, 3, lenp, &msgList);
	NUTS_TRUE(rv == 0 && *lenp == 1 && msgList != NULL);
	free_msg_list(msgList, NULL, lenp, 0);

	msgList = NULL;
	uint32_t len = 0;
	rv = exchange_client_get_msgs_fuzz(nni_sock_proto_data(nsock), 2, 3, &len, &msgList);
	NUTS_TRUE(rv != 0 && len == 0 && msgList == NULL);
	/* fuzz search end */

	for (int i = 1; i < 10; i++) {
		key = i;
		client_publish(sock, "topic1", key, NULL, 0, 0, 0);
	}

	/* Use aio recv to get msgs by key */
	lenp = nng_alloc(sizeof(uint32_t));
	*lenp = 0;
	client_get_msgs(sock, 1, 10, false, lenp, &msgList);
	NUTS_TRUE(*lenp == 9 && msgList != NULL);
	NUTS_TRUE(!nni_msg_shared(msgList[0]));
	free_msg_list(msgList, NULL, lenp, 0);

	/* Ringbuffer is full and msgs returned need to free */
	NUTS_FAIL(client_publish(sock, "topic1", 10, NULL, 0, 0, 0), NNG_EINVAL);
	NUTS_TRUE(exchange_client_get_msg_by_key(nni_sock_proto_data(nsock), 10, &msg) != 0);

	/* get and clean up msgs */
	lenp = nng_alloc(sizeof(uint32_t));
	*lenp = 0;
	client_get_and_clean_msgs(sock, lenp, &msgList);
	NUTS_TRUE(*lenp == 10 && msgList != NULL);
	free_msg_list(msgList, NULL, lenp, 1);
	/* A flush removes the index entries, and exact lookups use all 64 bits. */
	msg = NULL;
	NUTS_TRUE(exchange_client_get_msg_by_key(nni_sock_proto_data(nsock), 1, &msg) != 0);
	key = (UINT64_C(1) << 32) + 1;
	client_publish(sock, "topic1", key, (uint8_t *) "data", 4, 0, false);
	lenp = nng_alloc(sizeof(*lenp));
	client_get_msgs(sock, key, 0, false, lenp, &msgList);
	NUTS_TRUE(*lenp == 1 && msgList != NULL);
	NUTS_TRUE(nng_msg_get_timestamp(msgList[0]) == key);
	free_msg_list(msgList, NULL, lenp, 0);

	uint32_t snapshot_count;
	nng_msg **snapshot;
	client_get_msgs(sock, key, key, true, &snapshot_count, &snapshot);
	NUTS_TRUE(snapshot_count == 1 && snapshot != NULL);
	NUTS_TRUE(nni_msg_shared(snapshot[0]));
	lenp = nng_alloc(sizeof(*lenp));
	client_get_and_clean_msgs(sock, lenp, &msgList);
	NUTS_TRUE(*lenp == 1 && msgList != NULL);
	free_msg_list(msgList, NULL, lenp, 1);
	uint32_t size;
	uint8_t *data = nng_mqtt_msg_get_publish_payload(snapshot[0], &size);
	NUTS_TRUE(size == 4 && memcmp(data, "data", 4) == 0);
	nng_msg_free(snapshot[0]);
	nng_free(snapshot, sizeof(*snapshot));
	test_rollover_index(sock, RB_FULL_DROP);
	test_rollover_index(sock, RB_FULL_RETURN);
#if !defined(SUPP_PARQUET) && !defined(SUPP_BLF)
	test_rollover_index(sock, RB_FULL_FILE);
#endif
	nni_sock_close(nsock);

	cvector_free(conf->rbufs);
	nng_free(conf, sizeof(conf_exchange_node));
	nng_free(rb_node, sizeof(ringBuffer_node));

	return;
}

static void
test_drain_before_bind(void)
{
	nng_socket peer, sock;
	NUTS_PASS(nng_rep0_open(&peer));
	sock.data = &peer;
	NUTS_PASS(nng_exchange_client_open(&sock));
	uint32_t count;
	nng_msg **msgs;
	client_get_and_clean_msgs(sock, &count, &msgs);
	NUTS_TRUE(count == 0 && msgs == NULL);
	NUTS_PASS(nng_close(sock));
	NUTS_PASS(nng_close(peer));
}

static void
test_exchange_replay_cmd_parse(void)
{
	nng_exchange_replay_cmd cmd;
	int                     rv;

	rv = nng_exchange_replay_cmd_parse(
	    "replay-100-200-10-canudp/replay", &cmd);
	NUTS_TRUE(rv == 0);
	NUTS_TRUE(cmd.start_key == 100);
	NUTS_TRUE(cmd.end_key == 200);
	NUTS_TRUE(cmd.interval_ms == 10);
	NUTS_TRUE(strcmp(cmd.pub_topic, "canudp/replay") == 0);
	nng_exchange_replay_cmd_free(&cmd);

	rv = nng_exchange_replay_cmd_parse(
	    "replay-1788505150994-1788505162985-0-a/b-c", &cmd);
	NUTS_TRUE(rv == 0);
	NUTS_TRUE(cmd.start_key == 1788505150994ULL);
	NUTS_TRUE(cmd.end_key == 1788505162985ULL);
	NUTS_TRUE(cmd.interval_ms == 0);
	NUTS_TRUE(strcmp(cmd.pub_topic, "a/b-c") == 0);
	nng_exchange_replay_cmd_free(&cmd);

	NUTS_TRUE(nng_exchange_replay_cmd_parse("sync-1-2", &cmd) != 0);
	NUTS_TRUE(nng_exchange_replay_cmd_parse("replay-1-2-10-", &cmd) != 0);
	NUTS_TRUE(nng_exchange_replay_cmd_parse("replay-1-2-10", &cmd) != 0);
	NUTS_TRUE(nng_exchange_replay_cmd_parse("replay-5-1-10-t", &cmd) != 0);
	NUTS_TRUE(nng_exchange_replay_cmd_parse(NULL, &cmd) != 0);
}

NUTS_TESTS = {
	{ "Exchange client test", test_exchange_client },
	{ "Exchange drain before bind", test_drain_before_bind },
	{ "Exchange replay cmd parse", test_exchange_replay_cmd_parse },
	{ NULL, NULL },
};
