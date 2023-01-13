#include "nng/protocol/mqtt/mqtt_parser.h"
#include <assert.h>
#include <stdio.h>

static int
test_utf8_check()
{
	int     rv;
	uint8_t src[] = { 0x24, 0x4D, 0x51, 0x54, 0x54 };
	// TODO more cases for failure.
	rv = utf8_check(src, strlen(src) - 1);
	assert(rv == ERR_SUCCESS);

	return ERR_SUCCESS;
}

static int
test_get_utf8_str()
{
	int      rv    = 0;
	uint8_t  src[] = { 0x00, 0x05, 0x04, 0x4D, 0x51, 0x54, 0x54 };
	uint32_t pos   = 0;
	char    *dest;
	// test for non-utf8 src.
	rv = get_utf8_str(&dest, src, &pos);
	assert(rv == -1);
	// test for correct src.
	src[2] = 0x24;
	pos    = 0;
	rv     = get_utf8_str(&dest, src, &pos);
	assert(rv == 5);
	assert(strcmp(dest, "$MQTT") == 0);

	return ERR_SUCCESS;
}

static int
test_copyn_utf8_str()
{
	uint8_t  src[]   = { 0x00, 0x05, 0x24, 0x4D, 0x51, 0x54, 0x54 };
	int      pos     = 0;
	int      str_len = 0;
	int      limit   = 20;
	uint8_t *ptr_rv  = NULL;

	ptr_rv = copyn_utf8_str(src, &pos, &str_len, limit);
	assert(strcmp(ptr_rv, "$MQTT") == 0);

	limit  = 1;
	ptr_rv = copyn_utf8_str(src, &pos, &str_len, limit);
	assert(ptr_rv == NULL);

	return ERR_SUCCESS;
}

static int
test_copy_utf8_str()
{
	uint8_t  src[]   = { 0x00, 0x05, 0x24, 0x4D, 0x51, 0x54, 0x54 };
	int      pos     = 0;
	int      str_len = 0;
	uint8_t *ptr_rv  = NULL;

	ptr_rv           = copy_utf8_str(src, &pos, &str_len);
	assert(strcmp(ptr_rv, "$MQTT") == 0);

	return ERR_SUCCESS;
}

static int
test_copyn_str()
{
	uint8_t  src[]   = { 0x00, 0x05, 0x23, 0x4D, 0x51, 0x54, 0x54 };
	int      pos     = 0;
	int      str_len = 0;
	int      limit   = 20;
	uint8_t *ptr_rv  = NULL;

	ptr_rv           = copyn_str(src, &pos, &str_len, limit);
	assert(strcmp(ptr_rv, "#MQTT") == 0);

	ptr_rv = copyn_str(NULL, &pos, &str_len, limit);
	assert(ptr_rv == NULL);

	limit  = 1;
	ptr_rv = copyn_str(NULL, &pos, &str_len, limit);
	assert(ptr_rv == NULL);

	return ERR_SUCCESS;
}

static int
test_get_variable_binary()
{
	int     rv = 0;
	char   *dest;
	uint8_t src[] = { 0x00, 0x05, 0x24, 0x4D, 0x51, 0x54, 0x54 };

	rv            = get_variable_binary(&dest, src);
	assert(rv == 5);
	assert(strcmp(dest, "$MQTT") == 0);

	return ERR_SUCCESS;
}

static int
test_fixed_header_adaptor()
{
	int      rv       = 0;
	uint8_t  packet[] = { 0x00, 0x05, 0x12 };
	nng_msg *dst;
	nng_msg_alloc(&dst, 10);

	rv = fixed_header_adaptor(packet, dst);
	assert(rv == 0);

	nng_msg_free(dst);

	return ERR_SUCCESS;
}

static int
test_ws_msg_adaptor()
{
	int      rv       = 0;
	uint8_t  packet[] = { 0x00, 0x05, 0x12 };
	nng_msg *dst;
	nng_msg_alloc(&dst, 10);

	rv = ws_msg_adaptor(packet, dst);
	assert(rv == 0);

	nng_msg_free(dst);

	return ERR_SUCCESS;
}

static int
test_DJBHash()
{
	int   rv  = 0;
	char *str = "test";

	rv        = DJBHash(str);
	assert(rv == 2090756197);

	return ERR_SUCCESS;
}

static int
test_DJBHashn()
{
	int      rv  = 0;
	char    *str = "test";
	uint16_t len = 2;

	rv           = DJBHashn(str, len);
	assert(rv == 5863838);

	return ERR_SUCCESS;
}

static int
test_check_ifwildcard()
{
	bool  rv    = false;

	char *orgin = "test/#";
	char *input = "test/topic";
	rv          = check_ifwildcard(orgin, input);
	assert(rv == true);

	char *orgin2 = "test/topic2";
	rv           = check_ifwildcard(orgin2, input);
	assert(rv == false);

	char *orgin3 = "test/topic/#";
	rv           = check_ifwildcard(orgin3, input);
	assert(rv == true);

	char *orgin4 = "test/topic/+";
	rv           = check_ifwildcard(orgin4, input);
	assert(rv == false);

	char *input2 = "test/topic/topic2";
	rv           = check_ifwildcard(orgin2, input2);
	assert(rv == false);

	return ERR_SUCCESS;
}

static int
test_topic_filter()
{
	bool  rv    = false;

	char *orgin = "test/topic";
	char *input = "test/topic";
	rv          = topic_filter(orgin, input);
	assert(rv == true);

	char *orgin2 = "test/#";
	rv           = topic_filter(orgin2, input);
	assert(rv == true);

	return ERR_SUCCESS;
}

static int
test_topic_filtern()
{
	bool  rv    = false;

	char *orgin = "test/topic";
	char *input = "test/topic/test";
	rv          = topic_filtern(orgin, input, 10);
	assert(rv == true);
    
	rv = topic_filtern(orgin, input, 11);
	assert(rv == false);

	return ERR_SUCCESS;
}

int
main()
{
	test_utf8_check();

	test_get_utf8_str();

	test_copyn_utf8_str();

	test_copy_utf8_str();

	test_copyn_str();

	test_get_variable_binary();

	test_fixed_header_adaptor();

	test_ws_msg_adaptor();

	/*
	 * TODO more tests needed.
	 */

	test_DJBHash();

	test_DJBHashn();

	/*
	 * TODO more tests needed.
	 */

	test_check_ifwildcard();

	test_topic_filter();

	test_topic_filtern();
}