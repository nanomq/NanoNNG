#include "nng/nng.h"
#include "nng/supplemental/nanolib/log.h"
#include "nng/supplemental/tls/tls.h"

const char *cacert = "-----BEGIN CERTIFICATE-----\n"
"MIIDUTCCAjmgAwIBAgIJAPPYCjTmxdt/MA0GCSqGSIb3DQEBCwUAMD8xCzAJBgNV"
"BAYTAkNOMREwDwYDVQQIDAhoYW5nemhvdTEMMAoGA1UECgwDRU1RMQ8wDQYDVQQD"
"DAZSb290Q0EwHhcNMjAwNTA4MDgwNjUyWhcNMzAwNTA2MDgwNjUyWjA/MQswCQYD"
"VQQGEwJDTjERMA8GA1UECAwIaGFuZ3pob3UxDDAKBgNVBAoMA0VNUTEPMA0GA1UE"
"AwwGUm9vdENBMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAzcgVLex1"
"EZ9ON64EX8v+wcSjzOZpiEOsAOuSXOEN3wb8FKUxCdsGrsJYB7a5VM/Jot25Mod2"
"juS3OBMg6r85k2TWjdxUoUs+HiUB/pP/ARaaW6VntpAEokpij/przWMPgJnBF3Ur"
"MjtbLayH9hGmpQrI5c2vmHQ2reRZnSFbY+2b8SXZ+3lZZgz9+BaQYWdQWfaUWEHZ"
"uDaNiViVO0OT8DRjCuiDp3yYDj3iLWbTA/gDL6Tf5XuHuEwcOQUrd+h0hyIphO8D"
"tsrsHZ14j4AWYLk1CPA6pq1HIUvEl2rANx2lVUNv+nt64K/Mr3RnVQd9s8bK+TXQ"
"KGHd2Lv/PALYuwIDAQABo1AwTjAdBgNVHQ4EFgQUGBmW+iDzxctWAWxmhgdlE8Pj"
"EbQwHwYDVR0jBBgwFoAUGBmW+iDzxctWAWxmhgdlE8PjEbQwDAYDVR0TBAUwAwEB"
"/zANBgkqhkiG9w0BAQsFAAOCAQEAGbhRUjpIred4cFAFJ7bbYD9hKu/yzWPWkMRa"
"ErlCKHmuYsYk+5d16JQhJaFy6MGXfLgo3KV2itl0d+OWNH0U9ULXcglTxy6+njo5"
"CFqdUBPwN1jxhzo9yteDMKF4+AHIxbvCAJa17qcwUKR5MKNvv09C6pvQDJLzid7y"
"E2dkgSuggik3oa0427KvctFf8uhOV94RvEDyqvT5+pgNYZ2Yfga9pD/jjpoHEUlo"
"88IGU8/wJCx3Ds2yc8+oBg/ynxG8f/HmCC1ET6EHHoe2jlo8FpU/SgGtghS1YL30"
"IWxNsPrUP+XsZpBJy/mvOhE5QXo6Y35zDqqj8tI7AGmAWu22jg==\n"
"-----END CERTIFICATE-----\n";

const char *cert = "-----BEGIN CERTIFICATE-----\n"
"MIIDEzCCAfugAwIBAgIBATANBgkqhkiG9w0BAQsFADA/MQswCQYDVQQGEwJDTjER"
"MA8GA1UECAwIaGFuZ3pob3UxDDAKBgNVBAoMA0VNUTEPMA0GA1UEAwwGUm9vdENB"
"MB4XDTIwMDUwODA4MDY1N1oXDTMwMDUwNjA4MDY1N1owPzELMAkGA1UEBhMCQ04x"
"ETAPBgNVBAgMCGhhbmd6aG91MQwwCgYDVQQKDANFTVExDzANBgNVBAMMBkNsaWVu"
"dDCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAMy4hoksKcZBDbY680u6"
"TS25U51nuB1FBcGMlF9B/t057wPOlxF/OcmbxY5MwepS41JDGPgulE1V7fpsXkiW"
"1LUimYV/tsqBfymIe0mlY7oORahKji7zKQ2UBIVFhdlvQxunlIDnw6F9popUgyHt"
"dMhtlgZK8oqRwHxO5dbfoukYd6J/r+etS5q26sgVkf3C6dt0Td7B25H9qW+f7oLV"
"PbcHYCa+i73u9670nrpXsC+Qc7Mygwa2Kq/jwU+ftyLQnOeW07DuzOwsziC/fQZa"
"nbxR+8U9FNftgRcC3uP/JMKYUqsiRAuaDokARZxVTV5hUElfpO6z6/NItSDvvh3i"
"eikCAwEAAaMaMBgwCQYDVR0TBAIwADALBgNVHQ8EBAMCBeAwDQYJKoZIhvcNAQEL"
"BQADggEBABchYxKo0YMma7g1qDswJXsR5s56Czx/I+B41YcpMBMTrRqpUC0nHtLk"
"M7/tZp592u/tT8gzEnQjZLKBAhFeZaR3aaKyknLqwiPqJIgg0pgsBGITrAK3Pv4z"
"5/YvAJJKgTe5UdeTz6U4lvNEux/4juZ4pmqH4qSFJTOzQS7LmgSmNIdd072rwXBd"
"UzcSHzsJgEMb88u/LDLjj1pQ7AtZ4Tta8JZTvcgBFmjB0QUi6fgkHY6oGat/W4kR"
"jSRUBlMUbM/drr2PVzRc2dwbFIl3X+ZE6n5Sl3ZwRAC/s92JU6CPMRW02muVu6xl"
"goraNgPISnrbpR6KjxLZkVembXzjNNc=\n"
"-----END CERTIFICATE-----\n";

const char *key = "-----BEGIN RSA PRIVATE KEY-----\n"
"MIIEpAIBAAKCAQEAzLiGiSwpxkENtjrzS7pNLblTnWe4HUUFwYyUX0H+3TnvA86X"
"EX85yZvFjkzB6lLjUkMY+C6UTVXt+mxeSJbUtSKZhX+2yoF/KYh7SaVjug5FqEqO"
"LvMpDZQEhUWF2W9DG6eUgOfDoX2milSDIe10yG2WBkryipHAfE7l1t+i6Rh3on+v"
"561LmrbqyBWR/cLp23RN3sHbkf2pb5/ugtU9twdgJr6Lve73rvSeulewL5BzszKD"
"BrYqr+PBT5+3ItCc55bTsO7M7CzOIL99BlqdvFH7xT0U1+2BFwLe4/8kwphSqyJE"
"C5oOiQBFnFVNXmFQSV+k7rPr80i1IO++HeJ6KQIDAQABAoIBAGWgvPjfuaU3qizq"
"uti/FY07USz0zkuJdkANH6LiSjlchzDmn8wJ0pApCjuIE0PV/g9aS8z4opp5q/gD"
"UBLM/a8mC/xf2EhTXOMrY7i9p/I3H5FZ4ZehEqIw9sWKK9YzC6dw26HabB2BGOnW"
"5nozPSQ6cp2RGzJ7BIkxSZwPzPnVTgy3OAuPOiJytvK+hGLhsNaT+Y9bNDvplVT2"
"ZwYTV8GlHZC+4b2wNROILm0O86v96O+Qd8nn3fXjGHbMsAnONBq10bZS16L4fvkH"
"5G+W/1PeSXmtZFppdRRDxIW+DWcXK0D48WRliuxcV4eOOxI+a9N2ZJZZiNLQZGwg"
"w3A8+mECgYEA8HuJFrlRvdoBe2U/EwUtG74dcyy30L4yEBnN5QscXmEEikhaQCfX"
"Wm6EieMcIB/5I5TQmSw0cmBMeZjSXYoFdoI16/X6yMMuATdxpvhOZGdUGXxhAH+x"
"xoTUavWZnEqW3fkUU71kT5E2f2i+0zoatFESXHeslJyz85aAYpP92H0CgYEA2e5A"
"Yozt5eaA1Gyhd8SeptkEU4xPirNUnVQHStpMWUb1kzTNXrPmNWccQ7JpfpG6DcYl"
"zUF6p6mlzY+zkMiyPQjwEJlhiHM2NlL1QS7td0R8ewgsFoyn8WsBI4RejWrEG9td"
"EDniuIw+pBFkcWthnTLHwECHdzgquToyTMjrBB0CgYEA28tdGbrZXhcyAZEhHAZA"
"Gzog+pKlkpEzeonLKIuGKzCrEKRecIK5jrqyQsCjhS0T7ZRnL4g6i0s+umiV5M5w"
"fcc292pEA1h45L3DD6OlKplSQVTv55/OYS4oY3YEJtf5mfm8vWi9lQeY8sxOlQpn"
"O+VZTdBHmTC8PGeTAgZXHZUCgYA6Tyv88lYowB7SN2qQgBQu8jvdGtqhcs/99GCr"
"H3N0I69LPsKAR0QeH8OJPXBKhDUywESXAaEOwS5yrLNP1tMRz5Vj65YUCzeDG3kx"
"gpvY4IMp7ArX0bSRvJ6mYSFnVxy3k174G3TVCfksrtagHioVBGQ7xUg5ltafjrms"
"n8l55QKBgQDVzU8tQvBVqY8/1lnw11Vj4fkE/drZHJ5UkdC1eenOfSWhlSLfUJ8j"
"ds7vEWpRPPoVuPZYeR1y78cyxKe1GBx6Wa2lF5c7xjmiu0xbRnrxYeLolce9/ntp"
"asClqpnHT8/VJYTD7Kqj0fouTTZf0zkig/y+2XERppd8k+pSKjUCPQ==\n"
"-----END RSA PRIVATE KEY-----\n";

void
test_tls_pki(int idx)
{
	int rv = 0;
	// TODO
	const char *url = "tls+mqtt-tcp://broker.emqxio:8883";

	nng_stream_dialer *dialer = NULL;
	nng_aio * aio             = NULL;
	nng_tls_config * c1       = NULL;
	nng_stream * s            = NULL;
	nng_aio * saio            = NULL;
	nng_aio * raio            = NULL;
	nng_msg * rmsg            = NULL;

	if (0 != nng_stream_dialer_alloc(&dialer, url)) {
		log_error("failed to alloc stream dialer rv%d", rv);
		goto end;
	}

	if (0 != (rv = nng_aio_alloc(&aio, NULL, NULL))) {
		log_error("failed to alloc aio rv%d", rv);
		goto end;
	}
	nng_aio_set_timeout(aio, 5000); // 5 sec

	// set certs
	if ((rv = nng_tls_config_alloc(&c1, NNG_TLS_MODE_CLIENT)) != 0) {
		log_error("failed to alloc tls config rv%d", rv);
		goto end;
	}
	if ((rv = nng_tls_config_ca_chain(c1, cacert, NULL)) != 0) {
		log_error("failed to config ca chain rv%d", rv);
		goto end;
	}
	if ((rv = nng_tls_config_own_cert(c1, cert, key, NULL)) != 0) {
		log_error("failed to config own cert rv%d", rv);
		goto end;
	}
	if ((rv = nng_stream_dialer_set_ptr(dialer, NNG_OPT_TLS_CONFIG, c1)) != 0) {
		log_error("failed to set tls config rv%d", rv);
		goto end;
	}

	nng_stream_dialer_dial(dialer, aio);
	nng_aio_wait(aio);
	rv = nng_aio_result(aio);
	if (rv != 0) {
		log_error("failed to dial to broker %s", url);
		goto end;
	}
	s = nng_aio_get_output(aio, 0);
	if (s == NULL) {
		log_error("failed to get stream after dialing");
		goto end;
	}
	log_info("tcp connected-------");

	if ((rv = nng_aio_alloc(&raio, NULL, NULL)) != 0) {
		log_error("failed to allow raio for receive msg");
		goto end;
	}
	nng_stream_recv(s, raio);

	if ((rv = nng_aio_alloc(&saio, NULL, NULL)) != 0) {
		log_error("failed to allow saio for send msg");
		goto end;
	}
	nng_iov iov;
	iov.iov_buf = "aaa";
	iov.iov_len = 3;
	nng_aio_set_iov(saio, 1, &iov);

	nng_stream_send(s, saio);
	nng_aio_wait(saio);
	rv = nng_aio_result(saio);
	if (rv != 0) {
		log_error("failed to send msg to broker %s rv%d", url, rv);
		goto end;
	}
	log_info("sent something-------");

	nng_aio_wait(raio);
	rv = nng_aio_result(raio);
	rmsg = nng_aio_get_msg(raio);
	if (rv != 0 || rmsg == NULL) {
		log_error("failed to receivd msg from broker or null msg rv%d msg%p", rv, rmsg);
		goto end;
	}

end:
	if (c1)
		nng_tls_config_free(c1);
	if (rmsg)
		nng_msg_free(rmsg);
	if (aio)
		nng_aio_free(aio);
	if (saio)
		nng_aio_free(saio);
	if (raio)
		nng_aio_free(raio);
	if (s)
		nng_stream_free(s);
	if (dialer)
		nng_stream_dialer_free(dialer);
	log_info("-----------%d----------", idx);
}

int
main()
{
	log_set_level(NNG_LOG_INFO);
	log_add_console(NNG_LOG_INFO, NULL);

	int i = 0;
	while (true) {
		test_tls_pki(i);
		nng_msleep(200);
		++i;
	}
}
