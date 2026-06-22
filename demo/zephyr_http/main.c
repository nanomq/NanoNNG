/*
 * NanoNNG Zephyr HTTP Test Demo
 *
 * Self-contained test: starts an HTTP server, then uses an HTTP client
 * to verify GET/POST/404 responses over 127.0.0.1 loopback.
 *
 * Build: west build -b qemu_x86 .
 * Run:   west build -t run
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <nng/nng.h>
#include <nng/supplemental/http/http.h>
#include <nng/supplemental/util/platform.h>

/*
 * Server binds to all interfaces. Client connect URL is board-specific:
 *   qemu_x86  → 10.0.2.15:18888 (SLIRP guest IP)
 *   native_sim → 127.0.0.1:18888 (host loopback)
 */
#ifdef CONFIG_BOARD_QEMU_X86
#define SERVER_URL  "http://0.0.0.0:18888"
#define CLIENT_URL  "http://10.0.2.15:18888"
#else
#define SERVER_URL  "http://127.0.0.1:18888"
#define CLIENT_URL  "http://127.0.0.1:18888"
#endif

#define BUFSIZE     4096

static int pass_cnt, fail_cnt;

static void
fatal(const char *msg, int rv)
{
	printk("FATAL: %s: %s\n", msg, nng_strerror(rv));
	while (1) k_sleep(K_SECONDS(1));
}

static void
test_pass(const char *msg)
{
	printk("  PASS: %s\n", msg);
	pass_cnt++;
}

static void
test_fail(const char *msg, int rv)
{
	if (rv != 0)
		printk("  FAIL: %s (%s)\n", msg, nng_strerror(rv));
	else
		printk("  FAIL: %s\n", msg);
	fail_cnt++;
}

/* ── HTTP Server ─────────────────────────────────────────────────── */

static nng_http_server *server = NULL;
static nng_http_handler *handler = NULL;
static nng_url *server_url = NULL;

/*
 * handler_cb — called on every HTTP request to /api/*
 *
 * Routes internally: /api/test → 200 text, /api/echo → POST echo,
 * /api/json → 200 JSON.  Everything else → 404.
 */
static void
handler_cb(nng_aio *aio)
{
	nng_http_req *req = nng_aio_get_input(aio, 0);
	nng_http_conn *conn = nng_aio_get_input(aio, 2);

	const char *uri = nng_http_req_get_uri(req);
	const char *method = nng_http_req_get_method(req);
	nng_http_res *res;

	(void) conn;

	nng_http_res_alloc(&res);

	if (strcmp(uri, "/api/test") == 0 && strcmp(method, "GET") == 0) {
		nng_http_res_set_header(res, "Content-Type", "text/plain");
		nng_http_res_copy_data(res, "Hello Zephyr HTTP", 17);
	} else if (strcmp(uri, "/api/json") == 0 && strcmp(method, "GET") == 0) {
		const char *body = "{\"status\":\"ok\",\"engine\":\"NanoNNG\"}";
		nng_http_res_set_header(res, "Content-Type", "application/json");
		nng_http_res_copy_data(res, body, strlen(body));
	} else if (strcmp(uri, "/api/echo") == 0 && strcmp(method, "POST") == 0) {
		void *body;
		size_t len;
		nng_http_req_get_data(req, &body, &len);
		nng_http_res_set_header(res, "Content-Type", "text/plain");
		nng_http_res_copy_data(res, body, len);
	} else {
		nng_http_res_set_status(res, NNG_HTTP_STATUS_NOT_FOUND);
		nng_http_res_set_header(res, "Content-Type", "text/plain");
		nng_http_res_copy_data(res, "Not Found", 9);
	}

	nng_aio_set_output(aio, 0, res);
	nng_aio_finish(aio, 0);
}

static void
server_start(void)
{
	int rv;

	if ((rv = nng_url_parse(&server_url, SERVER_URL)) != 0)
		fatal("nng_url_parse", rv);

	if ((rv = nng_http_server_hold(&server, server_url)) != 0)
		fatal("nng_http_server_hold", rv);

	/* Register one handler for the /api tree — callback routes internally */
	if ((rv = nng_http_handler_alloc(&handler, "/api", handler_cb)) != 0)
		fatal("nng_http_handler_alloc", rv);

	nng_http_handler_set_method(handler, NULL); /* accept any method */
	nng_http_handler_set_tree(handler);          /* match /api/* */

	if ((rv = nng_http_server_add_handler(server, handler)) != 0)
		fatal("nng_http_server_add_handler", rv);

	if ((rv = nng_http_server_start(server)) != 0)
		fatal("nng_http_server_start", rv);

	printk("--- Test 0: Server Start ---\n");
	printk("  PASS: HTTP server started on %s\n", SERVER_URL);

}

static void
server_stop(void)
{
	if (server != NULL) {
		nng_http_server_stop(server);
		nng_http_server_release(server);
		server = NULL;
	}
	if (handler != NULL) {
		nng_http_handler_free(handler);
		handler = NULL;
	}
	if (server_url != NULL) {
		nng_url_free(server_url);
		server_url = NULL;
	}
}

/* ── HTTP Client helpers ─────────────────────────────────────────── */

/*
 * http_transact — synchronous HTTP request
 *
 * Opens a connection, sends req, reads res + body, closes.
 * Returns 0 on success; *status and *body_len out params.
 * Caller must free *body with nng_free.
 */
static int
http_transact(nng_http_req *req, nng_http_res *res,
    uint16_t *status, char **body, size_t *body_len)
{
	nng_http_client *client;
	nng_http_conn   *conn;
	nng_url         *url;
	nng_aio         *aio;
	const char      *hdr;
	int              len;
	char            *buf;
	nng_iov          iov;
	int              rv;

	if ((rv = nng_url_parse(&url, CLIENT_URL)) != 0)
		return (rv);
	if ((rv = nng_http_client_alloc(&client, url)) != 0) {
		nng_url_free(url);
		return (rv);
	}
	nng_url_free(url);

	if ((rv = nng_aio_alloc(&aio, NULL, NULL)) != 0) {
		nng_http_client_free(client);
		return (rv);
	}

	/* connect */
	nng_http_client_connect(client, aio);
	nng_aio_wait(aio);
	if ((rv = nng_aio_result(aio)) != 0)
		goto done;
	conn = nng_aio_get_output(aio, 0);

	/* write request */
	nng_http_conn_write_req(conn, req, aio);
	nng_aio_wait(aio);
	if ((rv = nng_aio_result(aio)) != 0)
		goto done;

	/* read response headers */
	nng_http_conn_read_res(conn, res, aio);
	nng_aio_wait(aio);
	if ((rv = nng_aio_result(aio)) != 0)
		goto done;

	*status = nng_http_res_get_status(res);

	/* read body if Content-Length present */
	hdr = nng_http_res_get_header(res, "Content-Length");
	if (hdr == NULL) {
		*body     = NULL;
		*body_len = 0;
		goto done;
	}

	len = (int) strtol(hdr, NULL, 10);
	if (len <= 0) {
		*body     = NULL;
		*body_len = 0;
		goto done;
	}

	buf = nng_alloc((size_t)len);
	if (buf == NULL) {
		rv = NNG_ENOMEM;
		goto done;
	}

	iov.iov_len = (size_t)len;
	iov.iov_buf = buf;
	nng_aio_set_iov(aio, 1, &iov);

	nng_http_conn_read_all(conn, aio);
	nng_aio_wait(aio);
	if ((rv = nng_aio_result(aio)) != 0) {
		nng_free(buf, (size_t)len);
		goto done;
	}

	*body     = buf;
	*body_len = (size_t)len;

done:
	nng_aio_free(aio);
	nng_http_client_free(client);
	return (rv);
}

/* ── Test cases ──────────────────────────────────────────────────── */

static void
test_get_text(void)
{
	nng_http_req *req;
	nng_http_res *res;
	uint16_t      status;
	char         *body;
	size_t        body_len;
	int           rv;

	printk("\n--- Test 1: GET /api/test ---\n");

	if ((rv = nng_http_req_alloc(&req, NULL)) != 0)
		fatal("nng_http_req_alloc", rv);
	nng_http_req_set_uri(req, "/api/test");

	if ((rv = nng_http_res_alloc(&res)) != 0)
		fatal("nng_http_res_alloc", rv);

	rv = http_transact(req, res, &status, &body, &body_len);
	nng_http_req_free(req);

	if (rv != 0) {
		test_fail("GET /api/test — connection failed", rv);
		nng_http_res_free(res);
		return;
	}

	if (status != 200) {
		/* print actual status */
		char msg[64];
		snprintf(msg, sizeof(msg), "GET /api/test — expected 200, got %u", status);
		test_fail(msg, 0);
	} else if (body_len != 17 || memcmp(body, "Hello Zephyr HTTP", 17) != 0) {
		test_fail("GET /api/test — body mismatch", 0);
	} else {
		test_pass("status=200, body='Hello Zephyr HTTP'");
	}

	if (body) nng_free(body, body_len);
	nng_http_res_free(res);
}

static void
test_get_json(void)
{
	nng_http_req *req;
	nng_http_res *res;
	uint16_t      status;
	char         *body;
	size_t        body_len;
	int           rv;

	printk("\n--- Test 2: GET /api/json ---\n");

	if ((rv = nng_http_req_alloc(&req, NULL)) != 0)
		fatal("nng_http_req_alloc", rv);
	nng_http_req_set_uri(req, "/api/json");

	if ((rv = nng_http_res_alloc(&res)) != 0)
		fatal("nng_http_res_alloc", rv);

	rv = http_transact(req, res, &status, &body, &body_len);
	nng_http_req_free(req);

	if (rv != 0) {
		test_fail("GET /api/json — connection failed", rv);
		nng_http_res_free(res);
		return;
	}

	if (status != 200) {
		char msg[64];
		snprintf(msg, sizeof(msg), "GET /api/json — expected 200, got %u", status);
		test_fail(msg, 0);
	} else if (body_len == 0 || body == NULL) {
		test_fail("GET /api/json — empty body", 0);
	} else {
		test_pass("status=200, application/json body received");
	}

	if (body) nng_free(body, body_len);
	nng_http_res_free(res);
}

static void
test_post_echo(void)
{
	nng_http_req *req;
	nng_http_res *res;
	uint16_t      status;
	char         *body;
	size_t        body_len;
	int           rv;
	const char   *msg = "ECHO_TEST_PAYLOAD";

	printk("\n--- Test 3: POST /api/echo ---\n");

	if ((rv = nng_http_req_alloc(&req, NULL)) != 0)
		fatal("nng_http_req_alloc", rv);
	nng_http_req_set_uri(req, "/api/echo");
	nng_http_req_set_method(req, "POST");
	nng_http_req_copy_data(req, msg, strlen(msg));

	if ((rv = nng_http_res_alloc(&res)) != 0)
		fatal("nng_http_res_alloc", rv);

	rv = http_transact(req, res, &status, &body, &body_len);
	nng_http_req_free(req);

	if (rv != 0) {
		test_fail("POST /api/echo — connection failed", rv);
		nng_http_res_free(res);
		return;
	}

	if (status != 200) {
		char msg2[64];
		snprintf(msg2, sizeof(msg2), "POST /api/echo — expected 200, got %u", status);
		test_fail(msg2, 0);
	} else if (body_len != strlen(msg) || memcmp(body, msg, body_len) != 0) {
		test_fail("POST /api/echo — echo body mismatch", 0);
	} else {
		test_pass("status=200, echo body matches");
	}

	if (body) nng_free(body, body_len);
	nng_http_res_free(res);
}

static void
test_not_found(void)
{
	nng_http_req *req;
	nng_http_res *res;
	uint16_t      status;
	char         *body;
	size_t        body_len;
	int           rv;

	printk("\n--- Test 4: GET /api/nonexistent (404) ---\n");

	if ((rv = nng_http_req_alloc(&req, NULL)) != 0)
		fatal("nng_http_req_alloc", rv);
	nng_http_req_set_uri(req, "/api/nonexistent");

	if ((rv = nng_http_res_alloc(&res)) != 0)
		fatal("nng_http_res_alloc", rv);

	rv = http_transact(req, res, &status, &body, &body_len);
	nng_http_req_free(req);

	if (rv != 0) {
		test_fail("GET /api/nonexistent — connection failed", rv);
		nng_http_res_free(res);
		return;
	}

	if (status != 404) {
		char msg[64];
		snprintf(msg, sizeof(msg), "GET /api/nonexistent — expected 404, got %u", status);
		test_fail(msg, 0);
	} else {
		test_pass("status=404 Not Found");
	}

	if (body) nng_free(body, body_len);
	nng_http_res_free(res);
}

/* ── Main ────────────────────────────────────────────────────────── */

void main(void)
{
	printk("\n========================================\n");
	printk("  NanoNNG Zephyr HTTP Test\n");
	printk("========================================\n");

	pass_cnt = 0;
	fail_cnt = 0;

	server_start();

	/* Small delay to ensure server fully ready */
	nng_msleep(500);

	test_get_text();
	test_get_json();
	test_post_echo();
	test_not_found();

	server_stop();

	printk("\n========================================\n");
	printk("  Results: %d PASS, %d FAIL", pass_cnt, fail_cnt);
	if (fail_cnt > 0)
		printk(", %d TESTS FAILED", fail_cnt);
	printk("\n========================================\n");

	while (1) k_sleep(K_SECONDS(3600));
}
