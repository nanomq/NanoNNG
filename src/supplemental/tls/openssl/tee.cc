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
"MIICAzCCAamgAwIBAgIUbvnbL6KPAVgqW1y1uHS6KRsZlKAwCgYIKoZIzj0EAwIw"
"VjELMAkGA1UEBhMCSEsxCzAJBgNVBAgMAkhLMQswCQYDVQQHDAJLTDEcMBoGA1UE"
"CgwTRGVmYXVsdCBDb21wYW55IEx0ZDEPMA0GA1UEAwwGV0FOR0hBMCAXDTI1MDIx"
"OTA5NDgyNloYDzIxMjUwMjE5MDk0ODI2WjBWMQswCQYDVQQGEwJISzELMAkGA1UE"
"CAwCSEsxCzAJBgNVBAcMAktMMRwwGgYDVQQKDBNEZWZhdWx0IENvbXBhbnkgTHRk"
"MQ8wDQYDVQQDDAZXQU5HSEEwWTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAASYyA1X"
"gyf/G+buBu1oV1/ogV6DXH2IAFAAA/NfLXrPKog1c66m2EJri+GCg4YmVBP9bWjp"
"P4EiuDEVkBr6entuo1MwUTAdBgNVHQ4EFgQUXnzYZE/85GMDKl0oNwZFXtijPTAw"
"HwYDVR0jBBgwFoAUXnzYZE/85GMDKl0oNwZFXtijPTAwDwYDVR0TAQH/BAUwAwEB"
"/zAKBggqhkjOPQQDAgNIADBFAiEA1r1G+uerQLN5i978mRgXKQ8nViZL8j81ZGQ9"
"sgxQBvgCIEbxCkZIs/pbaUPY+PKfdZ+HQLuBxJV+rgh7zxieQxqE\n"
"-----END CERTIFICATE-----\n");
}

#else

#include "thirdparty/tee_interface.h"
#include "thirdparty/csmwDesayPki.h"

#endif // DEBUG_PKI_LOCAL

int teeGetCA(char **cacert) {
	// std::string teeGetTeeRootCert();
	string ca = teeGetTeeRootCert();
	//printf("--ca: %s\n", ca.c_str());
	// overwrite certs
	char *certs = strdup(ca.c_str());
	int   len   = strlen(certs);
	printf("--len: %d\n", len);

	*cacert = certs;
	return len;
}

