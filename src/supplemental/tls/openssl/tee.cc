#include <string>
#include <stdio.h>
#include <cstring>
#include <nng/supplemental/tls/tee.h>

#ifdef TLS_EXTERN_PRIVATE_KEY
#include "/home/runner/work/NanoMQ_mirror/NanoMQ_mirror/extern/thirdparty/tee_interface.h"
#include "/home/runner/work/NanoMQ_mirror/NanoMQ_mirror/extern/thirdparty/csmwDesayPki.h"
//#include "/home/wangha/Documents/NanoMQ_mirror/extern/thirdparty/tee_interface.h"
//#include "/home/wangha/Documents/NanoMQ_mirror/extern/thirdparty/csmwDesayPki.h"
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

