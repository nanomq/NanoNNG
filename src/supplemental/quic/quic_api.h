#ifndef NNG_SUPP_QUIC_API_H
#define NNG_SUPP_QUIC_API_H

#define QUIC_HIGH_PRIOR_MSG (0x01)

#define QUIC_MAIN_STREAM (1)
#define QUIC_SUB_STREAM (0)

#define QUIC_IDLE_TIMEOUT_DEFAULT (90)
#define QUIC_KEEPALIVE_DEFAULT (60)

#include "core/nng_impl.h"
#include "nng/nng.h"
#include "msquic.h"

typedef struct quic_dialer quic_dialer;

extern int nni_quic_listener_alloc(nng_stream_listener **, const nni_url *);
extern int nni_quic_dialer_alloc(nng_stream_dialer **, const nni_url *);

typedef struct nni_quic_dialer nni_quic_dialer;

extern int  nni_quic_dialer_init(void **);
extern void nni_quic_dialer_fini(nni_quic_dialer *d);
extern void nni_quic_dial(void *, const char *, const char *, nni_aio *);
extern void nni_quic_dialer_close(void *);

typedef struct nni_quic_conn nni_quic_conn;

extern int  nni_msquic_quic_alloc(nni_quic_conn **, nni_quic_dialer *);
extern void nni_msquic_quic_init(nni_quic_conn *);
extern void nni_msquic_quic_start(nni_quic_conn *, int, int);
extern void nni_msquic_quic_dialer_rele(nni_quic_dialer *);

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


/*
 * Note.
 *
 * qsock is the handle of a quic connection.
 * Which can NOT be used to write or read.
 *
 * qpipe is the handle of a quic stream.
 * All qpipes should be were closed before disconnecting qsock.
 */

// Enable MsQuic
extern void quic_open();
// Disable MsQuic and free
extern void quic_close();

// Enable quic protocol for nng
extern void quic_proto_open(nni_proto *proto);
// Disable quic protocol for nng
extern void quic_proto_close();
// Set global configuration for quic protocol
extern void quic_proto_set_bridge_conf(void *arg);

// Establish a quic connection to target url. Return 0 if success.
// And the handle of connection(qsock) would pass to callback .pipe_init(,qsock,)
// Or the connection is failed in eastablishing.
extern int quic_connect_ipv4(const char *url, nni_sock *sock, uint32_t *index, void **qsockp);
// Close connection
extern int quic_disconnect(void *qsock, void *qpipe);
// set close flag of qsock to true
extern void quic_sock_close(void *qsock);
// Create a qpipe and open it
extern int quic_pipe_open(void *qsock, void **qpipe, void *mqtt_pipe);
// get disconnect reason code from QUIC transport
extern uint8_t quic_sock_disconnect_code(void *arg);
// Receive msg from a qpipe
extern int quic_pipe_recv(void *qpipe, nni_aio *raio);
// Send msg to a qpipe
extern int quic_pipe_send(void *qpipe, nni_aio *saio);
extern int quic_aio_send(void *arg, nni_aio *aio);
// Close a qpipe and free it
extern int quic_pipe_close(void *qpipe, uint8_t *code);

#endif
