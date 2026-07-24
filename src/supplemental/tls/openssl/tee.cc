#include <string>
#include <stdio.h>
#include <cstring>
#include <nng/supplemental/tls/tee.h>

using namespace std;

#ifdef DEBUG_PKI_LOCAL

std::string teeGetTeeRootCert()
{
	return string(
"-----BEGIN CERTIFICATE-----\n"
"-----END CERTIFICATE-----\n");
}

#else


#endif // DEBUG_PKI_LOCAL

int teeGetCA(char **cacert) {
	// overwrite certs
	char *certs = strdup(ca.c_str());
	int   len   = strlen(certs);

	*cacert = certs;
	return len;
}

