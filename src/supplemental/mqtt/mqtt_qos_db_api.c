#include "mqtt_qos_db_api.h"

void
nni_db_init(nni_db_ops *db_ops)
{
	db_ops->db_init(&db_ops->db);
}

void
nni_db_fini(nni_db_ops *db_ops)
{
	db_ops->db_fini(db_ops->db);
}

void
nni_db_set(nni_db_ops *db_ops, uint32_t pipe_id, void *msg)
{
	db_ops->db_set(db_ops->db, pipe_id, msg);
}

void *
nni_db_get(nni_db_ops *db_ops, uint32_t pipe_id)
{
	return db_ops->db_get(db_ops->db, pipe_id);
}

void
nni_db_remove(nni_db_ops *db_ops, uint32_t pipe_id)
{
	db_ops->db_remove(db_ops->db, pipe_id);
}

void
nni_db_check_remove_msg(nni_db_ops *db_ops, void *msg)
{
	db_ops->db_check_remove_msg(db_ops->db, msg);
}
