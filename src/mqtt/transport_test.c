#include <nuts.h>
#include "core/nng_impl.h"

void
test_main(void)
{
	nni_mqtt_tran_sys_init();
	nni_mqtt_tran_sys_fini();
}

NUTS_TESTS = {
	{ "start", test_main },
	{ NULL, NULL },
};