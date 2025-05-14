#include <stdio.h>
#include <string.h>

#include "nng/protocol/mqtt/mqtt_parser.h"
#include "nng/supplemental/nanolib/retains.h"
#include "nng/supplemental/nanolib/cJSON.h"
#include "nng/supplemental/nanolib/conf.h"
#include "nng/supplemental/util/idhash.h"
#include "nng/nng.h"
#include "core/nng_impl.h"
#include "core/sockimpl.h"

static retains_db_item *
new_item(const char *topic, const char *clientid, nng_msg *msg)
{
	retains_db_item *item = nng_alloc(sizeof(retains_db_item));
	item->topic = strdup(topic);
	item->clientid = strdup(clientid);
	nng_msg_clone(msg);
	item->msg = msg;
	return item;
}

static void
free_item(retains_db_item *item)
{
	nng_free(item->clientid, 0);
	nng_free(item->topic, 0);
	nng_msg_free(item->msg);
	nng_free(item, 0);
}

int
retains_db_add_item(nng_id_map *map, const char *topic, const char *clientid, nng_msg *msg)
{
	uint32_t id = DJBHashn((char *)topic, strlen(topic));
	log_debug("in: %d->%s,%s,%p", id, topic, clientid, msg);
	retains_db_item *old;
	retains_db_item *item = new_item(topic, clientid, msg);
	if (NULL != (old = nng_id_get(map, id))) {
		free_item(old);
	}
	return nng_id_set(map, id, item);
}

void
retains_db_rm_item(nng_id_map *map, const char *topic)
{
	log_debug("out: %s", topic);
	uint32_t id = DJBHashn((char *)topic, strlen(topic));
	retains_db_item *old;
	if (NULL != (old = nng_id_get(map, id))) {
		free_item(old);
	}
	nng_id_remove(map, id);
}

static inline void
iter_retains_db(void *k, void *v, void *arg)
{
	log_debug("iter: %d->%p", *(uint32_t *)k, v);
	if (!v)
		return;
	(void) k;

	retains_db_item *item = v;
	cJSON *retainjson = cJSON_CreateObject();
	cJSON_AddStringToObject(retainjson, "topic", item->topic);
	cJSON_AddStringToObject(retainjson, "clientid", item->clientid);

	cJSON_AddItemToArray(arg, retainjson);
}

char *
retains_json_all_items(nng_id_map *map)
{
	cJSON *resjson = cJSON_CreateObject();
	cJSON *arrjson = cJSON_CreateArray();
	nng_id_map_foreach2(map, iter_retains_db, arrjson);
	cJSON_AddItemToObject(resjson, "retains", arrjson);
	char *res = cJSON_PrintUnformatted(resjson);
	cJSON_Delete(resjson);
	return res;
}

