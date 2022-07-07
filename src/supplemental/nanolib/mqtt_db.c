//
// Copyright 2020 NanoMQ Team, Inc. <jaylin@emqx.io> //
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include <limits.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include "core/nng_impl.h"
#include "nng/nng_debug.h"
#include "nng/supplemental/nanolib/binary_search.h"
#include "nng/supplemental/nanolib/cvector.h"
#include "nng/supplemental/nanolib/hash_table.h"
#include "nng/supplemental/nanolib/mqtt_db.h"
#include "nng/supplemental/nanolib/zmalloc.h"

#define ROUND_ROBIN
// #define RANDOM

static int acnt = 0;

typedef struct dbtree_node dbtree_node;

struct dbtree_node {
	char *             topic;
	int                plus;
	int                well;
	dbtree_retain_msg *retain;
	cvector(uint32_t) clients;
	cvector(dbtree_node *) child;
	nni_rwlock rwlock;
};

typedef struct {
	char * topic;
	char **clients;
	int    cld_cnt;
} dbtree_info;

struct dbtree {
	dbtree_node *root;
	nni_rwlock   rwlock;
};

/**
 * @brief node_cmp - A callback to compare different node
 * @param x - normally x is dbtree_node
 * @param y - y is topic we want to compare
 * @return 0, minus or plus, based on strcmp
 */
static inline int
node_cmp(void *x_, void *y_)
{
	char *       y     = (char *) y_;
	dbtree_node *ele_x = (dbtree_node *) x_;
	return strcmp(ele_x->topic, y);
}

// TODO
/**
 * @brief ids_cmp - A callback to compare different id
 * @param x - normally x is pointer of id
 * @param y - normally y is pointer of id
 * @return 0, minus or plus, based on strcmp
 */
static inline int
ids_cmp(uint32_t x, uint32_t y)
{
	return y - x;
}

/**
 * @brief print_client - A way to print client in vec.
 * @param v - normally v is an dynamic array
 * @return void
 */
static void
print_client(uint32_t *v)
{
#ifdef NOLOG
	NNI_ARG_UNUSED(v);
#else
	puts("____________PRINT_DB_CLIENT___________");
	if (v) {
		for (int i = 0; i < cvector_size(v); i++) {
			printf("%d\t", v[i]);
		}
		puts("");
	}
	puts("____________PRINT_DB_CLIENT___________");
#endif
}

/**
 * @brief topic_count - Count how many levels.
 * @param topic - original topic
 * @return topic level
 */
static int
topic_count(char *topic)
{
	int   cnt = 0;
	char *t   = topic;

	while (t) {
		// debug_msg("%s", t);
		t = strchr(t, '/');
		cnt++;
		if (t == NULL) {
			break;
		}
		t++;
	}

	return cnt;
}

/**
 * @brief topic_parse - Parsing topic to topic queue.
 * @param topic - original topic
 * @return topic queue
 */
static char **
topic_parse(char *topic)
{
	if (topic == NULL) {
		debug_msg("topic is NULL");
		return NULL;
	}

	int   row   = 0;
	int   len   = 2;
	char *b_pos = topic;
	char *pos   = NULL;

	int cnt = topic_count(topic);

	// Here we will get (cnt + 1) memory, one for NULL end
	char **topic_queue = (char **) zmalloc(sizeof(char *) * (cnt + 1));

	while ((pos = strchr(b_pos, '/')) != NULL) {

		len              = pos - b_pos + 1;
		topic_queue[row] = (char *) zmalloc(sizeof(char) * len);
		memcpy(topic_queue[row], b_pos, (len - 1));
		topic_queue[row][len - 1] = '\0';
		b_pos                     = pos + 1;
		row++;
	}

	len = strlen(b_pos);

	topic_queue[row] = (char *) zmalloc(sizeof(char) * (len + 1));
	memcpy(topic_queue[row], b_pos, (len));
	topic_queue[row][len] = '\0';
	topic_queue[++row]    = NULL;

	return topic_queue;
}

/**
 * @brief topic_queue_free - Free topic queue memory.
 * @param topic_queue - topic array
 * @return void
 */

static void
topic_queue_free(char **topic_queue)
{
	char * t  = NULL;
	char **tt = topic_queue;

	while (*topic_queue) {
		t = *topic_queue;
		topic_queue++;
		zfree(t);
		t = NULL;
	}

	if (tt) {
		zfree(tt);
	}
}

void ***
dbtree_get_tree(dbtree *db, void *(*cb)(uint32_t pipe_id))
{
	if (db == NULL) {
		return NULL;
	}

	nni_rwlock_wrlock(&(db->rwlock));

	dbtree_node *  node    = db->root;
	dbtree_node ** nodes   = NULL;
	dbtree_node ** nodes_t = NULL;
	dbtree_info ***ret     = NULL;

	cvector_push_back(nodes, node);
	while (!cvector_empty(nodes)) {
		dbtree_info **ret_line_ping = NULL;
		for (size_t i = 0; i < cvector_size(nodes); i++) {
			dbtree_info *vn = zmalloc(sizeof(dbtree_info));
			vn->clients     = NULL;
			if (cb) {
				for (size_t j = 0;
				     j < cvector_size(nodes[i]->clients);
				     j++) {
					void *val = cb(nodes[i]->clients[j]);
					if (val) {
						cvector_push_back(
						    vn->clients, val);
					}
				}
			}

			cvector_push_back(ret_line_ping, vn);

			vn->topic   = zstrdup(nodes[i]->topic);
			vn->cld_cnt = cvector_size(nodes[i]->child);
			for (size_t j = 0; j < (size_t) vn->cld_cnt; j++) {
				cvector_push_back(
				    nodes_t, (nodes[i]->child)[j]);
			}
		}
		cvector_push_back(ret, ret_line_ping);
		cvector_free(nodes);
		nodes = NULL;

		dbtree_info **ret_line_pang = NULL;
		for (size_t i = 0; i < cvector_size(nodes_t); i++) {
			dbtree_info *vn = zmalloc(sizeof(dbtree_info));
			vn->clients     = NULL;
			if (cb) {
				for (size_t j = 0;
				     j < cvector_size(nodes_t[i]->clients);
				     j++) {
					void *val = cb(nodes_t[i]->clients[j]);
					if (val) {
						cvector_push_back(
						    vn->clients, val);
					}
				}
			}

			cvector_push_back(ret_line_pang, vn);

			vn->topic   = zstrdup(nodes_t[i]->topic);
			vn->cld_cnt = cvector_size(nodes_t[i]->child);
			for (size_t j = 0; j < (size_t) vn->cld_cnt; j++) {
				cvector_push_back(
				    nodes, (nodes_t[i]->child)[j]);
			}
		}
		cvector_push_back(ret, ret_line_pang);
		cvector_free(nodes_t);
		nodes_t = NULL;
	}
	nni_rwlock_unlock(&(db->rwlock));
	return (void ***) ret;
}

/**
 * @brief dbtree_print - Print dbtree for debug.
 * @param dbtree - dbtree
 * @return void
 */
#ifdef NOLOG
void
dbtree_print(dbtree *db)
{
	NNI_ARG_UNUSED(db);
	return;
}
#else
void
dbtree_print(dbtree *db)
{
	if (db == NULL) {
		return;
	}

	nni_rwlock_wrlock(&(db->rwlock));

	dbtree_node *node = db->root;
	dbtree_node **nodes = NULL;
	dbtree_node **nodes_t = NULL;

	const char node_fmt[] = "[%-5s]\t";
	cvector_push_back(nodes, node);
	puts("___________PRINT_DB_TREE__________");
	while (!cvector_empty(nodes)) {
		for (int i = 0; i < cvector_size(nodes); i++) {
			printf(node_fmt, nodes[i]->topic);

			for (int j = 0; j < cvector_size(nodes[i]->child);
			     j++) {
				cvector_push_back(
				    nodes_t, (nodes[i]->child)[j]);
			}
		}
		printf("\n");
		cvector_free(nodes);
		nodes = NULL;

		for (int i = 0; i < cvector_size(nodes_t); i++) {
			printf(node_fmt, nodes_t[i]->topic);

			for (int j = 0; j < cvector_size(nodes_t[i]->child);
			     j++) {
				cvector_push_back(
				    nodes, (nodes_t[i]->child)[j]);
			}
		}
		printf("\n");
		cvector_free(nodes_t);
		nodes_t = NULL;
	}
	nni_rwlock_unlock(&(db->rwlock));
	puts("___________PRINT_DB_TREE__________");
}
#endif

/**
 * @brief skip_wildcard - To get left boundry of binary search
 * @param node - dbtree_node
 * @return l - left boundry
 */
static int
skip_wildcard(dbtree_node *node)
{
	int l = 0;
	if (node->plus != -1) {
		l++;
	}
	if (node->well != -1) {
		l++;
	}

	return l;
}

/**
 * @brief find_next - check if this topic is exist in this level.
 * @param node - dbtree_node
 * @param equal - a return state value
 * @param topic_queue - topic queue
 * @param index - search index will be return
 * @return dbtree_node we find or original node
 */
// TODO topic or topic queue
// TODO return NULL if no find ?
static dbtree_node *
find_next(dbtree_node *node, bool *equal, char **topic_queue, size_t *index)
{
	if (node == NULL || node->child == NULL) {
		return NULL;
	}

	cvector(dbtree_node *) t = node->child;

	int l = skip_wildcard(node);

	if (true ==
	    binary_search((void **) t, l, index, *topic_queue, node_cmp)) {
		*equal = true;
		return t[*index];
	}

	return node;
}

/**
 * @brief dbtree_node_new - create a node
 * @param topic - topic
 * @return dbtree_node*
 */
static dbtree_node *
dbtree_node_new(char *topic)
{
	dbtree_node *node = NULL;
	node              = (dbtree_node *) zmalloc(sizeof(dbtree_node));
	if (node == NULL) {
		return NULL;
	}
	node->topic = zstrdup(topic);
	debug_msg("New node: [%s]", node->topic);

	node->retain  = NULL;
	node->child   = NULL;
	node->clients = NULL;
	node->well    = -1;
	node->plus    = -1;

	nni_rwlock_init(&node->rwlock);
	return node;
}

/**
 * @brief dbtree_node_free - Free a node memory
 * @param node - dbtree_node *
 * @return void
 */
static void
dbtree_node_free(dbtree_node *node)
{
	if (node) {
		if (node->topic) {
			debug_msg("Delete node: [%s]", node->topic);
			zfree(node->topic);
			node->topic = NULL;
		}
		nni_rwlock_fini(&node->rwlock);
		zfree(node);
		node = NULL;
	}
}

/**
 * @brief dbtree_create - Create a dbtree, declare a global variable as func
 * para
 * @param dbtree - dbtree
 * @return void
 */
void
dbtree_create(dbtree **db)
{
	*db = (dbtree *) zmalloc(sizeof(dbtree));
	memset(*db, 0, sizeof(dbtree));

	dbtree_node *node = dbtree_node_new("\0");
	(*db)->root       = node;
	nni_rwlock_init(&(*db)->rwlock);
#ifdef RANDOM
	srand(time(NULL));
#endif
	return;
}

/**
 * @brief dbtree_destory - Destory dbtree tree
 * @param dbtree - dbtree
 * @return void
 */
void
dbtree_destory(dbtree *db)
{
	if (db) {
		dbtree_node_free(db->root);
		zfree(db);
		db = NULL;
	}

	// nni_rwlock_destroy(&(db->rwlock));
	// nni_rwlock_destroy(&(db->rwlock_session));
}

/**
 * @brief insert_client_cb - insert a client on the right position
 * @param node - dbtree_node
 * @param pipe_id - pipe id
 * @return
 */
static void *
insert_client_cb(dbtree_node *node, void *pipe_id)
{
	nni_rwlock_wrlock(&(node->rwlock));
	size_t index = 0;
	if (false ==
	    binary_search_uint32(
	        node->clients, 0, &index, *(uint32_t *) pipe_id, ids_cmp)) {
		if (index == cvector_size(node->clients)) {
			cvector_push_back(
			    node->clients, *(uint32_t *) pipe_id);
		} else {
			cvector_insert(
			    node->clients, index, *(uint32_t *) pipe_id);
		}
	}
	nni_rwlock_unlock(&(node->rwlock));
	return NULL;
}

/**
 * @brief is_plus - Determine if the current topic is "#"
 * @param topic_data - topic in one level
 * @return true, if curr topic is "#"
 */
static bool
is_well(char *topic_data)
{
	if (topic_data == NULL) {
		return false;
	}
	return !strcmp(topic_data, "#");
}

/**
 * @brief is_plus - Determine if the current topic is "+"
 * @param topic_data - topic in one level
 * @return true, if curr topic is "+"
 */
static bool
is_plus(char *topic_data)
{
	// TODO
	if (topic_data == NULL) {
		return false;
	}
	return !strcmp(topic_data, "+");
}

/**
 * @brief dbtree_node_insert - insert node until topic_queue is NULL
 * @param node - dbtree_node
 * @param topic_queue - topic queue position
 * @param client - client info
 * @return void
 */
static dbtree_node *
dbtree_node_insert(dbtree_node *node, char **topic_queue)
{
	if (node == NULL || topic_queue == NULL) {
		debug_msg("node or topic_queue is NULL");
		return NULL;
	}

	while (*topic_queue) {
		dbtree_node *new_node = NULL;
		if (is_well(*topic_queue)) {
			if (node->well != -1) {
				new_node = node->child[node->well];
			} else {
				if (node->plus == 0) {
					node->plus = 1;
				}

				node->well = 0;
				new_node   = dbtree_node_new(*topic_queue);
				cvector_insert(node->child,
				    (size_t) node->well, new_node);
			}

		} else if (is_plus(*topic_queue)) {
			if (node->plus != -1) {
				new_node = node->child[node->plus];
			} else {
				if (node->well == 0) {
					node->well = 1;
				}

				node->plus = 0;
				new_node   = dbtree_node_new(*topic_queue);
				cvector_insert(node->child,
				    (size_t) node->plus, new_node);
			}
		} else {
			size_t l = skip_wildcard(node);
			if (l == cvector_size(node->child)) {
				new_node = dbtree_node_new(*topic_queue);
				cvector_push_back(node->child, new_node);

			} else {
				size_t index = 0;
				if (false ==
				    binary_search((void **) node->child, l,
				        &index, *topic_queue, node_cmp)) {
					new_node =
					    dbtree_node_new(*topic_queue);

					//  TODO
					if (index ==
					    cvector_size(node->child)) {
						cvector_push_back(
						    node->child, new_node);
					} else {
						cvector_insert(node->child,
						    index, new_node);
					}
				} else {
					new_node = node->child[index];
				}
			}
		}

		topic_queue++;
		node = new_node;
	}

	return node;
}

/**
 * @brief search_insert_node - check if this
 * topic and client id is exist on the tree, if
 * there is not exist, this func will insert
 * related node and client on the tree
 * @param dbtree - dbtree_node
 * @param topic - topic
 * @param client - client
 * @return
 */
static void *
search_insert_node(dbtree *db, char *topic, void *args,
    void *(*inserter)(dbtree_node *node, void *args))
{
	if (db == NULL || topic == NULL) {
		debug_msg("db or topic is NULL");
		return NULL;
	}

	char **topic_queue = topic_parse(topic);
	char **for_free    = topic_queue;

	nni_rwlock_wrlock(&(db->rwlock));
	dbtree_node *node = db->root;
	// while dbtree is NULL, we will insert directly.

	if (!(node->child && *node->child)) {
		node = dbtree_node_insert(node, topic_queue);
	} else {

		while (*topic_queue && node->child && *node->child) {
			dbtree_node *node_t = *node->child;
			debug_msg("topic is: %s, node->topic is: %s",
			    *topic_queue, node_t->topic);
			if (strcmp(node_t->topic, *topic_queue)) {
				bool   equal = false;
				size_t index = 0;

				node_t = find_next(
				    node, &equal, topic_queue, &index);
				if (equal == false) {
					/*
					 ** If no node is matched with topic
					 ** insert node until topic_queue
					 ** is NULL
					 */
					debug_msg("searching unequal");
					node = dbtree_node_insert(
					    node_t, topic_queue);
					break;
				}
			}

			if (node_t->child && *node_t->child &&
			    *(topic_queue + 1)) {
				topic_queue++;
				node = node_t;
			} else if (*(topic_queue + 1) == NULL) {
				debug_msg("Search and insert client");
				node = node_t;
				break;
			} else {
				debug_msg("Insert node and client");
				node = dbtree_node_insert(
				    node_t, topic_queue + 1);
				break;
			}
		}
	}

	void *ret = inserter(node, args);
	nni_rwlock_unlock(&(db->rwlock));
	topic_queue_free(for_free);
	return ret;
}

void *
dbtree_insert_client(dbtree *db, char *topic, uint32_t pipe_id)
{
	return search_insert_node(
	    db, topic, (void *) &pipe_id, insert_client_cb);
}

/**
 * @brief collect_clients - Get all clients in nodes
 * @param vec - all clients obey this rule will insert
 * @param nodes - all node need to be compare
 * @param nodes_t - all node need to be compare next time
 * @param topic_queue - topic queue position
 * @return all clients on lots of nodes
 */
static uint32_t **
collect_clients(uint32_t **vec, dbtree_node **nodes, dbtree_node ***nodes_t,
    char **topic_queue)
{
	// TODO insert sort for clients
	while (!cvector_empty(nodes)) {
		dbtree_node **node_t_ = cvector_end(nodes) - 1;
		dbtree_node * node_t  = *node_t_;
		cvector_pop_back(nodes);

		if (node_t == NULL || node_t->child == NULL ||
		    (*(node_t->child)) == NULL) {
			continue;
		}

		dbtree_node * t     = *node_t->child;
		dbtree_node **child = node_t->child;

		if (node_t->well != -1) {
			if (!cvector_empty(child[node_t->well]->clients)) {
				debug_msg("Find # tag");
				cvector_push_back(
				    vec, child[node_t->well]->clients);
			}
		}

		if (node_t->plus != -1) {
			if (*(topic_queue + 1) == NULL) {
				debug_msg("add + clients");
				if (!cvector_empty(
				        child[node_t->plus]->clients)) {
					cvector_push_back(
					    vec, child[node_t->plus]->clients);
				}

			} else {
				cvector_push_back(
				    (*nodes_t), child[node_t->plus]);
				debug_msg("add node_t: %s",
				    (*(cvector_end((*nodes_t)) - 1))->topic);
			}
		}

		bool equal = false;
		if (strcmp(t->topic, *topic_queue)) {
			size_t index = 0;
			t = find_next(node_t, &equal, topic_queue, &index);
		} else {
			equal = true;
		}

		if (equal == true) {
			debug_msg("Searching client: %s", t->topic);
			if (*(topic_queue + 1) == NULL) {
				if (!cvector_empty(t->clients)) {
					debug_msg(
					    "Searching client: %s", t->topic);
					cvector_push_back(vec, t->clients);
				}

				if (t->well != -1) {
					t = t->child[t->well];
					if (!cvector_empty(t->clients)) {
						debug_msg(
						    "Searching client: %s",
						    t->topic);
						cvector_push_back(
						    vec, t->clients);
					}
				}

			} else {
				debug_msg("Searching client: %s", t->topic);
				cvector_push_back((*nodes_t), t);
				debug_msg("add node_t: %s",
				    (*(cvector_end((*nodes_t)) - 1))->topic);
			}
		}
	}

	return vec;
}

// TODO
/**
 * @brief iterate_client - Deduplication for all clients
 * @param v - client
 * @return pipe id vector
 */
static uint32_t *
iterate_client(uint32_t **v)
{
	cvector(uint32_t) ids = NULL;

	if (v) {
		for (size_t i = 0; i < cvector_size(v); ++i) {

			for (size_t j = 0; j < cvector_size(v[i]); j++) {
				size_t index = 0;

				if (false ==
				    binary_search_uint32(
				        ids, 0, &index, v[i][j], ids_cmp)) {
					if (cvector_empty(ids) ||
					    index == cvector_size(ids)) {
						cvector_push_back(
						    ids, v[i][j]);
					} else {
						cvector_insert(
						    ids, index, v[i][j]);
					}
				}
			}
		}
	}

	return ids;
}

uint32_t *
search_client(dbtree *db, char *topic)
{
	if (db == NULL || topic == NULL) {
		debug_msg("db or topic is NULL");
		return NULL;
	}

	char **   topic_queue = topic_parse(topic);
	char **   for_free    = topic_queue;
	uint32_t *ret         = NULL;

	nni_rwlock_rdlock(&(db->rwlock));

	dbtree_node *node              = db->root;
	cvector(uint32_t *) pipe_ids   = NULL;
	cvector(dbtree_node *) nodes   = NULL;
	cvector(dbtree_node *) nodes_t = NULL;

	if (node->child && *node->child) {
		cvector_push_back(nodes, node);
	}

	while (*topic_queue && (!cvector_empty(nodes))) {
		pipe_ids =
		    collect_clients(pipe_ids, nodes, &nodes_t, topic_queue);
		topic_queue++;
		if (*topic_queue == NULL) {
			break;
		}
		pipe_ids =
		    collect_clients(pipe_ids, nodes_t, &nodes, topic_queue);
		topic_queue++;
	}

	ret = iterate_client(pipe_ids);

	nni_rwlock_unlock(&(db->rwlock));
	topic_queue_free(for_free);
	cvector_free(nodes);
	cvector_free(nodes_t);
	cvector_free(pipe_ids);

	return ret;
}

uint32_t *
dbtree_find_clients(dbtree *db, char *topic)
{
	return search_client(db, topic);
}

/**
 * @brief delete_dbtree_client - delete dbtree client
 * @param node - dbtree_node
 * @param id - client id
 * @param pipe_id - pipe id
 * @return
 */
static void *
delete_dbtree_client(dbtree_node *node, uint32_t pipe_id)
{
	size_t index = 0;
	void * ctxt  = NULL;

	nni_rwlock_wrlock(&(node->rwlock));
	if (true ==
	    binary_search_uint32(node->clients, 0, &index, pipe_id, ids_cmp)) {
		cvector_erase(node->clients, (size_t) index);
		print_client(node->clients);

		if (cvector_empty(node->clients)) {
			cvector_free(node->clients);
			node->clients = NULL;
		} else {
			print_client(node->clients);
		}
	} else {
		debug_msg("Not find pipe id: [%d]", pipe_id);
		debug_msg("node->topic: %s", node->topic);
		for (size_t i = 0; i < cvector_size(node->clients); i++) {
			debug_msg(
			    "node->clients[%d]: [%d]:", i, node->clients[i]);
		}
	}

	nni_rwlock_unlock(&(node->rwlock));
	return ctxt;
}

/**
 * @brief delete_dbtree_node - delete dbtree node
 * @param dbtree - dbtree_node
 * @param index - index
 * @return
 */
static int
delete_dbtree_node(dbtree_node *node, size_t index)
{
	nni_rwlock_wrlock(&(node->rwlock));
	dbtree_node *node_t = node->child[index];
	// TODO plus && well

	if (cvector_empty(node_t->child) && cvector_empty(node_t->clients)) {
		debug_msg("Delete node: [%s]", node_t->topic);
		cvector_free(node_t->child);
		cvector_free(node_t->clients);
		zfree(node_t->topic);
		cvector_erase(node->child, index);
		zfree(node_t);
		node_t = NULL;
		if (index == 0) {
			if (node->plus >= 0) {
				node->plus--;
			}

			if (node->well >= 0) {
				node->well--;
			}
		}

		if (index == 1) {
			if (node->plus == 1) {
				node->plus = -1;
			}

			if (node->well == 1) {
				node->well = -1;
			}
		}
	}

	if (cvector_empty(node->child)) {
		cvector_free(node->child);
		node->child = NULL;
	}

	nni_rwlock_unlock(&(node->rwlock));
	return 0;
}

void *
dbtree_delete_client(dbtree *db, char *topic, uint32_t pipe_id)
{
	if (db == NULL || topic == NULL) {
		debug_msg("db or topic is NULL");
		return NULL;
	}

	nni_rwlock_wrlock(&(db->rwlock));

	char **       topic_queue = topic_parse(topic);
	char **       for_free    = topic_queue;
	dbtree_node * node        = db->root;
	dbtree_node **node_buf    = NULL;
	int *         vec         = NULL;
	size_t        index       = 0;

	while (*topic_queue && node->child && *node->child) {
		index               = 0;
		dbtree_node *node_t = *node->child;
		debug_msg("topic is: %s, node->topic is: %s", *topic_queue,
		    node_t->topic);
		if (strcmp(node_t->topic, *topic_queue)) {
			bool equal = false;
			if (is_well(*topic_queue) && (node->well != -1)) {
				index = node->well;
				equal = true;
			}

			if (is_plus(*topic_queue) && (node->plus != -1)) {
				index = node->plus;
				equal = true;
			}

			node_t = node->child[index];

			if (equal == false) {
				// TODO node_t or node->child[index], to
				// determine if node_t is needed
				node_t = find_next(
				    node, &equal, topic_queue, &index);
				if (equal == false) {
					debug_msg("searching unequal");
					goto mem_free;
				}
			}
		}

		if (node_t->child && *node_t->child && *(topic_queue + 1)) {
			topic_queue++;
			cvector_push_back(node_buf, node);
			cvector_push_back(vec, index);
			node = node_t;

		} else if (*(topic_queue + 1) == NULL) {
			debug_msg("Search and delete client");
			debug_msg("node->topic: %s", node->topic);
			break;
		} else {
			debug_msg("No node and client need to be delete");
			goto mem_free;
		}
	}

	if (node->child) {
		delete_dbtree_client(node->child[index], pipe_id);
		delete_dbtree_node(node, index);
	}

	while (!cvector_empty(node_buf) && !cvector_empty(vec)) {
		dbtree_node *t = *(cvector_end(node_buf) - 1);
		int          i = *(cvector_end(vec) - 1);
		cvector_pop_back(node_buf);
		cvector_pop_back(vec);

		delete_dbtree_node(t, i);
	}

mem_free:
	cvector_free(node_buf);
	topic_queue_free(for_free);
	cvector_free(vec);
	nni_rwlock_unlock(&(db->rwlock));

	return NULL;
}

static void *
insert_dbtree_retain(dbtree_node *node, void *args)
{
	dbtree_retain_msg *retain = (dbtree_retain_msg *) args;
	void *             ret    = NULL;
	nni_rwlock_wrlock(&(node->rwlock));
	if (node->retain != NULL) {
		ret = node->retain;
	}

	node->retain = retain;

	nni_rwlock_unlock(&(node->rwlock));

	return ret;
}

void *
dbtree_insert_retain(dbtree *db, char *topic, dbtree_retain_msg *ret_msg)
{
	return search_insert_node(db, topic, ret_msg, insert_dbtree_retain);
}

dbtree_retain_msg **
collect_retain_well(dbtree_retain_msg **vec, dbtree_node *node)
{
	dbtree_node **nodes   = NULL;
	dbtree_node **nodes_t = NULL;
	cvector_push_back(nodes, node);
	while (!cvector_empty(nodes)) {
		for (size_t i = 0; i < cvector_size(nodes); i++) {
			if (nodes[i]->retain) {
				cvector_push_back(vec, nodes[i]->retain);
			}

			for (size_t j = 0; j < cvector_size(nodes[i]->child);
			     j++) {
				cvector_push_back(
				    nodes_t, (nodes[i]->child)[j]);
			}
		}

		cvector_free(nodes);
		nodes = NULL;

		for (size_t i = 0; i < cvector_size(nodes_t); i++) {
			if (nodes_t[i]->retain) {
				cvector_push_back(vec, nodes_t[i]->retain);
			}

			for (size_t j = 0; j < cvector_size(nodes_t[i]->child);
			     j++) {
				cvector_push_back(
				    nodes, (nodes_t[i]->child)[j]);
			}
		}
		cvector_free(nodes_t);
		nodes_t = NULL;
	}

	return vec;
}

/**
 * @brief collect_retains - Get all retain in nodes
 * @param vec - all clients obey this rule will insert
 * @param nodes - all node need to be compare
 * @param nodes_t - all node need to be compare next time
 * @param topic_queue - topic queue position
 * @return all clients on lots of nodes
 */
static dbtree_retain_msg **
collect_retains(dbtree_retain_msg **vec, dbtree_node **nodes,
    dbtree_node ***nodes_t, char **topic_queue)
{

	while (!cvector_empty(nodes)) {
		dbtree_node **node_t_ = cvector_end(nodes) - 1;
		dbtree_node * node_t  = *node_t_;
		cvector_pop_back(nodes);

		if (node_t == NULL || node_t->child == NULL ||
		    (*(node_t->child)) == NULL) {
			continue;
		}

		dbtree_node * t     = *node_t->child;
		dbtree_node **child = node_t->child;

		if (is_well(*topic_queue)) {
			vec = collect_retain_well(vec, node_t);
			break;
		} else if (is_plus(*topic_queue)) {
			if (*(topic_queue + 1) == NULL) {
				for (size_t i = 0; i < cvector_size(child);
				     i++) {
					node_t = child[i];
					if (node_t->retain) {
						cvector_push_back(
						    vec, node_t->retain);
					}
				}

			} else {
				for (size_t i = 0; i < cvector_size(child);
				     i++) {
					node_t = child[i];
					cvector_push_back(
					    ((*nodes_t)), node_t);
				}
			}

		} else {
			bool equal = false;

			if (strcmp(t->topic, *topic_queue)) {
				size_t index = 0;
				t            = find_next(
                                    node_t, &equal, topic_queue, &index);

			} else {
				equal = true;
			}

			if (equal == true) {
				debug_msg(
				    "Searching client: %s", node_t->topic);
				if (*(topic_queue + 1) == NULL) {
					if (t->retain) {
						debug_msg(
						    "Searching client: %s",
						    t->topic);
						cvector_push_back(
						    vec, t->retain);
					}
				} else {
					debug_msg(
					    "Searching client: %s", t->topic);
					cvector_push_back((*nodes_t), t);
					debug_msg("add node_t: %s",
					    (*(cvector_end((*nodes_t)) - 1))
					        ->topic);
				}
			}
		}
	}

	return vec;
}

dbtree_retain_msg **
dbtree_find_retain(dbtree *db, char *topic)
{

	if (db == NULL || topic == NULL) {
		debug_msg("db or topic is NULL");
		return NULL;
	}
	char **topic_queue = topic_parse(topic);
	char **for_free    = topic_queue;
	nni_rwlock_rdlock(&(db->rwlock));

	dbtree_node *node                 = db->root;
	cvector(dbtree_retain_msg *) rets = NULL;
	cvector(dbtree_node *) nodes      = NULL;
	cvector(dbtree_node *) nodes_t    = NULL;

	if (node->child && *node->child) {
		cvector_push_back(nodes, node);
	}

	while (*topic_queue && (!cvector_empty(nodes))) {

		rets = collect_retains(rets, nodes, &nodes_t, topic_queue);
		topic_queue++;
		if (*topic_queue == NULL) {
			break;
		}
		rets = collect_retains(rets, nodes_t, &nodes, topic_queue);
		topic_queue++;
	}

	nni_rwlock_unlock(&(db->rwlock));

	topic_queue_free(for_free);
	cvector_free(nodes);
	cvector_free(nodes_t);

	return rets;
}

static void *
delete_dbtree_retain(dbtree_node *node)
{
	if (node == NULL) {
		debug_msg("node is NULL");
		return NULL;
	}
	void *retain = NULL;
	if (node) {
		retain       = node->retain;
		node->retain = NULL;
	}

	return retain;
}

void *
dbtree_delete_retain(dbtree *db, char *topic)
{
	if (db == NULL || topic == NULL) {
		debug_msg("db or topic is NULL");
		return NULL;
	}
	nni_rwlock_wrlock(&(db->rwlock));

	char **       topic_queue = topic_parse(topic);
	char **       for_free    = topic_queue;
	dbtree_node * node        = db->root;
	dbtree_node **node_buf    = NULL;
	int *         vec         = NULL;
	void *        ret         = NULL;
	size_t        index       = 0;

	while (*topic_queue && node->child && *node->child) {
		index               = 0;
		dbtree_node *node_t = *node->child;
		debug_msg("topic is: %s, node->topic is: %s", *topic_queue,
		    node_t->topic);
		if (strcmp(node_t->topic, *topic_queue)) {
			bool equal = false;

			// TODO node_t or node->child[index], to determine if
			// node_t is needed
			node_t = find_next(node, &equal, topic_queue, &index);
			if (equal == false) {
				debug_msg("searching unequal");
				goto mem_free;
			}
		}

		if (node_t->child && *node_t->child && *(topic_queue + 1)) {
			topic_queue++;
			cvector_push_back(node_buf, node);
			cvector_push_back(vec, index);
			node = node_t;

		} else if (*(topic_queue + 1) == NULL) {
			debug_msg("Search and delete retain");
			debug_msg("node->topic: %s", node->topic);
			break;
		} else {
			debug_msg("No node and client need to be delete");
			goto mem_free;
		}
	}

	if (node->child) {
		ret = delete_dbtree_retain(node->child[index]);
		// print_client(node->child[index]->clients);
		delete_dbtree_node(node, index);
		// print_client(node->child[index]->clients);
	}

	// dbtree_print(dbtree);

	while (!cvector_empty(node_buf) && !cvector_empty(vec)) {
		dbtree_node *t = *(cvector_end(node_buf) - 1);
		int          i = *(cvector_end(vec) - 1);
		cvector_pop_back(node_buf);
		cvector_pop_back(vec);

		delete_dbtree_node(t, i);
		// dbtree_print(dbtree);
	}

	nni_rwlock_unlock(&(db->rwlock));

mem_free:
	cvector_free(node_buf);
	topic_queue_free(for_free);
	cvector_free(vec);

	return ret;
}

// TODO only use one of vector
static uint32_t *
iterate_shared_client(uint32_t **v)
{
	cvector(uint32_t) ids = NULL;

	if (v) {
		for (size_t i = 0; i < cvector_size(v); ++i) {
			// Dispatch strategy.
#ifdef RANDOM
			int index = rand();
#elif defined(ROUND_ROBIN)
			size_t index = acnt;
#endif
			// Get one pipe id from pipe id vector.
			// This vector is from a node.
			size_t j = index % cvector_size(v[i]);

			index = 0;
			// Deduplicate id.
			if (false ==
			    binary_search_uint32(
			        ids, 0, &index, v[i][j], ids_cmp)) {
				if (cvector_empty(ids) ||
				    (size_t) index == cvector_size(ids)) {
					cvector_push_back(ids, v[i][j]);
				} else {
					cvector_insert(ids, index, v[i][j]);
				}
			}
		}
		acnt++;
		if (acnt == INT_MAX) {
			acnt = 0;
		}
	}

	return ids;
}

uint32_t *
dbtree_find_shared_clients(dbtree *db, char *topic)
{
	cvector(uint32_t *) ids        = NULL;
	cvector(dbtree_node *) nodes_p = NULL;
	cvector(dbtree_node *) nodes_q = NULL;
	bool   equal                   = false;
	char * t                       = "$share";
	size_t index;

	nni_rwlock_rdlock(&(db->rwlock));
	dbtree_node *node = db->root;
	if (node == NULL) {
		nni_rwlock_unlock(&(db->rwlock));
		return NULL;
	}

	// Get shared node
	dbtree_node *shared = find_next(node, &equal, &t, &index);

	if (equal == false || shared == NULL || shared->child == NULL) {
		nni_rwlock_unlock(&(db->rwlock));
		return NULL;
	}

	dbtree_node **nlist = shared->child;

	char **topic_queue = topic_parse(topic);
	char **for_free    = topic_queue;

	// Push all shared child.
	for (size_t i = 0; i < cvector_size(nlist); i++) {
		dbtree_node *node = nlist[i];
		if (node->child && *node->child) {
			cvector_push_back(nodes_p, node);
		}
	}

	debug_msg("nodes size: %lu", cvector_size(nlist));
	while (*topic_queue && (!cvector_empty(nodes_p))) {

		ids = collect_clients(ids, nodes_p, &nodes_q, topic_queue);
		topic_queue++;
		if (*topic_queue == NULL) {
			break;
		}
		ids = collect_clients(ids, nodes_q, &nodes_p, topic_queue);
		topic_queue++;
	}

	uint32_t *ret = iterate_shared_client(ids);
	nni_rwlock_unlock(&(db->rwlock));
	topic_queue_free(for_free);
	cvector_free(nodes_p);
	cvector_free(nodes_q);
	cvector_free(ids);
	return ret;
}