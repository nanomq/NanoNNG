#include <string>
#include <cstring>
#include <nng/supplemental/tls/tee.h>

#ifdef TLS_EXTERN_PRIVATE_KEY
#include "tee_interface.h"
#include "csmwDesayPki.h"
#endif

using namespace std;

int teeGetCA(char **cacert) {
	// std::string teeGetTeeRootCert();
	string ca = teeGetTeeRootCert();
	// overwrite certs
	char *certs = strdup(ca.c_str());
	int   len   = strlen(certs);

	*cacert = certs;
	return len;
}

