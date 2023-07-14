#include "nng/protocol/mqtt/mqtt_parser.h"
#include <assert.h>
#include <nuts.h>
#include <stdio.h>

static void test_pub_extra()
{
	pub_extra *extra = NULL;
	NUTS_TRUE(extra = pub_extra_alloc(extra) != NULL);
	pub_extra_set_qos(extra, 0);
	uint8_t qos = pub_extra_get_qos(extra);
	NUTS_TRUE(qos == 0);
	pub_extra_set_packet_id(extra, 12);
	uint16_t pkt_id = pub_extra_get_packet_id(extra);
	NUTS_TRUE(pkt_id == 12);
	char *msg = "message";
	pub_extra_set_msg(extra, msg);
	char *m = (char *) pub_extra_get_msg(extra);
	NUTS_TRUE(strcmp(msg, m) == 0);
	pub_extra_free(extra);
}

static void
test_utf8_check()
{
	uint8_t src[] = { 0x24, 0x4D, 0x51, 0x54, 0x54, '\0' };
	uint8_t src_0[] = { 0x00, 0x4D, 0x51, 0x54, 0x54, '\0' };
	uint8_t src_C0[] = { 0xC0, 0x4D, 0x51, 0x54, 0x54, '\0' };
	uint8_t src_F6[] = { 0xF6, 0x4D, 0x51, 0x54, 0x54, '\0' };
	uint8_t src_FF[] = { 0xFF, 0x4D, 0x51, 0x54, 0x54, '\0' };

	NUTS_PASS(utf8_check((char *) src, strlen((char *) src) - 1));
	// test oversize & NULL src.
	NUTS_FAIL(utf8_check((char *) src, 65537), ERR_INVAL);
	NUTS_FAIL(utf8_check(NULL, 0), ERR_INVAL);
	// test control characters.
	src[0] = 0x04;
	NUTS_FAIL(utf8_check((char *) src, strlen((char *) src) - 1),
	    ERR_MALFORMED_UTF8);
	// test malformed utf8
	NUTS_FAIL(utf8_check((char *) src_0, strlen((char *) src_0) - 1),
	    ERR_MALFORMED_UTF8);
	NUTS_FAIL(utf8_check((char *) src_C0, strlen((char *) src_C0) - 1),
	    ERR_MALFORMED_UTF8);
	NUTS_FAIL(utf8_check((char *) src_F6, strlen((char *) src_F6) - 1),
	    ERR_MALFORMED_UTF8);
	NUTS_FAIL(utf8_check((char *) src_FF, strlen((char *) src_FF) - 1),
	    ERR_MALFORMED_UTF8);
}

static void
test_get_utf8_str()
{
	uint8_t  src[] = { 0x00, 0x05, 0x24, 0x4D, 0x51, 0x54, 0x54, '\0' };
	uint32_t pos   = 0;
	char    *dest;
	// test for correct src.
	NUTS_ASSERT(get_utf8_str(&dest, src, &pos) == 5);
	NUTS_MATCH((char *) dest, "$MQTT");
	src[2] = 0x04;
	pos    = 0;
	// test for non-utf8 src.
	NUTS_FAIL(get_utf8_str(&dest, src, &pos), -1);
}

static void
test_copyn_utf8_str()
{
	uint8_t  src[]   = { 0x00, 0x05, 0x24, 0x4D, 0x51, 0x54, 0x54, '\0' };
	uint32_t pos     = 0;
	int      str_len = 0;
	int      limit   = 20;
	uint8_t *ptr_rv  = NULL;
	// test src.
	ptr_rv = copyn_utf8_str(src, &pos, &str_len, limit);
	NUTS_MATCH((char *) ptr_rv, "$MQTT");
	nng_free(ptr_rv, sizeof(ptr_rv));
	// test for buffer overflow.
	limit   = 1;
	pos     = 0;
	str_len = 0;
	NUTS_ASSERT(copyn_utf8_str(src, &pos, &str_len, limit) == NULL);
	limit   = 2;
	pos     = 0;
	str_len = 0;
	NUTS_ASSERT(copyn_utf8_str(src, &pos, &str_len, limit) == NULL);
}

static void
test_copyn_str()
{
	uint8_t  src[]   = { 0x00, 0x05, 0x24, 0x4D, 0x51, 0x54, 0x54, '\0' };
	uint32_t pos     = 0;
	int      str_len = 0;
	int      limit   = 20;
	uint8_t *ptr_rv  = NULL;

	ptr_rv = copyn_str(src, &pos, &str_len, limit);
	NUTS_MATCH((char *) ptr_rv, "$MQTT");
	nng_free(ptr_rv, sizeof(ptr_rv));

	ptr_rv = copyn_str(NULL, &pos, &str_len, limit);
	NUTS_NULL(ptr_rv);

	limit  = 1;
	ptr_rv = copyn_str(NULL, &pos, &str_len, limit);
	NUTS_NULL(ptr_rv);
}

static void
test_get_variable_binary()
{
	char   *dest;
	uint8_t src[] = { 0x00, 0x05, 0x24, 0x4D, 0x51, 0x54, 0x54, '\0' };

	NUTS_ASSERT(get_variable_binary((uint8_t **) &dest, src) == 5);
	NUTS_MATCH(dest, "$MQTT");
}

static void
test_fixed_header_adaptor()
{
	uint8_t  packet[] = { 0x00, 0x05, 0x12, '\0' };
	nng_msg *dst;
	nng_msg_alloc(&dst, 10);

	NUTS_PASS(fixed_header_adaptor(packet, dst));

	nng_msg_free(dst);
}

static void
test_ws_msg_adaptor()
{
	uint8_t  packet[] = { 0x00, 0x05, 0x12, 0x22, 0x23, 0x24, '\0' };
	nng_msg *dst;
	nng_msg_alloc(&dst, 10);

	NUTS_PASS(ws_msg_adaptor(packet, dst));

	nng_msg_free(dst);
}

static void
test_hash()
{
	char *str  = "test";
	char *str2 = "testTEST";
	NUTS_ASSERT(DJBHash(str) == 2090756197);
	NUTS_ASSERT(DJBHashn(str, strlen(str)) == 2090756197);
	NUTS_ASSERT(fnv1a_hashn(str, strlen(str)) == (uint32_t)-1345293851); 
	NUTS_ASSERT(crc_hashn(str, strlen(str)) == 191);
	NUTS_ASSERT(nano_hash(str) == 6385723493);

	NUTS_ASSERT(crc32_hashn(str2, strlen(str2)) == (uint32_t)-620996987);
	NUTS_ASSERT(crc32c_hashn(str2, strlen(str2)) == 788723578);
}

static void
test_topic_filter()
{
	char *orgin = "test/topic";
	char *input = "test/topic";
	NUTS_ASSERT(topic_filter(orgin, input) == true);

	char *orgin2 = "test/#";
	NUTS_ASSERT(topic_filter(orgin2, input) == true);
}

static void
test_topic_filtern()
{
	char *orgin = "test/topic";
	char *input = "test/topic/test";
	NUTS_ASSERT(topic_filtern(orgin, input, 10) == true);

	NUTS_ASSERT(topic_filtern(orgin, input, 11) == false);
}

NUTS_TESTS = {
	{ "mqtt_parser pub_extras", test_pub_extra },
	{ "mqtt_parser utf8_check", test_utf8_check },
	{ "mqtt_parser get_utf8_str", test_get_utf8_str },
	{ "mqtt_parser copyn_utf8_str", test_copyn_utf8_str },
	{ "mqtt_parser copyn_str", test_copyn_str },
	{ "mqtt_parser get_variable_binary", test_get_variable_binary },
	{ "mqtt_parser fixed_header_adaptor", test_fixed_header_adaptor },
	{ "mqtt_parser ws_msg_adaptor", test_ws_msg_adaptor },
	// TODO more tests needed.
	{ "mqtt_parser hash", test_hash },
	// TODO more tests needed.
	{ "mqtt_parser topic_filter", test_topic_filter },
	{ "mqtt_parser topic_filtern", test_topic_filtern },

	{ NULL, NULL },
};