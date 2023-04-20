#include "nng/supplemental/nanolib/mqtt_db.h"
#include "nng/supplemental/nanolib/nanolib.h"
#include "test.h"
#include <assert.h>
#include <string.h>

#include "nng/nng.h"

#include "nuts.h"


dbtree *db     = NULL;
dbtree *db_ret = NULL;

///////////for wildcard////////////
char topic0[] = "zhang/bei/hai";
char topic1[] = "zhang/#";
char topic2[] = "#";
char topic3[] = "zhang/bei/#";
char topic4[] = "zhang/+/hai";
char topic5[] = "zhang/bei/+";
char topic6[] = "zhang/bei/h/ai";
char topic7[] = "+/+/+";
char topic8[] = "zhang/+/+";
char topic9[] = "zhang/bei/hai";

///////////for wildcard////////////
char share0[] = "$share/a/zhang/bei/hai";
char share1[] = "$share/a/zhang/bei/hai";
char share2[] = "$share/a/zhang/bei/hai";
char share3[] = "$share/a/zhang/bei/hai";
char share4[] = "$share/a/zhang/bei/hai";
char share5[] = "$share/a/zhang/bei/hai";
char share6[] = "$share/a/zhang/bei/hai";
char share7[] = "$share/a/+/+/+";
char share8[] = "$share/a/+/+/+";
char share9[] = "$share/a/zhang/bei/hai";

// char share0[] = "$share/a/zhang/bei/hai";
// char share1[] = "$share/a/zhang/#";
// char share2[] = "$share/a/#";
// char share3[] = "$share/a/zhang/bei/#";
// char share4[] = "$share/a/zhang/+/hai";
// char share5[] = "$share/b/zhang/bei/+";
// char share6[] = "$share/b/zhang/bei/h/ai";
// char share7[] = "$share/b/+/+/+";
// char share8[] = "$share/b/zhang/+/+";
// char share9[] = "$share/b/zhang/bei/hai";
//////////for binary_search/////////
char topic00[] = "zhang/bei/hai";
char topic01[] = "zhang/bei/aih";
char topic02[] = "zhang/bei/iah";
char topic03[] = "zhang/ee/aa";
char topic04[] = "zhang/ee/bb";
char topic05[] = "zhang/ee/cc";
char topic06[] = "aa/xx/yy";
char topic07[] = "bb/zz/aa";
char topic08[] = "cc/dd/aa";
char topic09[] = "www/xxx/zz";

////////////////////////////////////
char ctxt0[] = "350429";
char ctxt1[] = "350420";
char ctxt2[] = "350427";
char ctxt3[] = "350426";
char ctxt4[] = "350425";
char ctxt5[] = "350424";
char ctxt6[] = "350423";
char ctxt7[] = "350422";
char ctxt8[] = "350421";
char ctxt9[] = "350420";

uint32_t pipe_id0 = 150429;
uint32_t pipe_id1 = 150420;
uint32_t pipe_id2 = 150427;
uint32_t pipe_id3 = 150426;
uint32_t pipe_id4 = 150425;
uint32_t pipe_id5 = 150424;
uint32_t pipe_id6 = 150423;
uint32_t pipe_id7 = 150422;
uint32_t pipe_id8 = 150421;
uint32_t pipe_id9 = 150420;

nng_msg *retain0 = NULL;
nng_msg *retain1 = NULL;
nng_msg *retain2 = NULL;
nng_msg *retain3 = NULL;
nng_msg *retain4 = NULL;
nng_msg *retain5 = NULL;
nng_msg *retain6 = NULL;
nng_msg *retain7 = NULL;
nng_msg *retain8 = NULL;
nng_msg *retain9 = NULL;



uint32_t pipe_ids[] = {
	130429,
	130428,
	130427,
	130426,
	130425,
	130424,
	130423,
	130422,
	130421,
	130420,
};

static void
test_insert_client()
{
	dbtree_insert_client(db, topic0, pipe_id0);
	dbtree_print(db);
	dbtree_insert_client(db, topic1, pipe_id1);
	dbtree_print(db);
	dbtree_insert_client(db, topic2, pipe_id2);
	dbtree_print(db);
	dbtree_insert_client(db, topic3, pipe_id3);
	dbtree_print(db);
	dbtree_insert_client(db, topic4, pipe_id4);
	dbtree_print(db);
	dbtree_insert_client(db, topic5, pipe_id5);
	dbtree_print(db);
	dbtree_insert_client(db, topic6, pipe_id6);
	dbtree_print(db);
	dbtree_insert_client(db, topic7, pipe_id7);
	dbtree_print(db);
	dbtree_insert_client(db, topic8, pipe_id8);
	dbtree_print(db);
	dbtree_insert_client(db, topic9, pipe_id9);
	dbtree_print(db);
}

static void
test_insert_shared_client()
{
	dbtree_insert_client(db, share0, pipe_id0);
	dbtree_print(db);                
	dbtree_insert_client(db, share1, pipe_id1);
	dbtree_print(db);                
	dbtree_insert_client(db, share2, pipe_id2);
	dbtree_print(db);                
	dbtree_insert_client(db, share3, pipe_id3);
	dbtree_print(db);                
	dbtree_insert_client(db, share4, pipe_id4);
	dbtree_print(db);                
	dbtree_insert_client(db, share5, pipe_id5);
	dbtree_print(db);                
	dbtree_insert_client(db, share6, pipe_id6);
	dbtree_print(db);                
	dbtree_insert_client(db, share7, pipe_id7);
	dbtree_print(db);                
	dbtree_insert_client(db, share8, pipe_id8);
	dbtree_print(db);                
	dbtree_insert_client(db, share9, pipe_id9);
	dbtree_print(db);
}


static void
test_delete_shared_client()
{
	puts("================begin delete shared client===============");
	dbtree_delete_client(db, share0, pipe_id0);
	dbtree_print(db);              
	dbtree_delete_client(db, share1, pipe_id1);
	dbtree_print(db);              
	dbtree_delete_client(db, share2, pipe_id2);
	dbtree_print(db);              
	dbtree_delete_client(db, share3, pipe_id3);
	dbtree_print(db);              
	dbtree_delete_client(db, share4, pipe_id4);
	dbtree_print(db);              
	dbtree_delete_client(db, share5, pipe_id5);
	dbtree_print(db);              
	dbtree_delete_client(db, share6, pipe_id6);
	dbtree_print(db);              
	dbtree_delete_client(db, share7, pipe_id7);
	dbtree_print(db);              
	dbtree_delete_client(db, share8, pipe_id8);
	dbtree_print(db);              
	dbtree_delete_client(db, share9, pipe_id9);
	dbtree_print(db);


	puts("================finish delete shared client===============");
}

static void
test_delete_client()
{
	puts("================begin delete client===============");
	dbtree_delete_client(db, topic0, pipe_id0);
	dbtree_print(db);
	dbtree_delete_client(db, topic1, pipe_id1);
	dbtree_print(db);
	dbtree_delete_client(db, topic2, pipe_id2);
	dbtree_print(db);
	dbtree_delete_client(db, topic3, pipe_id3);
	dbtree_print(db);
	dbtree_delete_client(db, topic4, pipe_id4);
	dbtree_print(db);
	dbtree_delete_client(db, topic5, pipe_id5);
	dbtree_print(db);
	dbtree_delete_client(db, topic6, pipe_id6);
	dbtree_print(db);
	dbtree_delete_client(db, topic7, pipe_id7);
	dbtree_print(db);
	dbtree_delete_client(db, topic8, pipe_id8);
	dbtree_print(db);
	dbtree_delete_client(db, topic9, pipe_id9);
	dbtree_print(db);

	puts("================finish delete client===============");
}

static void
test_search_client()
{
	puts("================test search client==============");
	uint32_t *uv = dbtree_find_clients(db, topic0);
	if (uv) {
		cvector_free(uv);
	}
	puts("================finish search client===============");
}

static void
test_search_shared_client()
{
	puts("================test search shared client==============");
	for (int i = 0; i < 20; i++) {
		uint32_t *v = dbtree_find_shared_clients(db, topic0);

		if (v) {
			cvector_free(v);
		}

	}
	puts("================test search shared client==============");
}

static void
test_insert_retain()
{
	nng_msg_alloc(&retain0, 0);
	nng_msg_alloc(&retain1, 0);
	nng_msg_alloc(&retain2, 0);
	nng_msg_alloc(&retain3, 0);
	nng_msg_alloc(&retain4, 0);
	nng_msg_alloc(&retain5, 0);
	nng_msg_alloc(&retain6, 0);
	nng_msg_alloc(&retain7, 0);
	nng_msg_alloc(&retain8, 0);
	nng_msg_alloc(&retain9, 0);

	dbtree_insert_retain(db_ret, topic00, retain0);
	dbtree_print(db_ret);
	dbtree_insert_retain(db_ret, topic01, retain1);
	dbtree_print(db_ret);
	dbtree_insert_retain(db_ret, topic02, retain2);
	dbtree_print(db_ret);
	dbtree_insert_retain(db_ret, topic03, retain3);
	dbtree_print(db_ret);
	dbtree_insert_retain(db_ret, topic04, retain4);
	dbtree_print(db_ret);
	dbtree_insert_retain(db_ret, topic05, retain5);
	dbtree_print(db_ret);
	dbtree_insert_retain(db_ret, topic06, retain6);
	dbtree_print(db_ret);
	dbtree_insert_retain(db_ret, topic07, retain7);
	dbtree_print(db_ret);
	dbtree_insert_retain(db_ret, topic08, retain8);
	dbtree_print(db_ret);
	dbtree_insert_retain(db_ret, topic09, retain9);
	dbtree_print(db_ret);
}

static void
test_delete_retain()
{
	nng_msg *ret0 = NULL;
	nng_msg *ret1 = NULL;
	nng_msg *ret2 = NULL;
	nng_msg *ret3 = NULL;
	nng_msg *ret4 = NULL;
	nng_msg *ret5 = NULL;
	nng_msg *ret6 = NULL;
	nng_msg *ret7 = NULL;
	nng_msg *ret8 = NULL;
	nng_msg *ret9 = NULL;


	ret0 = dbtree_delete_retain(db_ret, topic00);
	dbtree_print(db_ret);
	ret1 = dbtree_delete_retain(db_ret, topic01);
	dbtree_print(db_ret);
	ret2 = dbtree_delete_retain(db_ret, topic02);
	dbtree_print(db_ret);
	ret3 = dbtree_delete_retain(db_ret, topic03);
	dbtree_print(db_ret);
	ret4 = dbtree_delete_retain(db_ret, topic04);
	dbtree_print(db_ret);
	ret5 = dbtree_delete_retain(db_ret, topic05);
	dbtree_print(db_ret);
	ret6 = dbtree_delete_retain(db_ret, topic06);
	dbtree_print(db_ret);
	ret7 = dbtree_delete_retain(db_ret, topic07);
	dbtree_print(db_ret);
	ret8 = dbtree_delete_retain(db_ret, topic08);
	dbtree_print(db_ret);
	ret9 = dbtree_delete_retain(db_ret, topic09);
	dbtree_print(db_ret);

	nng_msg_free(ret0);
	nng_msg_free(ret1);
	nng_msg_free(ret2);
	nng_msg_free(ret3);
	nng_msg_free(ret4);
	nng_msg_free(ret5);
	nng_msg_free(ret6);
	nng_msg_free(ret7);
	nng_msg_free(ret8);
	nng_msg_free(ret9);

}

static void *
test_single_thread(void *args)
{
	(void) args;
	for (int i = 0; i < TEST_LOOP; i++) {
		test_insert_client();
		test_search_client();
		test_delete_client();
	}
	return NULL;
}

void
dbtree_test(void)
{
	puts("\n----------------TEST START------------------");

	dbtree_create(&db);
	test_insert_shared_client();
	dbtree_print(db);
	test_search_shared_client();

	test_delete_shared_client();

	test_single_thread(NULL);
	// test_concurrent(test_single_thread);
	dbtree_destory(db);

	dbtree_create(&db_ret);
	test_insert_retain();
	puts("=======================================");
	nng_msg **r = dbtree_find_retain(db_ret, topic6);
	for (size_t i = 0; i < cvector_size(r); i++) {
		if (r[i]) {
			nng_msg_free(r[i]);
			printf("%p\t", r[i]);
		}
	}
	cvector_free(r);
	puts("");
	puts("=======================================");
	test_delete_retain();

	dbtree_destory(db_ret);
	puts("---------------TEST FINISHED----------------\n");
}

TEST_LIST = {
   {"dbtree_test", dbtree_test},

   {NULL, NULL} 
};