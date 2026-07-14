#include <nuts.h>

#include "nng/protocol/mqtt/nmq_mqtt.h"
#include "nng/supplemental/nanolib/conf.h"

static void
test_dropped_messages_option(void)
{
	conf      *config = nng_zalloc(sizeof(*config));
	nng_socket socket = { 0 };
	uint64_t   dropped = UINT64_MAX;
	uint64_t   sent = UINT64_MAX;
	bool       log_drops;

	NUTS_TRUE(config != NULL);
	conf_init(config);
	socket.data = config;
	NUTS_PASS(nng_nmq_tcp0_open(&socket));
	NUTS_PASS(nng_socket_get_uint64(
	    socket, NMQ_OPT_MQTT_MSGS_DROPPED, &dropped));
	NUTS_ASSERT(dropped == 0);
	NUTS_PASS(nng_socket_get_uint64(
	    socket, NMQ_OPT_MQTT_MSGS_SENT, &sent));
	NUTS_ASSERT(sent == 0);
	NUTS_FAIL(nng_socket_set_uint64(
	              socket, NMQ_OPT_MQTT_MSGS_DROPPED, 1),
	    NNG_EREADONLY);
	NUTS_PASS(nng_socket_get_bool(
	    socket, NMQ_OPT_MQTT_LOG_DROPS, &log_drops));
	NUTS_TRUE(log_drops);
	NUTS_PASS(nng_socket_set_bool(socket, NMQ_OPT_MQTT_LOG_DROPS, false));
	NUTS_PASS(nng_socket_get_bool(
	    socket, NMQ_OPT_MQTT_LOG_DROPS, &log_drops));
	NUTS_ASSERT(!log_drops);
	NUTS_PASS(nng_close(socket));
}

NUTS_TESTS = {
	{ "nmq mqtt dropped messages option", test_dropped_messages_option },
	{ NULL, NULL },
};
