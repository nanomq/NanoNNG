//
// Copyright 2020 NanoMQ Team, Inc. <jaylin@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//
#include "nng/supplemental/nanolib/hash_table.h"
#include "core/nng_impl.h"
#include "nng/nng.h"
#include "nng/nng_debug.h"
#include "nng/supplemental/nanolib/binary_search.h"
#include "nng/supplemental/nanolib/khash.h"
#include "nng/supplemental/nanolib/mqtt_db.h"
#include <stdio.h>

#define dbhash_check_init(name, h, lock) \
	if (h == NULL) {                 \
		h = kh_init(name);       \
		nni_rwlock_init(&lock);  \
	}

static dbhash_atpair_t *dbhash_atpair_alloc(uint32_t alias, const char *topic);
static void             dbhash_atpair_free(dbhash_atpair_t *atpair);

KHASH_MAP_INIT_INT(alias_table, dbhash_atpair_t **)
static nni_rwlock alias_lock;
static khash_t(alias_table) *ah = NULL;

void
dbhash_init_alias_table(void)
{
	dbhash_check_init(alias_table, ah, alias_lock);
}

static dbhash_atpair_t **
find_atpair_vec(uint32_t p)
{
	khint_t k = kh_get(alias_table, ah, p);
	if (k == kh_end(ah)) {
		return NULL;
	}

	return kh_val(ah, k);
}

void
dbhash_insert_atpair(uint32_t p, uint32_t a, const char *t)
{
	int               absent;
	dbhash_atpair_t * atpair = dbhash_atpair_alloc(a, t);
	dbhash_atpair_t **vec    = NULL;
	khint32_t         k      = 0;

	nni_rwlock_wrlock(&alias_lock);
	k = kh_get(alias_table, ah, p);
	if (k == kh_end(ah)) {
		k = kh_put(alias_table, ah, p, &absent);
		cvector_push_back(vec, atpair);
		kh_value(ah, k) = vec;

	} else {
		size_t index = 0;
		vec          = kh_val(ah, k);

		if (true ==
		    binary_search((void **) vec, 0, &index, &a, alias_cmp)) {
			dbhash_atpair_t *tmp = vec[index];
			dbhash_atpair_free(tmp);
			vec[index] = atpair;
		} else {
			if (cvector_size(vec) == index) {
				cvector_push_back(vec, atpair);
			} else {
				cvector_insert(vec, index, atpair);
			}
		}

		kh_val(ah, k) = vec;
	}

	nni_rwlock_unlock(&alias_lock);
	return;
}

const char *
dbhash_find_atpair(uint32_t p, uint32_t a)
{
	nni_rwlock_rdlock(&alias_lock);
	const char *t = NULL;

	dbhash_atpair_t **vec = find_atpair_vec(p);
	if (vec) {
		size_t index = 0;

		if (true ==
		    binary_search((void **) vec, 0, &index, &a, alias_cmp)) {
			if (vec[index]) {
				t = vec[index]->topic;
			}
		}
	}

	nni_rwlock_unlock(&alias_lock);
	return t;
}

void
dbhash_del_atpair_queue(uint32_t p)
{
	nni_rwlock_wrlock(&alias_lock);

	khint32_t k = kh_get(alias_table, ah, p);
	if (k == kh_end(ah)) {
		nni_rwlock_unlock(&alias_lock);
		return;
	}

	dbhash_atpair_t **vec = kh_val(ah, k);
	;
	if (vec) {
		size_t size = cvector_size(vec);
		for (size_t i = 0; i < size; i++) {
			if (vec[i]) {
				dbhash_atpair_free(vec[i]);
			}
		}
		cvector_free(vec);
	}

	kh_del(alias_table, ah, k);
	nni_rwlock_unlock(&alias_lock);
}

void
dbhash_destroy_alias_table(void)
{
	// for each
	kh_destroy(alias_table, ah);
	ah = NULL;
}

KHASH_MAP_INIT_INT(pipe_table, topic_queue *)
static nni_rwlock pipe_lock;
static khash_t(pipe_table) *ph = NULL;

void
dbhash_init_pipe_table(void)
{
	dbhash_check_init(pipe_table, ph, pipe_lock);
}

void
dbhash_destroy_pipe_table(void)
{
	kh_destroy(pipe_table, ph);
	ph = NULL;
	return;
}

dbhash_ptpair_t *
dbhash_ptpair_alloc(uint32_t p, char *t)
{
	dbhash_ptpair_t *pt =
	    (dbhash_ptpair_t *) nni_zalloc(sizeof(dbhash_ptpair_t));

	if (pt == NULL) {
		return NULL;
	}
	pt->pipe  = p;
	pt->topic = nni_strdup(t);
	return pt;
}

void
dbhash_ptpair_free(dbhash_ptpair_t *pt)
{
	if (pt) {
		if (pt->topic) {
			free(pt->topic);
			pt->topic = NULL;
		}
		free(pt);
		pt = NULL;
	}
	return;
}

size_t
dbhash_get_pipe_cnt(void)
{
	nni_rwlock_wrlock(&pipe_lock);
	size_t size = kh_size(ph);
	nni_rwlock_unlock(&pipe_lock);
	return size;
}

dbhash_ptpair_t **
dbhash_get_ptpair_all(void)
{
	nni_rwlock_wrlock(&pipe_lock);
	size_t size = kh_size(ph);

	dbhash_ptpair_t **res = NULL;
	cvector_set_size(res, size);

	for (khint_t k = kh_begin(ph); k != kh_end(ph); ++k) {
		if (!kh_exist(ph, k))
			continue;
		topic_queue *    tq = kh_val(ph, k);
		dbhash_ptpair_t *pt =
		    dbhash_ptpair_alloc(kh_key(ph, k), tq->topic);
		cvector_push_back(res, pt);
	}

	nni_rwlock_unlock(&pipe_lock);
	return res;
}

topic_queue **
dbhash_get_topic_queue_all(size_t *sz)
{
	nni_rwlock_wrlock(&pipe_lock);
	size_t size = kh_size(ph);

	topic_queue **res =
	    (topic_queue **) malloc(size * sizeof(topic_queue *));

	for (khint_t k = kh_begin(ph); k != kh_end(ph); ++k) {
		if (kh_exist(ph, k)) {
			*res++ = kh_value(ph, k);
		}
	}
	nni_rwlock_unlock(&pipe_lock);

	*sz = size;
	return res;
}

static struct topic_queue *
new_topic_queue(char *val, uint8_t qos)
{
	struct topic_queue *tq  = NULL;
	int                 len = strlen(val);

	tq = (struct topic_queue *) malloc(sizeof(struct topic_queue));
	if (!tq) {
		fprintf(stderr, "malloc: Out of memory\n");
		fflush(stderr);
		abort();
	}
	tq->topic = (char *) malloc(sizeof(char) * (len + 1));
	if (!tq->topic) {
		fprintf(stderr, "malloc: Out of memory\n");
		fflush(stderr);
		abort();
	}
	memcpy(tq->topic, val, len);
	tq->topic[len] = '\0';
	tq->qos = qos;
	tq->next       = NULL;

	return tq;
}

static void
delete_topic_queue(struct topic_queue *tq)
{
	if (tq) {
		if (tq->topic) {
			free(tq->topic);
			tq->topic = NULL;
		}
		free(tq);
		tq = NULL;
	}
	return;
}

/*
 * @obj. _topic_hash.
 * @key. pipe_id.
 * @val. topic_queue.
 */

// TODO If we have same topic to same id,
// how to deal with ?
void
dbhash_insert_topic(uint32_t id, char *val, uint8_t qos)
{
	struct topic_queue *ntq = new_topic_queue(val, qos);
	struct topic_queue *tq  = NULL;

	nni_rwlock_wrlock(&pipe_lock);
	khint_t k = kh_get(pipe_table, ph, id);
	// Pipe id is find in hash table.
	if (k != kh_end(ph)) {
		tq                      = kh_val(ph, k);
		struct topic_queue *tmp = tq->next;
		tq->next                = ntq;
		ntq->next               = tmp;
	} else {
		// If not find pipe id in this hash table, we add a new one.
		int     absent;
		khint_t l = kh_put(pipe_table, ph, id, &absent);
		if (absent) {
			kh_val(ph, l) = ntq;
		}
	}
	nni_rwlock_unlock(&pipe_lock);
}

/*
 * @obj. _topic_hash.
 * @key. pipe_id.
 * @val. topic.
 */

bool
dbhash_check_topic(uint32_t id, char *val)
{

	// dbhash_check_init(pipe_table, ph, pipe_lock);
	if (!dbhash_check_id(id)) {
		return false;
	}

	bool ret = false;
	nni_rwlock_rdlock(&pipe_lock);
	struct topic_queue *tq = NULL;
	khint_t             k  = kh_get(pipe_table, ph, id);
	if (k != kh_end(ph)) {
		tq = kh_val(ph, k);
	}

	while (tq != NULL) {
		if (!strcmp(tq->topic, val)) {
			ret = true;
			break;
		}
		tq = tq->next;
	}
	nni_rwlock_unlock(&pipe_lock);

	return ret;
}

char *
dbhash_get_first_topic(uint32_t id)
{
	char *topic = NULL;
	nni_rwlock_wrlock(&pipe_lock);
	khint_t k = kh_get(pipe_table, ph, id);
	if (k != kh_end(ph)) {
		struct topic_queue *ret = kh_val(ph, k);
		if (ret && ret->topic) {
			topic = nni_strdup(ret->topic);
		}
	}
	nni_rwlock_unlock(&pipe_lock);

	return topic;
}

/*
 * @obj. _topic_hash.
 * @key. pipe_id.
 */

struct topic_queue *
dbhash_get_topic_queue(uint32_t id)
{

	// dbhash_check_init(pipe_table, ph, pipe_lock);
	struct topic_queue *ret = NULL;

	nni_rwlock_wrlock(&pipe_lock);
	khint_t k = kh_get(pipe_table, ph, id);
	if (k != kh_end(ph)) {
		ret = kh_val(ph, k);
	}
	nni_rwlock_unlock(&pipe_lock);

	return ret;
}

struct topic_queue *
dbhash_copy_topic_queue(uint32_t id)
{

	// dbhash_check_init(pipe_table, ph, pipe_lock);
	struct topic_queue *ret = NNI_ALLOC_STRUCT(ret);
	struct topic_queue *res = ret;
	

	nni_rwlock_wrlock(&pipe_lock);
	khint_t k = kh_get(pipe_table, ph, id);
	if (k != kh_end(ph)) {
		struct topic_queue *tq = kh_val(ph, k);
		while (tq) {
			ret->next = tq->next;
			ret->qos = tq->qos;
			ret->topic = nng_strdup(tq->topic);
			ret = ret->next;
			tq = tq->next;
			if (tq) {
				ret = NNI_ALLOC_STRUCT(ret);
			}
		}
	}
	nni_rwlock_unlock(&pipe_lock);

	return res;
}


/*
 * @obj. _topic_hash.
 * @key. pipe_id.
 */

// TODO
void
dbhash_del_topic(uint32_t id, char *topic)
{
	// dbhash_check_init(pipe_table, ph, pipe_lock);
	struct topic_queue *tt = NULL;
	struct topic_queue *tb = NULL;

	nni_rwlock_wrlock(&pipe_lock);
	khint_t k = kh_get(pipe_table, ph, id);
	if (k != kh_end(ph)) {
		tt = kh_val(ph, k);
	}

	if (tt == NULL) {
		nni_rwlock_unlock(&pipe_lock);
		return;
	}
	// If topic is the first one and no other topic follow,
	// we should delete the topic and delete pipe id from
	// this hash table.
	if (!strcmp(tt->topic, topic) && tt->next == NULL) {
		kh_del(pipe_table, ph, k);
		delete_topic_queue(tt);
		nni_rwlock_unlock(&pipe_lock);
		return;
	}

	// If topic is the first one with other topic follow,
	// we should delete it and assign the next pointer
	// to this key.
	if (!strcmp(tt->topic, topic)) {
		kh_val(ph, k) = tt->next;
		delete_topic_queue(tt);

		nni_rwlock_unlock(&pipe_lock);
		return;
	}

	while (tt) {
		if (!strcmp(tt->topic, topic)) {
			if (tt->next == NULL) {
				tb->next = NULL;
			} else {
				tb->next = tt->next;
			}
			break;
		}
		tb = tt;
		tt = tt->next;
	}

	delete_topic_queue(tt);

	nni_rwlock_unlock(&pipe_lock);
	return;
}

/*
 * @obj. _topic_hash.
 * @key.pipe_id.
 */

void *
del_topic_queue(uint32_t id, void *(*cb)(void *, char *), void *args)
{
	struct topic_queue *tq = NULL;
	khint_t             k  = kh_get(pipe_table, ph, id);
	void *              rv = NULL;
	if (k == kh_end(ph)) {
		return NULL;
	}

	tq = kh_val(ph, k);

	while (tq) {
		struct topic_queue *tt = tq;
		tq                     = tq->next;
		if (cb && args) {
			rv = cb(args, tt->topic);
		}
		delete_topic_queue(tt);
	}

	kh_del(pipe_table, ph, k);

	return rv;
}

/*
 * @obj. _topic_hash.
 * @key.pipe_id.
 */

void *
dbhash_del_topic_queue(uint32_t id, void *(*cb)(void *, char *), void *args)
{
	void *rv = NULL;
	nni_rwlock_wrlock(&pipe_lock);
	rv = del_topic_queue(id, cb, args);
	nni_rwlock_unlock(&pipe_lock);

	return rv;
}

/*
 * @obj. _topic_hash.
 */

static bool
check_id(uint32_t id)
{
	bool    ret = false;
	khint_t k   = kh_get(pipe_table, ph, id);
	if (k != kh_end(ph)) {
		ret = true;
	}
	return ret;
}

/*
 * @obj. _topic_hash.
 */

bool
dbhash_check_id(uint32_t id)
{
	bool ret = false;
	nni_rwlock_rdlock(&pipe_lock);
	ret = check_id(id);
	nni_rwlock_unlock(&pipe_lock);
	return ret;
}

void *
dbhash_check_id_and_do(uint32_t id, void *(*cb)(void *), void *arg)
{
	void *ret = NULL;
	nni_rwlock_wrlock(&pipe_lock);
	if (!check_id(id) && cb) {
		ret = cb(arg);
	}
	nni_rwlock_unlock(&pipe_lock);
	return ret;
}

/*
 * @obj. _topic_hash.
 * @key. pipe_id.
 */

void
dbhash_print_topic_queue(uint32_t id)
{
	// dbhash_check_init(pipe_table, ph, pipe_lock);
	struct topic_queue *tq = NULL;
	nni_rwlock_wrlock(&pipe_lock);
	khint_t k = kh_get(pipe_table, ph, id);
	if (k != kh_end(ph)) {
		tq = kh_val(ph, k);
	}

	int t_num = 0;
	while (tq) {
		printf("Topic number %d, topic subscribed: %s.\n", ++t_num,
		    tq->topic);
		tq = tq->next;
	}
	nni_rwlock_unlock(&pipe_lock);
}

/*
 * @obj. _cached_topic_hash.
 * @key. (DJBhashed) client_id.
 * @val. cached_topic_queue.
 */

// mqtt_hash<uint32_t, topic_queue *> _cached_topic_hash;
KHASH_MAP_INIT_INT(_cached_topic_hash, topic_queue *)
static khash_t(_cached_topic_hash) *ch = NULL;
static nni_rwlock cached_lock;

void
dbhash_init_cached_table(void)
{
	dbhash_check_init(_cached_topic_hash, ch, cached_lock);
}

void
dbhash_destroy_cached_table(void)
{
	kh_destroy(_cached_topic_hash, ch);
	ch = NULL;
}

static bool
cached_check_id(uint32_t key)
{
	khint_t k = kh_get(_cached_topic_hash, ch, key);
	if (k != kh_end(ch)) {
		return true;
	}
	return false;
}

/*
 * @obj. _cached_topic_hash.
 * @key. (DJBhashed) client_id.
 * @val. topic_queue
 */

static void
delete_cached_topic_one(struct topic_queue *ctq)
{
	if (ctq) {
		if (ctq->topic) {
			free(ctq->topic);
			ctq->topic = NULL;
		}
		free(ctq);
		ctq = NULL;
	}
	return;
}

void
del_cached_topic_all(uint32_t cid)
{
	struct topic_queue *ctq = NULL;
	khint_t             k   = kh_get(_cached_topic_hash, ch, cid);
	if (k != kh_end(ch)) {
		ctq = kh_val(ch, k);
		kh_del(_cached_topic_hash, ch, k);
	}

	while (ctq) {
		struct topic_queue *tt = ctq;
		ctq                    = ctq->next;
		delete_cached_topic_one(tt);
	}

	return;
}

/*
 * @obj. _topic_hash.
 * @key. pipe_id.
 * @obj. _cached_topic_hash.
 * @key. (DJBhashed) client_id.
 */

void
dbhash_cache_topic_all(uint32_t pid, uint32_t cid)
{
	struct topic_queue *tq_in_topic_hash = NULL;
	nni_rwlock_wrlock(&pipe_lock);
	khint_t k = kh_get(pipe_table, ph, pid);
	if (k != kh_end(ph)) {
		tq_in_topic_hash = kh_val(ph, k);
		kh_del(pipe_table, ph, k);
	}
	nni_rwlock_unlock(&pipe_lock);

	if (tq_in_topic_hash == NULL) {
		return;
	}

	nni_rwlock_wrlock(&cached_lock);
	if (cached_check_id(cid)) {
		// log_info("unexpected: cached hash instance is not vacant");
		del_cached_topic_all(cid);
	}
	int     absent;
	khint_t l     = kh_put(_cached_topic_hash, ch, cid, &absent);
	kh_val(ch, l) = tq_in_topic_hash;
	nni_rwlock_unlock(&cached_lock);
}

/*
 * @obj. _cached_topic_hash.
 * @key. (DJBhashed) client_id.
 * @obj. _topic_hash.
 * @key. pipe_id.
 */

void
dbhash_restore_topic_all(uint32_t cid, uint32_t pid)
{
	struct topic_queue *tq_in_cached = NULL;
	nni_rwlock_wrlock(&cached_lock);
	khint_t k = kh_get(_cached_topic_hash, ch, cid);
	if (k != kh_end(ch)) {
		tq_in_cached = kh_val(ch, k);
		kh_del(_cached_topic_hash, ch, k);
	}
	nni_rwlock_unlock(&cached_lock);

	if (tq_in_cached == NULL) {
		return;
	}

	nni_rwlock_wrlock(&pipe_lock);
	if (check_id(pid)) {
		// log_info("unexpected: hash instance is not vacant");
		del_topic_queue(pid, NULL, NULL);
	}
	int     absent;
	khint_t l     = kh_put(pipe_table, ph, pid, &absent);
	kh_val(ph, l) = tq_in_cached;
	nni_rwlock_unlock(&pipe_lock);
}

/*
 * @obj. _cached_topic_hash.
 * @key. (DJBhashed) client_id.
 * @val. topic_queue
 */

// FIXME Return pointer of topic_queue directly is not safe.
struct topic_queue *
dbhash_get_cached_topic(uint32_t cid)
{
	struct topic_queue *ctq = NULL;
	nni_rwlock_wrlock(&cached_lock);
	khint_t k = kh_get(_cached_topic_hash, ch, cid);
	if (k != kh_end(ch)) {
		ctq = kh_val(ch, k);
	}
	nni_rwlock_unlock(&cached_lock);
	return ctq;
}

/*
 * @obj. _cached_topic_hash.
 * @key. (DJBhashed) client_id.
 */

void
dbhash_del_cached_topic_all(uint32_t cid)
{
	struct topic_queue *ctq = NULL;
	nni_rwlock_wrlock(&cached_lock);
	khint_t k = kh_get(_cached_topic_hash, ch, cid);
	if (k != kh_end(ch)) {
		ctq = kh_val(ch, k);
		kh_del(_cached_topic_hash, ch, k);
	}

	while (ctq) {
		struct topic_queue *tt = ctq;
		ctq                    = ctq->next;
		delete_cached_topic_one(tt);
	}

	nni_rwlock_unlock(&cached_lock);
	return;
}

/*
 * @obj. _cached_topic_hash.
 * @key. (DJBhashed) client_id.
 */

bool
dbhash_cached_check_id(uint32_t key)
{
	bool ret = false;
	nni_rwlock_rdlock(&cached_lock);
	ret = cached_check_id(key);
	nni_rwlock_unlock(&cached_lock);
	return ret;
}

dbhash_atpair_t *
dbhash_atpair_alloc(uint32_t alias, const char *topic)
{
	if (topic == NULL) {
		debug_msg("Topic should not be NULL");
	}
	dbhash_atpair_t *atpair =
	    (dbhash_atpair_t *) nni_zalloc(sizeof(dbhash_atpair_t));
	if (atpair == NULL) {
		debug_msg("Memory alloc error.");
		return NULL;
	}
	atpair->alias = alias;
	atpair->topic = nni_strdup(topic);

	return atpair;
}

static void
dbhash_atpair_free(dbhash_atpair_t *atpair)
{
	if (atpair) {
		if (atpair->topic) {
			free(atpair->topic);
			atpair->topic = NULL;
		}
		free(atpair);
		atpair = NULL;
	}
}
