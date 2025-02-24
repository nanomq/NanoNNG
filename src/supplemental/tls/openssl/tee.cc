#include <string>
#include <stdio.h>
#include <cstring>
#include <nng/supplemental/tls/tee.h>

#ifdef TLS_EXTERN_PRIVATE_KEY
#include "thirdparty/tee_interface.h"
#include "thirdparty/csmwDesayPki.h"
#endif

using namespace std;

int teeGetCA(char **cacert) {
	// std::string teeGetTeeRootCert();
	string ca = teeGetTeeRootCert();
	printf("--ca: %s\n", ca.c_str());
	// overwrite certs
	char *certs = strdup(ca.c_str());
	int   len   = strlen(certs);
	printf("--len: %d\n", len);

	*cacert = certs;
	return len;
}

