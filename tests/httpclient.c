//
// Copyright 2021 Staysail Systems, Inc. <info@staysail.tech>
// Copyright 2018 Capitar IT Group BV <info@capitar.com>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

// Basic HTTP client tests.  Every exchange is served by a scripted server
// on the loopback interface, so that the result does not depend on the
// state of any public web server.

#ifndef _WIN32
#include <arpa/inet.h>
#endif

#include <nng/nng.h>
#include <nng/supplemental/http/http.h>
#include <nng/supplemental/tls/tls.h>
#include <nng/supplemental/util/platform.h>

#include "core/nng_impl.h"
#include "supplemental/http/http_api.h"

#include "convey.h"
#include "trantest.h"

static const char hello_body[]    = "hello";
static const char redirect_body[] = "redirect";

static const char *hello_reply[] = {
	"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n"
	"Content-Length: 5\r\n\r\nhello",
	NULL,
};

static const char *redirect_reply[] = {
	"HTTP/1.1 301 Moved Permanently\r\nLocation: /elsewhere\r\n"
	"Content-Type: text/plain\r\nContent-Length: 8\r\n\r\nredirect",
	NULL,
};

// The chunked transfer test serves its own response: our HTTP server always
// emits a Content-Length, and the public demo site this test used to fetch
// (anglesharp.azurewebsites.net) no longer resolves.  A raw TCP listener
// lets us hand the client a hand written chunked response instead.
static const char chunked_body[] = "chunked transfer works fine!!";

// The reply goes out in pieces, each one written separately with Nagle
// disabled and a short pause in between, so that they usually land in
// separate reads.  The split points are deliberately awkward -- inside a header
// value, between the CR and the LF that ends the headers, inside a chunk
// size line, inside chunk data, and inside the trailer -- so the parser has
// to carry state across reads the way it had to when this response still
// came off the wire from a remote server.  The sizes cover a single digit as
// well as lower and upper case hex, the first chunk carries a chunk
// extension, and a trailer field follows the last chunk;
// nni_http_chunks_parse() handles all of those.
static const char *chunked_reply[] = {
	"HTTP/1.1 200 OK\r\nContent-Type: text/pl",
	"ain\r\nTransfer-Encoding: chunked\r\n\r",
	"\n8;nng=1\r",
	"\nchunked ",
	"\r\na\r\ntransfer w\r\nB\r\norks fine!",
	"!\r\n0\r\nX-Chunk-Trailer: nng\r",
	"\n\r\n",
	NULL,
};

// The reply above is served over TCP, which does not preserve write
// boundaries, so that exchange cannot guarantee that
// nni_http_chunks_parse() ever had to resume mid token.  The streams below
// are handed to the parser directly instead, in every piece size from a
// single byte up to the whole stream, which does guarantee it.  Between
// them they cover single and multi digit sizes, both hex letter cases,
// chunk extensions, several trailer fields and no trailer at all.
#define CHUNK_A "The quick brown fox jumps over " // 31 == 0x1f
#define CHUNK_B "the lazy dog!!!"                 // 15 == 0xF
#define CHUNK_C " Pack my box XYZ"                // 16 == 0x10

static const char chunked_multi[] = "1f\r\n" CHUNK_A "\r\nF\r\n" CHUNK_B
                                    "\r\n10\r\n" CHUNK_C
                                    "\r\n0\r\nX-A: 1\r\nX-B: 2\r\n\r\n";
static const char chunked_multi_body[] = CHUNK_A CHUNK_B CHUNK_C;

static const char chunked_ext[]      = "1F;a=b;c\r\n" CHUNK_A "\r\n0\r\n\r\n";
static const char chunked_ext_body[] = CHUNK_A;

// Malformed streams; the parser has to reject every one of these.
static const char *chunked_bad[] = {
	"2\r\nabXX0\r\n\r\n",               // data not followed by CRLF
	"2\r\nab\r\nzz\r\n\r\n",            // bad hex digit in a size line
	"8;x\nchunked \r\n0\r\n\r\n",       // bare LF in a chunk extension
	"1\r\na\r\n0\r\nX-\x01: 1\r\n\r\n", // control byte in the trailer
	"8\rXchunked \r\n0\r\n\r\n",        // CR in a size line, no LF
	"\r\n0\r\n\r\n",                    // size line starts with CR
	";x=y\r\n0\r\n\r\n",                // size line starts with an extension
	NULL,
};

// Parses one stream in fixed size pieces, so that the parser has to resume
// wherever the piece boundaries fall.  Collects the chunk data into out.
// A parse that finishes early, or that stops making progress, is a failure.
static int
chunk_feed(const char *str, size_t n, size_t piece, char *out, size_t outsz,
    size_t *outlen)
{
	nni_http_chunks *cl;
	nni_http_chunk * ch  = NULL;
	size_t           i   = 0;
	size_t           off = 0;
	int              rv  = NNG_EAGAIN;

	*outlen = 0;
	if ((rv = nni_http_chunks_init(&cl, 0)) != 0) {
		return (rv);
	}
	while (i < n) {
		size_t len = 0;
		size_t k   = piece < (n - i) ? piece : (n - i);

		rv = nni_http_chunks_parse(cl, (void *) (str + i), k, &len);
		if ((rv != 0) && (rv != NNG_EAGAIN)) {
			nni_http_chunks_free(cl);
			return (rv);
		}
		if ((rv == NNG_EAGAIN) && (len == 0)) {
			nni_http_chunks_free(cl); // no forward progress
			return (NNG_ESTATE);
		}
		i += len;
		if (rv == 0) {
			break;
		}
	}
	// A finished parse has to land exactly on the end of the stream;
	// stopping short means the parser lost track of where it was.
	if ((rv == 0) && (i != n)) {
		nni_http_chunks_free(cl);
		return (NNG_ESTATE);
	}
	while ((rv == 0) &&
	    ((ch = nni_http_chunks_iter(cl, ch)) != NULL)) {
		if ((off + nni_http_chunk_size(ch)) > outsz) {
			nni_http_chunks_free(cl);
			return (NNG_EMSGSIZE);
		}
		memcpy(out + off, nni_http_chunk_data(ch),
		    nni_http_chunk_size(ch));
		off += nni_http_chunk_size(ch);
	}
	*outlen = off;
	nni_http_chunks_free(cl);
	return (rv);
}

// Runs one good stream through every piece size.  Returns the piece size
// that failed, or zero if all of them parsed to the expected body.
static size_t
chunk_check_splits(const char *wire, const char *body)
{
	char   out[128];
	size_t n = strlen(wire);
	size_t piece;

	for (piece = 1; piece <= n; piece++) {
		size_t len = 0;
		int    rv;

		rv = chunk_feed(wire, n, piece, out, sizeof(out), &len);
		if (rv != 0) {
			printf("piece %u: parse failed: %s\n",
			    (unsigned) piece, nng_strerror(rv));
			return (piece);
		}
		if ((len != strlen(body)) || (memcmp(out, body, len) != 0)) {
			printf("piece %u: body mismatch, got %u bytes\n",
			    (unsigned) piece, (unsigned) len);
			return (piece);
		}
	}
	return (0);
}

// Returns the piece size at which a malformed stream was wrongly accepted,
// or zero if it was rejected at every piece size.
static size_t
chunk_check_rejected(const char *wire)
{
	char   out[128];
	size_t n = strlen(wire);
	size_t piece;

	for (piece = 1; piece <= n; piece++) {
		size_t len = 0;

		if (chunk_feed(wire, n, piece, out, sizeof(out), &len) !=
		    NNG_EPROTO) {
			printf("piece %u: bad stream was not rejected\n",
			    (unsigned) piece);
			return (piece);
		}
	}
	return (0);
}

// Consumes a request, up to and including the end of its headers.  Every
// request of this test is a GET of the root that names the server as its
// host; anything else is a protocol error.
static int
raw_read_req(nng_stream *s, nng_aio *aio)
{
	char   buf[1024];
	size_t got = 0;

	while (got < sizeof(buf) - 1) {
		nng_iov iov;
		int     rv;

		iov.iov_buf = buf + got;
		iov.iov_len = sizeof(buf) - 1 - got;
		if ((rv = nng_aio_set_iov(aio, 1, &iov)) != 0) {
			return (rv);
		}
		nng_stream_recv(s, aio);
		nng_aio_wait(aio);
		if ((rv = nng_aio_result(aio)) != 0) {
			return (rv);
		}
		got += nng_aio_count(aio);
		buf[got] = '\0';
		if (strstr(buf, "\r\n\r\n") != NULL) {
			if ((strncmp(buf, "GET / HTTP/1.1\r\n", 16) != 0) ||
			    (strstr(buf, "\r\nHost: 127.0.0.1:") == NULL)) {
				return (NNG_EPROTO);
			}
			return (0);
		}
	}
	return (NNG_EMSGSIZE);
}

// Writes the whole buffer, tolerating short sends.
static int
raw_write_all(nng_stream *s, nng_aio *aio, const char *data, size_t len)
{
	while (len > 0) {
		nng_iov iov;
		int     rv;

		iov.iov_buf = (void *) data;
		iov.iov_len = len;
		if ((rv = nng_aio_set_iov(aio, 1, &iov)) != 0) {
			return (rv);
		}
		nng_stream_send(s, aio);
		nng_aio_wait(aio);
		if ((rv = nng_aio_result(aio)) != 0) {
			return (rv);
		}
		data += nng_aio_count(aio);
		len -= nng_aio_count(aio);
	}
	return (0);
}


// One script per test; each entry answers one request.
static const char **hello_script[]   = { hello_reply, NULL };
static const char **reuse_script[]   = { hello_reply, redirect_reply, NULL };
static const char **chunked_script[] = { chunked_reply, NULL };
static const char **silent_script[]  = { NULL };

// A scripted HTTP server.  It accepts a single connection and answers each
// request on it with the next reply of its script.  Once the script is used
// up it keeps the connection open without answering, until the client
// closes it.  A request it does not expect makes it close the connection.
struct scripted_server {
	nng_stream_listener *l;
	nng_thread          *thr;
	const char        ***script;
	char                 url[64];
};

/** Serves the script of a scripted server on its own thread. */
static void
scripted_server_main(void *arg)
{
	struct scripted_server *srv = arg;
	nng_aio                *aio;
	nng_stream             *s;
	int                     rv = 0;

	if (nng_aio_alloc(&aio, NULL, NULL) != 0) {
		return;
	}
	nng_aio_set_timeout(aio, 10000); // 10 sec timeout
	nng_stream_listener_accept(srv->l, aio);
	nng_aio_wait(aio);
	if (nng_aio_result(aio) != 0) {
		nng_aio_free(aio);
		return;
	}
	s = nng_aio_get_output(aio, 0);

	for (int i = 0; (rv == 0) && (srv->script[i] != NULL); i++) {
		const char **reply = srv->script[i];

		rv = raw_read_req(s, aio);
		for (int j = 0; (rv == 0) && (reply[j] != NULL); j++) {
			if (j > 0) {
				nng_msleep(5);
			}
			rv = raw_write_all(s, aio, reply[j], strlen(reply[j]));
		}
	}
	while (rv == 0) {
		rv = raw_read_req(s, aio);
	}

	nng_stream_close(s);
	nng_stream_free(s);
	nng_aio_free(aio);
}

/** Stops a scripted server; the client connection must be closed. */
static void
scripted_server_stop(struct scripted_server *srv)
{
	if (srv->l != NULL) {
		nng_stream_listener_close(srv->l);
	}
	if (srv->thr != NULL) {
		nng_thread_destroy(srv->thr);
	}
	if (srv->l != NULL) {
		nng_stream_listener_free(srv->l);
	}
}

/** Starts a scripted server on an ephemeral loopback port. */
static int
scripted_server_start(struct scripted_server *srv, const char ***script)
{
	int rv;
	int port;

	memset(srv, 0, sizeof(*srv));
	srv->script = script;

	if (((rv = nng_stream_listener_alloc(&srv->l, "tcp://127.0.0.1:0")) !=
	        0) ||
	    ((rv = nng_stream_listener_set_bool(
	          srv->l, NNG_OPT_TCP_NODELAY, true)) != 0) ||
	    ((rv = nng_stream_listener_listen(srv->l)) != 0) ||
	    ((rv = nng_stream_listener_get_int(
	          srv->l, NNG_OPT_TCP_BOUND_PORT, &port)) != 0)) {
		scripted_server_stop(srv);
		return (rv);
	}
	snprintf(srv->url, sizeof(srv->url), "http://127.0.0.1:%d/", port);
	rv = nng_thread_create(&srv->thr, scripted_server_main, srv);
	if (rv != 0) {
		scripted_server_stop(srv);
	}
	return (rv);
}

/** Checks HTTP client requests, response bodies and connection reuse. */
TestMain("HTTP Client", {
	// Reset runs after its Convey block has been left.
	struct scripted_server srv;
	nng_aio               *aio;
	nng_http_client       *cli;
	nng_http_conn         *http;
	nng_url               *url;
	nng_http_req          *req;
	nng_http_res          *res;

	Convey("Given a TCP connection to a local server", {
		So(scripted_server_start(&srv, hello_script) == 0);
		// The server thread must not outlive a failed assertion in
		// the setup below; the full Reset replaces this one.
		Reset({ scripted_server_stop(&srv); });
		So(nng_aio_alloc(&aio, NULL, NULL) == 0);

		So(nng_url_parse(&url, srv.url) == 0);

		nng_aio_set_timeout(aio, 10000);
		So(nng_http_client_alloc(&cli, url) == 0);
		nng_http_client_connect(cli, aio);
		nng_aio_wait(aio);
		So(nng_aio_result(aio) == 0);
		http = nng_aio_get_output(aio, 0);
		Reset({
			nng_http_client_free(cli);
			nng_http_conn_close(http);
			nng_aio_free(aio);
			nng_url_free(url);
			scripted_server_stop(&srv);
		});

		Convey("We can initiate a message", {
			void  *data;
			size_t sz;

			So(http != NULL);

			So(nng_http_req_alloc(&req, url) == 0);
			So(nng_http_res_alloc(&res) == 0);
			Reset({
				nng_http_req_free(req);
				nng_http_res_free(res);
			});
			nng_http_conn_write_req(http, req, aio);

			nng_aio_wait(aio);
			So(nng_aio_result(aio) == 0);
			nng_http_conn_read_res(http, res, aio);
			nng_aio_wait(aio);
			So(nng_aio_result(aio) == 0);
			So(nng_http_res_get_status(res) == 200);

			Convey("The message contents are correct", {
				const char *cstr;
				nng_iov     iov;

				cstr = nng_http_res_get_header(
				    res, "Content-Length");
				So(cstr != NULL);
				sz = atoi(cstr);
				So(sz == strlen(hello_body));

				data = nng_alloc(sz);
				So(data != NULL);
				Reset({ nng_free(data, sz); });

				iov.iov_buf = data;
				iov.iov_len = sz;
				So(nng_aio_set_iov(aio, 1, &iov) == 0);

				nng_http_conn_read_all(http, aio);
				nng_aio_wait(aio);
				So(nng_aio_result(aio) == 0);
				So(nng_aio_count(aio) == sz);
				So(memcmp(data, hello_body, sz) == 0);
			});
		});
	});

	Convey("Given a client", {
		nng_http_res  *res1;
		nng_http_res  *res2;
		nng_http_conn *conn;

		So(scripted_server_start(&srv, reuse_script) == 0);
		// The server thread must not outlive a failed assertion in
		// the setup below; the full Reset replaces this one.
		Reset({ scripted_server_stop(&srv); });
		So(nng_aio_alloc(&aio, NULL, NULL) == 0);

		So(nng_url_parse(&url, srv.url) == 0);

		So(nng_http_client_alloc(&cli, url) == 0);
		nng_aio_set_timeout(aio, 10000); // 10 sec timeout

		Reset({
			nng_http_client_free(cli);
			nng_url_free(url);
			nng_aio_free(aio);
			scripted_server_stop(&srv);
		});

		Convey("One off exchange works", {
			void  *data;
			size_t len;

			So(nng_http_req_alloc(&req, url) == 0);
			So(nng_http_res_alloc(&res) == 0);
			Reset({
				nng_http_req_free(req);
				nng_http_res_free(res);
			});

			nng_http_client_transact(cli, req, res, aio);
			nng_aio_wait(aio);
			So(nng_aio_result(aio) == 0);
			So(nng_http_res_get_status(res) == 200);
			nng_http_res_get_data(res, &data, &len);
			So(len == strlen(hello_body));
			So(memcmp(data, hello_body, len) == 0);
		});

		// The server accepts a single connection, so the second
		// exchange only succeeds if the client reuses it.
		Convey("Connection reuse works", {
			void       *data;
			size_t      len;
			const char *cstr;

			conn = NULL;

			So(nng_http_req_alloc(&req, url) == 0);
			So(nng_http_res_alloc(&res1) == 0);
			So(nng_http_res_alloc(&res2) == 0);
			Reset({
				nng_http_req_free(req);
				nng_http_res_free(res1);
				nng_http_res_free(res2);
				if (conn != NULL) {
					nng_http_conn_close(conn);
				}
			});

			nng_http_client_connect(cli, aio);
			nng_aio_wait(aio);
			So(nng_aio_result(aio) == 0);
			conn = nng_aio_get_output(aio, 0);

			nng_http_conn_transact(conn, req, res1, aio);
			nng_aio_wait(aio);
			So(nng_aio_result(aio) == 0);
			So(nng_http_res_get_status(res1) == 200);
			nng_http_res_get_data(res1, &data, &len);
			So(len == strlen(hello_body));
			So(memcmp(data, hello_body, len) == 0);

			nng_http_conn_transact(conn, req, res2, aio);
			nng_aio_wait(aio);
			So(nng_aio_result(aio) == 0);
			So(nng_http_res_get_status(res2) == 301);
			cstr = nng_http_res_get_header(res2, "Location");
			So(cstr != NULL);
			So(strcmp(cstr, "/elsewhere") == 0);
			nng_http_res_get_data(res2, &data, &len);
			So(len == strlen(redirect_body));
			So(memcmp(data, redirect_body, len) == 0);
		});
	});

	Convey("Client times out", {
		So(scripted_server_start(&srv, silent_script) == 0);
		// The server thread must not outlive a failed assertion in
		// the setup below; the full Reset replaces this one.
		Reset({ scripted_server_stop(&srv); });
		So(nng_aio_alloc(&aio, NULL, NULL) == 0);

		So(nng_url_parse(&url, srv.url) == 0);

		So(nng_http_client_alloc(&cli, url) == 0);
		So(nng_http_req_alloc(&req, url) == 0);
		So(nng_http_res_alloc(&res) == 0);

		Reset({
			nng_http_client_free(cli);
			nng_url_free(url);
			nng_aio_free(aio);
			nng_http_req_free(req);
			nng_http_res_free(res);
			scripted_server_stop(&srv);
		});
		nng_aio_set_timeout(aio, 100); // 100 msec timeout

		nng_http_client_transact(cli, req, res, aio);
		nng_aio_wait(aio);
		So(nng_aio_result(aio) == NNG_ETIMEDOUT);
	});

	Convey("Given a client (chunked)", {
		So(scripted_server_start(&srv, chunked_script) == 0);
		// The server thread must not outlive a failed assertion in
		// the setup below; the full Reset replaces this one.
		Reset({ scripted_server_stop(&srv); });
		So(nng_aio_alloc(&aio, NULL, NULL) == 0);

		So(nng_url_parse(&url, srv.url) == 0);

		So(nng_http_client_alloc(&cli, url) == 0);
		nng_aio_set_timeout(aio, 10000); // 10 sec timeout

		Reset({
			nng_http_client_free(cli);
			nng_url_free(url);
			nng_aio_free(aio);
			scripted_server_stop(&srv);
		});

		Convey("One off exchange works", {
			const char *cstr;
			void       *data;
			size_t      len;

			So(nng_http_req_alloc(&req, url) == 0);
			So(nng_http_res_alloc(&res) == 0);
			Reset({
				nng_http_req_free(req);
				nng_http_res_free(res);
			});

			nng_http_client_transact(cli, req, res, aio);
			nng_aio_wait(aio);
			So(nng_aio_result(aio) == 0);
			So(nng_http_res_get_status(res) == 200);
			cstr = nng_http_res_get_header(res, "Content-Type");
			So(cstr != NULL);
			So(strcmp(cstr, "text/plain") == 0);
			nng_http_res_get_data(res, &data, &len);
			So(len == strlen(chunked_body));
			So(memcmp(data, chunked_body, len) == 0);
		});
	});

	Convey("Chunk parser resumes at every boundary", {
		char        wire[256];
		const char *chunks;
		int         i;

		wire[0] = '\0';
		for (i = 0; chunked_reply[i] != NULL; i++) {
			So(strlen(wire) + strlen(chunked_reply[i]) <
			    sizeof(wire));
			strcat(wire, chunked_reply[i]);
		}
		So((chunks = strstr(wire, "\r\n\r\n")) != NULL);
		chunks += 4; // skip the status line and headers

		So(chunk_check_splits(chunks, chunked_body) == 0);
		So(chunk_check_splits(chunked_multi, chunked_multi_body) == 0);
		So(chunk_check_splits(chunked_ext, chunked_ext_body) == 0);

		for (i = 0; chunked_bad[i] != NULL; i++) {
			So(chunk_check_rejected(chunked_bad[i]) == 0);
		}
	});
})
