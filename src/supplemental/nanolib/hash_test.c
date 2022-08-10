#include "nng/supplemental/nanolib/hash_table.h"
#include "test.h"
#include <nuts.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define TABLE_SZ 8

typedef struct {
	uint32_t pipe;
	uint32_t key;
	char    *topic;
} test_table;

test_table table1[] = { { .pipe = 1, .key = 1, .topic = "test1" },
	{ .pipe = 1, .key = 10, .topic = "test10" },
	{ .pipe = 1, .key = 100, .topic = "test100" },
	{ .pipe = 1, .key = 1000, .topic = "test1000" },
	{ .pipe = 1, .key = 10000, .topic = "test10000" },
	{ .pipe = 1, .key = 100000, .topic = "test100000" },
	{ .pipe = 1, .key = 1000000, .topic = "test1000000" },
	{ .pipe = 1, .key = 10000000, .topic = "test10000000" } };

test_table table2[] = { { .pipe = 2, .key = 1, .topic = "test10000000" },
	{ .pipe = 2, .key = 10, .topic = "test1000000" },
	{ .pipe = 2, .key = 100, .topic = "test100000" },
	{ .pipe = 2, .key = 1000, .topic = "test10000" },
	{ .pipe = 2, .key = 10000, .topic = "test1000" },
	{ .pipe = 2, .key = 100000, .topic = "test100" },
	{ .pipe = 2, .key = 1000000, .topic = "test10" },
	{ .pipe = 2, .key = 10000000, .topic = "test1" }

};

static void
assert_str(const char *s1, const char *s2)
{
	if (strcmp(s1, s2)) {
		debug_msg("Test failed lv: %s, rv: %s", s1, s2);
		exit(0);
	}
	// } else {
	// 	debug_msg("FOR OBSERVE: %s, %s", s1, s2);
	// }

	return;
}

static void
test_check_alias_table(void)
{
	const char *r = NULL;

	// Make sure delete on an empty table is OK!
	for (int i = 0; i < TABLE_SZ; i++) {
		dbhash_del_atpair_queue(table1[i].pipe);
		r = dbhash_find_atpair(table1[i].pipe, table1[i].key);
		NUTS_TRUE(!r);
	}
	// Check if insert many is OK!
	for (int i = 0; i < TABLE_SZ; i++) {
		dbhash_insert_atpair(
		    table1[i].pipe, table1[i].key, table1[i].topic);
		r = dbhash_find_atpair(table1[i].pipe, table1[i].key);
		NUTS_TRUE(NULL != r);
		assert_str(r, table1[i].topic);
	}

	for (int i = 0; i < TABLE_SZ; i++) {
		r = dbhash_find_atpair(table1[i].pipe, table1[i].key);
		NUTS_TRUE(NULL != r);
		assert_str(r, table1[i].topic);
	}

	for (int i = 0; i < TABLE_SZ; i++) {
		dbhash_del_atpair_queue(table1[i].pipe);
		r = dbhash_find_atpair(table1[i].pipe, table1[i].key);
		NUTS_TRUE(NULL == r);
	}

	return;
}

static void
test_alias_table(void)
{
	for (size_t i = 0; i < TABLE_SZ; i++) {
		dbhash_insert_atpair(
		    table1[i].pipe, table1[i].key, table1[i].topic);
		dbhash_find_atpair(table1[i].pipe, table1[i].key);
		dbhash_del_atpair_queue(table1[i].pipe);

		dbhash_insert_atpair(
		    table2[i].pipe, table2[i].key, table2[i].topic);
		dbhash_find_atpair(table2[i].pipe, table2[i].key);
		dbhash_del_atpair_queue(table2[i].pipe);
	}

	return;
}

static void
test_check_pipe_table()
{

	for (int i = 0; i < TABLE_SZ; i++) {
		dbhash_insert_topic(table1[i].key, table1[i].topic);
		dbhash_insert_topic(table2[i].key, table2[i].topic);
	}

	dbhash_ptpair_t **pt = dbhash_get_ptpair_all();

	size_t s = cvector_size(pt);
	for (size_t i = 0; i < s; i++) {
		for (size_t j = 0; j < TABLE_SZ; j++) {
			if (pt[i]->pipe == table1[j].key) {
				NUTS_PASS(
				    strcmp(pt[i]->topic, table1[j].topic));
			}
		}

		dbhash_ptpair_free(pt[i]);
	}
	cvector_free(pt);

	for (size_t i = 0; i < TABLE_SZ; i++) {
		NUTS_TRUE(dbhash_check_topic(table1[i].key, table1[i].topic));
		NUTS_TRUE(dbhash_check_topic(table2[i].key, table2[i].topic));
		NUTS_TRUE(dbhash_check_id(table1[i].key));
	}

	topic_queue *tq = NULL;
	for (size_t i = 0; i < TABLE_SZ; i++) {
		tq = dbhash_get_topic_queue(table1[i].key);
		NUTS_TRUE(NULL != tq);
		assert_str(tq->topic, table1[i].topic);
		assert_str(tq->next->topic, table2[i].topic);
	}

	for (size_t i = 0; i < TABLE_SZ / 2; i++) {
		dbhash_del_topic(table1[i].key, table1[i].topic);
		dbhash_del_topic(table1[i].key, table2[i].topic);
		NUTS_TRUE(false ==
		    dbhash_check_topic(table1[i].key, table1[i].topic));
		NUTS_TRUE(false ==
		    dbhash_check_topic(table1[i].key, table2[i].topic));
	}

	for (size_t i = TABLE_SZ / 2; i < TABLE_SZ; i++) {
		dbhash_del_topic_queue(table1[i].key, NULL, NULL);
		NUTS_TRUE(false == dbhash_check_id(table1[i].key));
		NUTS_TRUE(false ==
		    dbhash_check_topic(table1[i].key, table1[i].topic));
		NUTS_TRUE(false ==
		    dbhash_check_topic(table2[i].key, table2[i].topic));
	}

	return;
}

static void
test_pipe_table()
{

	for (size_t i = 0; i < TABLE_SZ; i++) {
		dbhash_insert_topic(table1[i].key, table1[i].topic);
		dbhash_insert_topic(table2[i].key, table2[i].topic);

		dbhash_check_topic(table1[i].key, table1[i].topic);
		dbhash_check_topic(table2[i].key, table2[i].topic);

		dbhash_get_topic_queue(table1[i].key);

		dbhash_del_topic(table1[i].key, table1[i].topic);
		dbhash_del_topic(table2[i].key, table2[i].topic);
		dbhash_check_id(table1[i].key);

		dbhash_check_topic(table1[i].key, table1[i].topic);
		dbhash_check_topic(table2[i].key, table2[i].topic);

		dbhash_insert_topic(table1[i].key, table1[i].topic);
		dbhash_insert_topic(table2[i].key, table2[i].topic);

		char *first_topic = dbhash_get_first_topic(table1[i].key);
		nng_free(first_topic, strlen(first_topic));
		dbhash_del_topic_queue(table1[i].key, NULL, NULL);
	}

	return;
}

static void
test_check_cached_table()
{
	dbhash_insert_topic(table1[0].key, table1[0].topic);
	dbhash_insert_topic(table1[0].key, table2[0].topic);

	dbhash_insert_topic(table1[1].key, table1[1].topic);
	dbhash_insert_topic(table1[1].key, table2[1].topic);

	dbhash_cache_topic_all(table1[0].key, table1[0].key);
	dbhash_cache_topic_all(table1[1].key, table1[1].key);

	topic_queue *tq1 = dbhash_get_cached_topic(table1[0].key);
	topic_queue *tq2 = dbhash_get_cached_topic(table1[1].key);

	NUTS_TRUE(NULL != tq1);
	NUTS_TRUE(NULL != tq2);
	assert_str(tq1->topic, table1[0].topic);
	assert_str(tq1->next->topic, table2[0].topic);
	assert_str(tq2->topic, table1[1].topic);
	assert_str(tq2->next->topic, table2[1].topic);

	tq1 = dbhash_get_topic_queue(table1[0].key);
	tq2 = dbhash_get_topic_queue(table1[1].key);
	NUTS_TRUE(!tq1);
	NUTS_TRUE(!tq2);

	dbhash_restore_topic_all(table1[0].key, table1[0].key);
	dbhash_restore_topic_all(table1[1].key, table1[1].key);
	tq1 = dbhash_get_cached_topic(table1[0].key);
	tq2 = dbhash_get_cached_topic(table1[1].key);

	NUTS_TRUE(!tq1);
	NUTS_TRUE(!tq2);

	tq1 = dbhash_get_topic_queue(table1[0].key);
	tq2 = dbhash_get_topic_queue(table1[1].key);
	NUTS_TRUE(NULL != tq1);
	NUTS_TRUE(NULL != tq2);
	assert_str(tq1->topic, table1[0].topic);
	assert_str(tq1->next->topic, table2[0].topic);
	assert_str(tq2->topic, table1[1].topic);
	assert_str(tq2->next->topic, table2[1].topic);

	dbhash_cache_topic_all(table1[0].key, table1[0].key);
	dbhash_cache_topic_all(table1[1].key, table1[1].key);

	NUTS_TRUE(dbhash_cached_check_id(table1[0].key));
	NUTS_TRUE(dbhash_cached_check_id(table1[1].key));

	dbhash_del_cached_topic_all(table1[0].key);
	dbhash_del_cached_topic_all(table1[1].key);

	tq1 = dbhash_get_cached_topic(table1[0].key);
	tq2 = dbhash_get_cached_topic(table1[1].key);

	NUTS_TRUE(!tq1);
	NUTS_TRUE(!tq2);
	return;
}

static void
test_cached_table()
{

	for (size_t i = 0; i < TABLE_SZ; i++) {
		dbhash_insert_topic(table1[i].key, table1[i].topic);
		dbhash_insert_topic(table2[i].key, table2[i].topic);

		dbhash_check_topic(table1[i].key, table1[i].topic);
		dbhash_check_topic(table2[i].key, table2[i].topic);

		dbhash_cache_topic_all(table1[i].key, table1[i].key);

		topic_queue *tq = dbhash_get_cached_topic(table1[i].key);

		tq = dbhash_get_topic_queue(table1[i].key);
		
		tq = dbhash_get_cached_topic(table1[i].key);

		(void) tq;
	}

	return;
}

static void
test_check(void)
{
	test_check_alias_table();
	test_check_pipe_table();
	test_check_cached_table();
}

static void *
test_single_thread(void *args)
{
	(void) args;

	for (size_t i = 0; i < TEST_LOOP; i++) {
		test_alias_table();
		test_pipe_table();
		test_cached_table();
	}

	return NULL;
}

void
hash_test(void)
{

	debug_msg("TEST STARTED");
	dbhash_init_alias_table();
	dbhash_init_pipe_table();
	dbhash_init_cached_table();
	test_check();
	// test_concurrent(test_single_thread);
	test_single_thread(NULL);

	for (size_t i = 0; i < TABLE_SZ; i++) {
		const char *r =
		    dbhash_find_atpair(table1[i].pipe, table1[i].key);
		NUTS_TRUE(!r);
		r = dbhash_find_atpair(table2[i].pipe, table2[i].key);
		NUTS_TRUE(!r);
	}

	for (size_t j = 0; j < TABLE_SZ; j++) {
		topic_queue *tq = dbhash_get_topic_queue(table1[j].key);
		NUTS_TRUE(NULL == tq);
		tq = dbhash_get_cached_topic(table1[j].key);
		dbhash_del_cached_topic_all(table1[j].key);
		NUTS_TRUE(NULL != tq);
	}

	dbhash_destroy_alias_table();
	dbhash_destroy_pipe_table();
	dbhash_destroy_cached_table();

	debug_msg("TEST FINISHED");
	return;
}

NUTS_TESTS = {
	{ "dbhash_test", hash_test },
	{ NULL, NULL },
};