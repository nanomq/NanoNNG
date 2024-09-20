#ifndef _NNG_SUPPLEMENTAL_NANOLIB_ENCRYPT_H
#define _NNG_SUPPLEMENTAL_NANOLIB_ENCRYPT_H

#include "nng/nng.h"

#define HASHLEN 32
#define SALTLEN 16

NNG_DECL int get_encrypt(void *hash, char* password);

#endif