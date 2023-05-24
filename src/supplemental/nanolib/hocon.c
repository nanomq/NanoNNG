#include "nng/supplemental/nanolib/cvector.h"
#include "nng/supplemental/nanolib/hocon.h"
#include "nng/supplemental/nanolib/parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern FILE *yyin;

static cJSON *
path_expression_parse_core(cJSON *parent, cJSON *jso)
{
	jso       = cJSON_DetachItemFromObject(parent, jso->string);
	char *str = strdup(jso->string);
	char *p   = str;
	char *p_a = str + strlen(str);

	char t[128] = { 0 };
	while (NULL != (p = strrchr(str, '.'))) {
		cJSON *jso_new = cJSON_CreateObject();
		// cJSON *jso_new = NULL;

		// a.b.c: {object}
		// c ==> create json object jso(c, jso)
		*p = '_';
		strncpy(t, p + 1, p_a - p);
		// cJSON_AddItemToObject(jso_new, t, jso);
		cJSON_AddItemToObject(
		    jso_new, t, jso); // cJSON_Duplicate(jso, cJSON_True));
		memset(t, 0, 128);
		// jso_new = json(c, jso)
		// cJSON_Delete(jso);
		jso     = jso_new;
		jso_new = NULL;
		p_a     = --p;
	}

	strncpy(t, str, p_a - str + 1);
	cJSON_AddItemToObject(parent, t, jso);
	// memset(t, 0, 128);
	// cJSON_DeleteItemFromObject(parent, str);

	free(str);
	return parent;
}

// {"bridge.sqlite":{"enable":false,"disk_cache_size":102400,"mounted_file_path":"/tmp/","flush_mem_threshold":100,"resend_interval":5000}}
// {"bridge":{"sqlite":{"enable":false,"disk_cache_size":102400,"mounted_file_path":"/tmp/","flush_mem_threshold":100,"resend_interval":5000}}}

// level-order traversal
// find key bridge.sqlite
// create object sqlite with object value
// insert object bridge with sqlite
// delete bridge.sqlite

static cJSON *
path_expression_parse(cJSON *jso)
{
	cJSON *parent = jso;
	cJSON *child  = NULL;
	if (NULL != jso)
		child = jso->child;

	while (child) {
		if (child->child) {
			path_expression_parse(child);
		}
		if (NULL != child->string &&
		    NULL != strchr(child->string, '.')) {
			path_expression_parse_core(parent, child);
			child = parent->child;
		} else {
			child = child->next;
		}
	}

	return jso;
}

// if same level has same name, if they are not object
// the back covers the front
// TODO FIXME memory leak
cJSON *
deduplication_and_merging(cJSON *jso)
{
	cJSON *parent = jso;
	cJSON *child  = NULL;
	if (NULL != jso)
		child = jso->child;
	cJSON **table = NULL;

	while (child) {
		for (size_t i = 0; i < cvector_size(table); i++) {

			// If we find duplicate, compare json key
			if (table[i] && child && table[i]->string &&
			    child->string &&
			    0 == strcmp(table[i]->string, child->string)) {
				if (table[i]->type == child->type &&
				    cJSON_Object == table[i]->type) {
					// merging object, move all child
					// of table[i] to current (child) node
					cJSON *next = table[i]->child;
					while (next) {
						cJSON *dup = cJSON_Duplicate(
						    next, cJSON_True);
						cJSON_AddItemToObject(
						    child, dup->string, dup);
						next = next->next;
					}
					cJSON_DeleteItemFromObject(
					    parent, table[i]->string);
					cvector_erase(table, i);
				} else {
					// The rule of No object merge is
					// depending on the last value, thus we
					// delete node from table if we find
					// dup node
					if (0 == i) {
						// If first child is duplicate,
						// free it and move child to
						// child next
						parent->child->next->prev =
						    parent->child->prev;
						parent->child =
						    parent->child->next;
						cJSON_free(table[i]);
						cvector_erase(table, i);

					} else {
						table[i]->next->prev =
						    table[i]->prev;
						table[i - 1]->next =
						    table[i]->next;
						cJSON_free(table[i]);
						cvector_erase(table, i);
					}
				}
			}
		}

		cvector_push_back(table, child);

		if (child->child) {
			deduplication_and_merging(child);
		}
		child = child->next;
	}
	cvector_free(table);
	return jso;
}

cJSON *
hocon_parse_file(const char *file)
{
	// yydebug = 1;
	if (!(yyin = fopen(file, "r"))) {
		perror((file));
		return NULL;
	}

	cJSON *jso = NULL;
	int    rv  = yyparse(&jso);
	if (0 != rv) {
		fprintf(stderr, "invalid data to parse!\n");
		return NULL;
	}
	if (cJSON_False != cJSON_IsInvalid(jso)) {
		jso = path_expression_parse(jso);
		return deduplication_and_merging(jso);
	}
	return NULL;
}

typedef struct yy_buffer_state *YY_BUFFER_STATE;
extern YY_BUFFER_STATE          yy_scan_bytes(char *buffer, size_t size);
extern void yy_delete_buffer(struct yy_buffer_state *buffer);

cJSON *
hocon_parse_str(char *str, size_t len)
{
	YY_BUFFER_STATE buffer = yy_scan_bytes(str, len);

	cJSON *jso = NULL;
	int    rv  = yyparse(&jso);
	if (0 != rv) {
		fprintf(stderr, "invalid data to parse!\n");
		return NULL;
	}
	yy_delete_buffer(buffer);
	if (cJSON_False != cJSON_IsInvalid(jso)) {
		jso = path_expression_parse(jso);
		return deduplication_and_merging(jso);
	}
	return NULL;
}