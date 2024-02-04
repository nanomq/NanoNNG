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

static inline void client_get_msgs(nng_socket sock, uint64_t startKey, uint64_t endKey, uint32_t *lenp, nng_msg ***msgList)
{
	nni_aio *aio = NULL;
	NUTS_PASS(nng_aio_alloc(&aio, NULL, NULL));

	nng_msg *msg;
	nng_time *tss = NULL;
	nng_msg_alloc(&msg, 0);

	if (endKey == 0) {
		nng_msg_set_timestamp(msg, startKey);
		nng_msg_set_proto_data(msg, NULL, NULL);
	} else {
		tss = nng_alloc(sizeof(nng_time) * 3);
		tss[0] = startKey;
		tss[1] = endKey;
		tss[2] = (nng_time *)NULL;
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
void
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
	nng_mqtt_msg_set_publish_payload(
	    pubmsg, (uint8_t *) payload, payload_len);
	nng_mqtt_msg_set_publish_topic(pubmsg, topic);
	nng_mqtt_msg_set_publish_topic_len(pubmsg, strlen(topic));

	nni_aio *aio = NULL;
	NUTS_PASS(nng_aio_alloc(&aio, NULL, NULL));


	nng_msg_set_timestamp(pubmsg, key);
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

	nng_aio_free(aio);
}

void
test_exchange_client(void)
{
	int rv = 0;
	uint64_t key = 0;
	nng_socket sock;

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
	client_publish(sock, "topic1", key, NULL, 0, 0, 0);

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
	client_get_msgs(sock, key, 0, lenp, &msgList);
	NUTS_TRUE(*lenp == 1 && msgList != NULL);
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
	client_get_msgs(sock, 1, 10, lenp, &msgList);
	NUTS_TRUE(*lenp == 9 && msgList != NULL);
	free_msg_list(msgList, NULL, lenp, 0);

	/* Ringbuffer is full and msgs returned need to free */
	client_publish(sock, "topic1", 10, NULL, 0, 0, 0);

	/* get and clean up msgs */
	lenp = nng_alloc(sizeof(uint32_t));
	*lenp = 0;
	client_get_and_clean_msgs(sock, lenp, &msgList);
	NUTS_TRUE(*lenp == 10 && msgList != NULL);
	free_msg_list(msgList, NULL, lenp, 1);

	cvector_free(conf->rbufs);
	nng_free(conf, sizeof(conf_exchange_node));
	nng_free(rb_node, sizeof(ringBuffer_node));

	return;
}

NUTS_TESTS = {
	{ "Exchange client test", test_exchange_client },
	{ NULL, NULL },
};
