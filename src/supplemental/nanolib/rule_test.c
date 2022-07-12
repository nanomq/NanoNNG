#include "nng/supplemental/nanolib/conf.h"
#include "nng/supplemental/nanolib/cvector.h"
#include "nng/nng.h"
#include "nng/nng_debug.h"
#include "core/nng_impl.h"
#include "string.h"
#include <nuts.h>


// int rule_find_key(const char *str, size_t len);
// char *rule_get_key_arr(char *p, rule_key *key);
// bool rule_sql_parse(conf_rule *cr, char *sql);


void test_rule_find_key(void)
{
	char *test_key_arr[] = {
		"qos",
		"id",
		"topic",
		"clientid",
		"username",
		"password",
		"timestamp",
		"payload",
		"*",
		NULL,
	};

	// Find all key
	int i = 0;
	while (test_key_arr[i]) {
		int j = rule_find_key(test_key_arr[i], strlen(test_key_arr[i]));
		NUTS_TRUE(j >= 0 && j <= 8);
		NUTS_TRUE(j == i);
		i++;
	}

	char *invalid_arr[] = {
		"invalid",
		"preqos",
		"qosaft",
		NULL
	};

	/// Test invalid 
	i = 0;
	while (invalid_arr[i]) {
		NUTS_TRUE(-1 == rule_find_key(invalid_arr[i], strlen(invalid_arr[i])));
		i++;
	}

}

void test_rule_get_key_arr(void)
{

	rule_key rk = { 0 };

	char test_arr[][16] = {
		".a",
		".a.b",
		".a.b.c",
	};

	char *rst_arr[4][3] = {
		{"a", NULL, NULL},
		{"a", "b", NULL},
		{"a", "b", "c"},
		NULL
	};

	int i = 0;
	for (;i < 3; i++) {
		rk.key_arr = NULL;
		rule_get_key_arr(test_arr[i], &rk);
		int j = 0;
		while(rst_arr[i][j]) {
			NUTS_NULL(!rk.key_arr[j]);
			NUTS_PASS(strcmp(rk.key_arr[j], rst_arr[i][j]));
			nng_free(rk.key_arr[j], strlen(rk.key_arr[j]));
			j++;
		}
		cvector_free(rk.key_arr);
	}

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
