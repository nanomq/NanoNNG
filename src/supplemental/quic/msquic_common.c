#include "quic_private.h"
#include "core/nng_impl.h"

static const QUIC_API_TABLE *MsQuic = NULL;

/***************************** MsQuic Bindings *****************************/

void
msquic_set_api_table(const QUIC_API_TABLE *table)
{
	MsQuic = table;
}

void
msquic_conn_close(HQUIC qconn, int rv)
{
	MsQuic->ConnectionShutdown(qconn, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, (QUIC_UINT62)rv);
}

void
msquic_conn_fini(HQUIC qconn)
{
	MsQuic->ConnectionClose(qconn);
}

void
msquic_strm_close(HQUIC qstrm)
{
	log_info("stream %p shutdown", qstrm);
	MsQuic->StreamShutdown(
	    qstrm, QUIC_STREAM_SHUTDOWN_FLAG_ABORT | QUIC_STREAM_SHUTDOWN_FLAG_IMMEDIATE, NNG_ECONNSHUT);
}

void
msquic_strm_fini(HQUIC qstrm)
{
	log_info("stream %p fini", qstrm);
	MsQuic->StreamClose(qstrm);
}

void
msquic_strm_recv_start(HQUIC qstrm)
{
	MsQuic->StreamReceiveSetEnabled(qstrm, TRUE);
}

