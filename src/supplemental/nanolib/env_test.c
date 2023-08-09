#include "nng/supplemental/nanolib/env.h"

#include "nuts.h"

void
test_env(void)
{
	conf *conf_test = NULL;
	NUTS_TRUE((conf_test = nng_zalloc(sizeof(conf))) != NULL);

	read_env_conf(conf_test);
	conf_fini(conf_test);
}

NUTS_TESTS = {
   {"test env", test_env},
   {NULL, NULL} 
};