#ifndef NANOLIBS_RETAINS_H
#define NANOLIBS_RETAINS_H

#include "nng/nng.h"
#include "nng/supplemental/util/idhash.h"

struct retains_db_item {
	char    *topic;
	uint8_t  qos;
	char    *clientid;
	nng_time ts;
	uint8_t *bin;
	uint32_t binsz;
	nng_msg *msg;
};
typedef struct retains_db_item retains_db_item;

int    retains_db_add_item(nng_id_map *map, const char *topic, const char *clientid, nng_msg *msg);
void   retains_db_rm_item(nng_id_map *map, const char *topic);
char * retains_json_all_items(nng_id_map *map);

#endif
