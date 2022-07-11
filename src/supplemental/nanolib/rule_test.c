#include "nng/supplemental/nanolib/conf.h"
#include "nng/supplemental/nanolib/cvector.h"
#include "nng/nng.h"
#include "nng/nng_debug.h"
#include "core/nng_impl.h"
#include <nuts.h>


void test_rule_find_key(void)
{

	NUTS_PASS(0);

}

void test_rule_get_key_arr(void)
{
	NUTS_PASS(0);

}

void test_rule_sql_parse(void)
{
	NUTS_PASS(0);

}

NUTS_TESTS = {
	{ "rule engine find key", test_rule_find_key },
	{ "rule engine get key array", test_rule_get_key_arr },
	{ "rule engine sql parse", test_rule_sql_parse },
	{ NULL, NULL },
};
