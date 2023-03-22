// #include <nuts.h>
// #include "core/nng_impl.h"

// void
// test_main(void)
// {
// 	nni_sp_tran *tran;
// 	void *ep;
// 	nni_aio     *aio;
// 	nni_dialer  *ndialer;
// 	nng_url     *url_protocol;
// 	nng_url     *url_broker;
// 	char         urlstr[48];
// 	strcpy(urlstr,"mqtt-tcp://127.0.0.1");

// 	nni_mqtt_tran_sys_init();
// 	nng_url_parse(&url_protocol, urlstr);
// 	tran = nni_mqtt_tran_find(url_protocol);
// 	tran->tran_init();

// 	tran->tran_fini();
// 	nng_url_parse(&url_protocol, urlstr);
// 	tran = nni_mqtt_tran_find(url_protocol);
// 	tran->tran_init();

// 	strcpy(urlstr,"mqtt-quic:432121.xyz:14567");
// 	nng_url_parse(&url_broker, urlstr);
// 	tran->tran_dialer->d_init(&ep, url_broker, ndialer);
// 	tran->tran_dialer->d_connect(ep, aio);

// 	tran->tran_fini();
// 	nni_mqtt_tran_sys_fini();
// }

// NUTS_TESTS = {
// 	{ "start", test_main },
// 	{ NULL, NULL }
// };