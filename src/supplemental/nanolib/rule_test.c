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
		{NULL, NULL, NULL}
	};

	int i = 0;
	for (;i < 3; i++) {
		rk.key_arr = NULL;
		rule_get_key_arr(test_arr[i], &rk);
		int j = 0;
		while(rst_arr[i][j]) {
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
	// test 1
	char sql1[128] = "SELECT * FROM \"#\" WHERE username = \'abc\'";
	conf_rule cr = { 0 };

	NUTS_TRUE(rule_sql_parse(&cr, sql1));
	for (int j = 0; j < 9; j++) {
		if (RULE_PAYLOAD_FIELD == j) {
			NUTS_TRUE(!cr.rules[0].flag[j]);
		} else {
			NUTS_TRUE(cr.rules[0].flag[j]);
			if (RULE_USERNAME == j) {
				NUTS_TRUE(RULE_CMP_EQUAL == cr.rules[0].cmp_type[j]);
			} else  {
				NUTS_TRUE(RULE_CMP_NONE == cr.rules[0].cmp_type[j]);
			}
		}

	}

	NUTS_PASS(strcmp("#", cr.rules[0].topic));
	nng_free(cr.rules[0].topic, strlen(cr.rules[0].topic));

	NUTS_PASS(strcmp("abc", cr.rules[0].filter[RULE_USERNAME]));
	nng_free(cr.rules[0].filter[RULE_USERNAME], strlen(cr.rules[0].filter[RULE_USERNAME]));
	nng_free(cr.rules[0].filter, sizeof(char *) * 8);
	cvector_free(cr.rules);

	// test 2
	memset(&cr, 0, sizeof(conf_rule));
	char sql2[128] = "SELECT qos, username, clientid FROM \"t/#\"";
	NUTS_TRUE(rule_sql_parse(&cr, sql2));
	for (int j = 0; j < 9; j++) {
		if (RULE_QOS == j || RULE_USERNAME == j || RULE_CLIENTID == j) {
			NUTS_TRUE(cr.rules[0].flag[j]);
		} else {
			NUTS_TRUE(!cr.rules[0].flag[j]);
		}

	}

	NUTS_PASS(strcmp("t/#", cr.rules[0].topic));
	nng_free(cr.rules[0].topic, strlen(cr.rules[0].topic));
	cvector_free(cr.rules);

	// test3
	memset(&cr, 0, sizeof(conf_rule));
	char sql3[128] = "SELECT a, b FROM \"t/#\"";
	NUTS_TRUE(!rule_sql_parse(&cr, sql3));

	// test4
	memset(&cr, 0, sizeof(conf_rule));
	char sql4[128] = "SELECT clientid as cid FROM \"#\" WHERE cid = \'abc\'";
	NUTS_TRUE(rule_sql_parse(&cr, sql4));
	for (int j = 0; j < 9; j++) {
		if ( RULE_CLIENTID == j) {
			NUTS_TRUE(cr.rules[0].flag[j]);
			NUTS_PASS(strcmp(cr.rules[0].as[j], "cid"));
			NUTS_PASS(strcmp("abc", cr.rules[0].filter[j]));
			nng_free(cr.rules[0].as[j], strlen(cr.rules[0].as[j]));
			nng_free(cr.rules[0].filter[j], strlen(cr.rules[0].filter[j]));
			nng_free(cr.rules[0].filter, sizeof(char *) * 8);
		} else {
			NUTS_TRUE(!cr.rules[0].flag[j]);
		}

	}

	NUTS_PASS(strcmp("#", cr.rules[0].topic));
	nng_free(cr.rules[0].topic, strlen(cr.rules[0].topic));
	cvector_free(cr.rules);

	// test5
	memset(&cr, 0, sizeof(conf_rule));
	char sql5[128] = "SELECT clientid as cid FROM \"#\" WHERE xyz = \'abc\'";
	NUTS_TRUE(!rule_sql_parse(&cr, sql5));

	// test6
	memset(&cr, 0, sizeof(conf_rule));
	char sql6[128] = "SELECT payload FROM \"#\" WHERE payload.x.y = 1";
	NUTS_TRUE(rule_sql_parse(&cr, sql6));
	for (int j = 0; j < 9; j++) {
		if (RULE_PAYLOAD_ALL == j) {
			NUTS_TRUE(cr.rules[0].flag[j]);
		} else {
			NUTS_TRUE(!cr.rules[0].flag[j]);
		}
	}

	if (cr.rules[0].payload[0]) {
		NUTS_PASS(strcmp(cr.rules[0].payload[0]->filter, "1"));
		NUTS_PASS(strcmp(cr.rules[0].payload[0]->psa[0], "x"));
		NUTS_PASS(strcmp(cr.rules[0].payload[0]->psa[1], "y"));
		NUTS_PASS(strcmp(cr.rules[0].payload[0]->pas, "payload.x.y"));
		nng_free(cr.rules[0].payload[0]->filter, strlen(cr.rules[0].payload[0]->filter));
		nng_free(cr.rules[0].payload[0]->psa[0], strlen(cr.rules[0].payload[0]->psa[0]));
		nng_free(cr.rules[0].payload[0]->psa[1], strlen(cr.rules[0].payload[0]->psa[1]));
		nng_free(cr.rules[0].payload[0]->pas, strlen(cr.rules[0].payload[0]->pas));
		cvector_free(cr.rules[0].payload[0]->psa);
		NUTS_TRUE(RULE_CMP_EQUAL == cr.rules[0].payload[0]->cmp_type);
		NUTS_TRUE(false == cr.rules[0].payload[0]->is_store);
		nng_free(cr.rules[0].payload[0], sizeof(rule_payload));
		cvector_free(cr.rules[0].payload);
	}

	nng_free(cr.rules[0].filter, sizeof(char*) * 8);

	NUTS_PASS(strcmp("#", cr.rules[0].topic));
	nng_free(cr.rules[0].topic, strlen(cr.rules[0].topic));
	cvector_free(cr.rules);

	// test7
	memset(&cr, 0, sizeof(conf_rule));
	char sql7[128] = "SELECT payload.x.y as y FROM \"#\" WHERE y = 1";
	NUTS_TRUE(rule_sql_parse(&cr, sql7));
	for (int j = 0; j < 9; j++) {
		if (RULE_PAYLOAD_FIELD == j) {
			NUTS_TRUE(cr.rules[0].flag[j]);
		} else {
			NUTS_TRUE(!cr.rules[0].flag[j]);
		}
	}

	if (cr.rules[0].payload[0]) {
		NUTS_PASS(strcmp(cr.rules[0].payload[0]->filter, "1"));
		NUTS_PASS(strcmp(cr.rules[0].payload[0]->psa[0], "x"));
		NUTS_PASS(strcmp(cr.rules[0].payload[0]->psa[1], "y"));
		NUTS_PASS(strcmp(cr.rules[0].payload[0]->pas, "y"));
		nng_free(cr.rules[0].payload[0]->filter, strlen(cr.rules[0].payload[0]->filter));
		nng_free(cr.rules[0].payload[0]->psa[0], strlen(cr.rules[0].payload[0]->psa[0]));
		nng_free(cr.rules[0].payload[0]->psa[1], strlen(cr.rules[0].payload[0]->psa[1]));
		nng_free(cr.rules[0].payload[0]->pas, strlen(cr.rules[0].payload[0]->pas));
		cvector_free(cr.rules[0].payload[0]->psa);
		NUTS_TRUE(RULE_CMP_EQUAL == cr.rules[0].payload[0]->cmp_type);
		NUTS_TRUE(true == cr.rules[0].payload[0]->is_store);
		nng_free(cr.rules[0].payload[0], sizeof(rule_payload));
		cvector_free(cr.rules[0].payload);
	}

	nng_free(cr.rules[0].filter, sizeof(char*) * 8);

	NUTS_PASS(strcmp("#", cr.rules[0].topic));
	nng_free(cr.rules[0].topic, strlen(cr.rules[0].topic));
	cvector_free(cr.rules);
	NUTS_PASS(0);

}

NUTS_TESTS = {
	{ "rule engine find key", test_rule_find_key },
	{ "rule engine get key array", test_rule_get_key_arr },
	{ "rule engine sql parse", test_rule_sql_parse },
	{ NULL, NULL },
};
