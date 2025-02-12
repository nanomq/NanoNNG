#include <string>
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
	// overwrite certs
	char *certs = strdup(ca.c_str());
	int   len   = strlen(certs);

	*cacert = certs;
	return len;
}

