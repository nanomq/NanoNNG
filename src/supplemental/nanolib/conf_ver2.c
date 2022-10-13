#include "nng/supplemental/nanolib/conf.h"
#include "core/nng_impl.h"
#include "nng/nng.h"
#include "nng/supplemental/nanolib/cJSON.h"
#include "nng/supplemental/nanolib/cvector.h"
#include "nng/supplemental/nanolib/file.h"
#include "nng/supplemental/nanolib/log.h"
#include "nng/supplemental/nanolib/hocon.h"
#include <ctype.h>
#include <string.h>

// Read json value into struct
// use same struct fields and json keys
#define hocon_read_str_base(structure, field, key, jso)          \
	do {                                                     \
		cJSON *jso_key = cJSON_GetObjectItem(jso, key);  \
		if (NULL == jso_key) {                           \
			log_error("Read config %s failed!", key); \
			break;                                   \
		}                                                \
		switch (jso_key->type) {                         \
		case cJSON_String:                               \
			(structure)->field =                     \
			    nng_strdup(jso_key->valuestring);    \
			break;                                   \
		default:                                         \
			break;                                   \
		}                                                \
	} while (0);

#define hocon_read_bool_base(structure, field, key, jso)            \
	do {                                                        \
		cJSON *jso_key = cJSON_GetObjectItem(jso, key);     \
		if (NULL == jso_key) {                              \
			log_error("Read config %s failed!", key); \
			break;                                      \
		}                                                   \
		switch (jso_key->type) {                            \
		case cJSON_True:                                    \
			(structure)->field = cJSON_IsTrue(jso_key); \
			break;                                      \
		default:                                            \
			break;                                      \
		}                                                   \
	} while (0);

#define hocon_read_num_base(structure, field, key, jso)             \
	do {                                                        \
		cJSON *jso_key = cJSON_GetObjectItem(jso, key);     \
		if (NULL == jso_key) {                              \
			log_error("Read config %s failed!", key); \
			break;                                      \
		}                                                   \
		switch (jso_key->type) {                            \
		case cJSON_Number:                                  \
			(structure)->field = jso_key->valueint;     \
			break;                                      \
		default:                                            \
			break;                                      \
		}                                                   \
	} while (0);

#define hocon_read_str_arr_base(structure, field, key, jso)              \
	do {                                                                 \
		cJSON *jso_arr = cJSON_GetObjectItem(jso, key);           \
		if (NULL == jso_arr) {                                       \
			log_error("Read config %s failed!", key); \
			break;                                              	\
		}                                                            \
		cJSON *elem = NULL;                                          \
		cJSON_ArrayForEach(elem, jso_arr)                            \
		{                                                            \
			switch (elem->type) {                                \
			case cJSON_String:                                   \
				cvector_push_back((structure)->field,             \
				    nng_strdup(cJSON_GetStringValue(elem))); \
				break;                                       \
			default:                                             \
				break;                                       \
			}                                                    \
		}                                                            \
	} while (0);

// Add easier interface
// if struct fields and json key 
// are the same use this interface
#define hocon_read_str(structure, key, jso) hocon_read_str_base(structure, key, #key, jso)
#define hocon_read_num(structure, key, jso) hocon_read_num_base(structure, key, #key, jso)
#define hocon_read_bool(structure, key, jso) hocon_read_bool_base(structure, key, #key, jso)
#define hocon_read_str_arr(structure, key, jso) hocon_read_str_arr_base(structure, key, #key, jso)

static char **string_split(char *str, char sp)
{
	char **ret = NULL;
	char *p = str;
	char *p_b = p;
	while (NULL != (p = strchr(p, sp))) {
		*p++ = '\0';
		cvector_push_back(ret, p_b);
	}
	cvector_push_back(ret, p_b);
	return ret;
}

cJSON *hocon_get_obj(char *key, cJSON *jso)
{
	cJSON *ret = jso;
	char **str_vec = string_split(key, '.');
	
	for (size_t i = 0; cvector_size(str_vec); i++) {
		ret = cJSON_GetObjectItem(ret, str_vec[i]);
	}

	return ret;
}