//
// Copyright 2021 Staysail Systems, Inc. <info@staysail.tech>
// Copyright 2018 Capitar IT Group BV <info@capitar.com>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

// Basic HTTP client tests.

#ifndef _WIN32
#include <arpa/inet.h>
#endif

#include <nng/nng.h>
#include <nng/supplemental/http/http.h>
#include <nng/supplemental/tls/tls.h>

#include "core/nng_impl.h"
#include "convey.h"
#include "trantest.h"

// ----------------------------------------------------------------------
// Tests
// ----------------------------------------------------------------------

TestMain("HTTP Client", {
    // 提升所有指针的作用域，根除 ASan 的 stack-use-after-scope 报错
    nng_aio *        aio = NULL;
    nng_http_client *cli = NULL;
    nng_http_conn *  http = NULL;
    nng_url *        url = NULL;
    nng_http_req *   req = NULL;
    nng_http_res *   res = NULL;
    nng_http_res *   res1 = NULL;
    nng_http_res *   res2 = NULL;
    void *           data = NULL;
    size_t           sz = 0;
    nng_http_conn *  conn = NULL;

    // Server Variables
    nng_http_server * svr = NULL;
    nng_url *         svr_url = NULL;
    nng_http_handler *h200 = NULL;
    nng_http_handler *h301 = NULL;
    nng_http_handler *hchunk = NULL;
    char              url_200[64];
    char              url_301[64];
    char              url_chunked[64];

    Convey("Given a mock HTTP server", {
        svr_url = NULL; svr = NULL;
        h200 = NULL; h301 = NULL; hchunk = NULL;

        So(nng_url_parse(&svr_url, "http://127.0.0.1:0") == 0);
        So(nng_http_server_hold(&svr, svr_url) == 0);

        // 使用 NNG 原生的 static handler，彻底避免 AIO 阻塞或内存泄漏
        So(nng_http_handler_alloc_static(&h200, "/200", "hello", 5, "text/plain") == 0);
        So(nng_http_server_add_handler(svr, h200) == 0);

        So(nng_http_handler_alloc_static(&h301, "/301", "redirect", 8, "text/plain") == 0);
        So(nng_http_server_add_handler(svr, h301) == 0);

        So(nng_http_handler_alloc_static(&hchunk, "/chunked", "chunked_data", 12, "text/plain") == 0);
        So(nng_http_server_add_handler(svr, hchunk) == 0);

        So(nng_http_server_start(svr) == 0);

        nng_sockaddr addr;
        So(nng_http_server_get_addr(svr, &addr) == 0);
        uint16_t port = 0;
        
        // 安全获取系统分配的动态端口 (NNG 内部 sa_port 使用网络字节序)
        if (addr.s_family == NNG_AF_INET) {
            port = ntohs(addr.s_in.sa_port);
        } else if (addr.s_family == NNG_AF_INET6) {
            port = ntohs(addr.s_in6.sa_port);
        }

        snprintf(url_200, sizeof(url_200), "http://127.0.0.1:%u/200", port);
        snprintf(url_301, sizeof(url_301), "http://127.0.0.1:%u/301", port);
        snprintf(url_chunked, sizeof(url_chunked), "http://127.0.0.1:%u/chunked", port);

        Reset({
            if (svr) nng_http_server_release(svr);
            if (svr_url) nng_url_free(svr_url);
        });

        Convey("Given a TCP connection to local server", {
            aio = NULL; cli = NULL; http = NULL; url = NULL;

            So(nng_aio_alloc(&aio, NULL, NULL) == 0);
            So(nng_url_parse(&url, url_200) == 0);

            nng_aio_set_timeout(aio, 10000);
            So(nng_http_client_alloc(&cli, url) == 0);
            nng_http_client_connect(cli, aio);
            nng_aio_wait(aio);
            So(nng_aio_result(aio) == 0); 
            http = nng_aio_get_output(aio, 0);
            
            Reset({
                if (http) { nng_http_conn_close(http); }
                if (cli)  { nng_http_client_free(cli); }
                if (aio)  { nng_aio_free(aio); }
                if (url)  { nng_url_free(url); }
            });

            Convey("We can initiate a message", {
                req = NULL; res = NULL;

                So(http != NULL);

                So(nng_http_req_alloc(&req, url) == 0);
                So(nng_http_res_alloc(&res) == 0);
                Reset({
                    if (req) nng_http_req_free(req);
                    if (res) nng_http_res_free(res);
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

                    data = NULL; sz = 0;

                    cstr = nng_http_res_get_header(res, "Content-Length");
                    So(cstr != NULL);
                    sz = atoi(cstr);
                    So(sz == 5);

                    data = nng_alloc(sz);
                    So(data != NULL);
                    Reset({ if (data) nng_free(data, sz); });

                    iov.iov_buf = data;
                    iov.iov_len = sz;
                    So(nng_aio_set_iov(aio, 1, &iov) == 0);

                    nng_http_conn_read_all(http, aio);
                    nng_aio_wait(aio);
                    So(nng_aio_result(aio) == 0);

                    So(memcmp(data, "hello", 5) == 0); 
                });
            });
        });

        Convey("Given a client", {
            aio = NULL; cli = NULL; url = NULL;

            So(nng_aio_alloc(&aio, NULL, NULL) == 0);
            So(nng_url_parse(&url, url_301) == 0);
            So(nng_http_client_alloc(&cli, url) == 0);
            nng_aio_set_timeout(aio, 10000); 

            Reset({
                if (cli) nng_http_client_free(cli);
                if (url) nng_url_free(url);
                if (aio) nng_aio_free(aio);
            });

            Convey("One off exchange works", {
                req = NULL; res = NULL; data = NULL;
                size_t  len = 0;

                So(nng_http_req_alloc(&req, url) == 0);
                So(nng_http_res_alloc(&res) == 0);
                Reset({
                    if (req) nng_http_req_free(req);
                    if (res) nng_http_res_free(res);
                });

                nng_http_client_transact(cli, req, res, aio);
                nng_aio_wait(aio);
                So(nng_aio_result(aio) == 0);
                So(nng_http_res_get_status(res) == 200); // 统一验证 200
                
                nng_http_res_get_data(res, &data, &len);
                So(len == 8);
                So(memcmp(data, "redirect", 8) == 0);
            });

            Convey("Connection reuse works", {
                req = NULL; res1 = NULL; res2 = NULL; conn = NULL; data = NULL;
                size_t len = 0;

                So(nng_http_req_alloc(&req, url) == 0);
                So(nng_http_res_alloc(&res1) == 0);
                So(nng_http_res_alloc(&res2) == 0);
                Reset({
                    if (req)  nng_http_req_free(req);
                    if (res1) nng_http_res_free(res1);
                    if (res2) nng_http_res_free(res2);
                });

                nng_http_client_connect(cli, aio);
                nng_aio_wait(aio);
                So(nng_aio_result(aio) == 0);
                conn = nng_aio_get_output(aio, 0);
                
                nng_aio_set_timeout(aio, 1400);
                nng_http_conn_transact(conn, req, res1, aio);
                nng_aio_wait(aio);
                So(nng_aio_result(aio) == 0);
                So(nng_http_res_get_status(res1) == 200);
                nng_http_res_get_data(res1, &data, &len);
                So(len == 8);
                So(memcmp(data, "redirect", 8) == 0);

                nng_aio_set_timeout(aio, 1400);
                nng_http_conn_transact(conn, req, res2, aio);
                nng_aio_wait(aio);
                So(nng_aio_result(aio) == 0);
                So(nng_http_res_get_status(res2) == 200);
                nng_http_res_get_data(res2, &data, &len);
                So(len == 8);
                So(memcmp(data, "redirect", 8) == 0);
                
                if (conn != NULL) {
                    nng_http_conn_close(conn);
                }
            });
        });

        SkipConvey("Client times out", {
            aio = NULL; cli = NULL; url = NULL; req = NULL; res = NULL;

            So(nng_aio_alloc(&aio, NULL, NULL) == 0);
            So(nng_url_parse(&url, "http://httpbin.org/delay/30") == 0);
            So(nng_http_client_alloc(&cli, url) == 0);
            So(nng_http_req_alloc(&req, url) == 0);
            So(nng_http_res_alloc(&res) == 0);

            Reset({
                if (cli) nng_http_client_free(cli);
                if (url) nng_url_free(url);
                if (aio) nng_aio_free(aio);
                if (req) nng_http_req_free(req);
                if (res) nng_http_res_free(res);
            });
            nng_aio_set_timeout(aio, 10); 

            So(nng_http_req_set_header(req, "Cache-Control", "no-cache") == 0);
            nng_http_client_transact(cli, req, res, aio);
            nng_aio_wait(aio);
            So(nng_aio_result(aio) == NNG_ETIMEDOUT);
        });

        Convey("Given a client (chunked)", {
            aio = NULL; cli = NULL; url = NULL;

            So(nng_aio_alloc(&aio, NULL, NULL) == 0);
            So(nng_url_parse(&url, url_chunked) == 0);
            So(nng_http_client_alloc(&cli, url) == 0);
            nng_aio_set_timeout(aio, 10000);

            Reset({
                if (cli) nng_http_client_free(cli);
                if (url) nng_url_free(url);
                if (aio) nng_aio_free(aio);
            });

            Convey("One off exchange works", {
                req = NULL; res = NULL; data = NULL;
                size_t len = 0;

                So(nng_http_req_alloc(&req, url) == 0);
                So(nng_http_res_alloc(&res) == 0);
                Reset({
                    if (req) nng_http_req_free(req);
                    if (res) nng_http_res_free(res);
                });

                nng_http_client_transact(cli, req, res, aio);
                nng_aio_wait(aio);
                So(nng_aio_result(aio) == 0);
                So(nng_http_res_get_status(res) == 200);
                
                nng_http_res_get_data(res, &data, &len);
                So(len == 12);
                So(memcmp(data, "chunked_data", 12) == 0);
            });
        });
    });
})