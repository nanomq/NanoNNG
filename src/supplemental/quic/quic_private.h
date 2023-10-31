#ifndef NNG_SUPP_QUIC_PRIVATE_H
#define NNG_SUPP_QUIC_PRIVATE_H

#include "msquic.h"
#include "core/nng_impl.h"
#include "nng/nng.h"

// Config for msquic
static const QUIC_REGISTRATION_CONFIG quic_reg_config = {
	"mqtt",
	QUIC_EXECUTION_PROFILE_LOW_LATENCY
};

static const QUIC_BUFFER quic_alpn = {
	sizeof("mqtt") - 1,
	(uint8_t *) "mqtt"
};

static const QUIC_API_TABLE *MsQuic = NULL;

static HQUIC registration;
static HQUIC configuration;

int  msquic_open();
void msquic_close();

typedef struct nni_quic_dialer nni_quic_dialer;

int  nni_quic_dialer_init(void **);
void nni_quic_dialer_fini(nni_quic_dialer *d);
void nni_quic_dial(void *, const char *, const char *, nni_aio *);
void nni_quic_dialer_close(void *);

typedef struct nni_quic_listener nni_quic_listener;

int  nni_quic_listener_init(void **);
void nni_quic_listener_listen(nni_quic_listener *, const char *, const char *);
void nni_quic_listener_accept(nni_quic_listener *, nng_aio *aio);

typedef struct nni_quic_conn nni_quic_conn;

// Might no different TODO
int  nni_msquic_quic_dialer_conn_alloc(nni_quic_conn **, nni_quic_dialer *);
int  nni_msquic_quic_listener_conn_alloc(nni_quic_conn **, nni_quic_listener *);
void nni_msquic_quic_dialer_rele(nni_quic_dialer *);


// MsQuic bindings

void msquic_conn_close(HQUIC qconn, int rv);
void msquic_conn_fini(HQUIC qconn);

void msquic_strm_close(HQUIC qstrm);
void msquic_strm_fini(HQUIC qstrm);
void msquic_strm_recv_start(HQUIC qstrm);


struct nni_quic_dialer {
	nni_aio                *qconaio; // for quic connection
	nni_quic_conn          *currcon;
	nni_list                connq;   // pending connections/quic streams
	bool                    closed;
	bool                    nodelay;
	bool                    keepalive;
	struct sockaddr_storage src;
	size_t                  srclen;
	nni_mtx                 mtx;
	nni_atomic_u64          ref;
	nni_atomic_bool         fini;

	// MsQuic
	HQUIC    qconn; // quic connection
	bool     enable_0rtt;
	bool     enable_mltstrm;
	uint8_t  reason_code;
	// ResumptionTicket
	char      rticket[4096]; // Ususally it would be within 4096.
	                         // But in msquic. The maximum size is 65535.
	uint16_t  rticket_sz;
	// CertificateFile
	char *    cacert;
	char *    key;
	char *    password;
	bool      verify_peer;
	char *    ca;

	// Quic settings
	uint64_t  qidle_timeout;
	uint32_t  qkeepalive;
	uint64_t  qconnect_timeout;
	uint32_t  qdiscon_timeout;
	uint32_t  qsend_idle_timeout;
	uint32_t  qinitial_rtt_ms;
	uint32_t  qmax_ack_delay_ms;

	QUIC_SETTINGS settings;
};

struct nni_quic_listener {
	nni_mtx                 mtx;
	nni_atomic_u64          ref;
	nni_atomic_bool         fini;
	bool                    closed;
	bool                    started;
	nni_list                acceptq;
	nni_list                incomings;

	// MsQuic
	HQUIC    ql; // Quic Listener

	// Quic Settings
	bool     enable_0rtt;
	bool     enable_mltstrm;

	QUIC_SETTINGS settings;
};

struct nni_quic_conn {
	nng_stream      stream;
	nni_list        readq;
	nni_list        writeq;
	bool            closed;
	nni_mtx         mtx;
	nni_aio *       dial_aio;
	// nni_aio *       qstrmaio; // Link to msquic_strm_cb
	nni_quic_dialer *dialer;

	// MsQuic
	HQUIC           qstrm; // quic stream
	uint8_t         reason_code;

	nni_reap_node   reap;
};

#endif
