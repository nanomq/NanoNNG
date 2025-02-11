#include <string>
#include <cstring>
#include <nng/supplemental/tls/tee.h>

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

