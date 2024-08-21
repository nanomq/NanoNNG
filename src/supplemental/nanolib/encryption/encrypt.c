#include "nng/supplemental/nanolib/argon2.h"
#include "nng/supplemental/nanolib/encrypt.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int get_encrypt(void* hash, char* password)
{
    uint8_t salt[SALTLEN];
    memset( salt, 0x00, SALTLEN );

    uint8_t *pwd = (uint8_t *)strdup(password);
    uint32_t pwdlen = strlen((char *)pwd);

    uint32_t t_cost = 2;            // 2-pass computation
    uint32_t m_cost = (1<<16);      // 64 mebibytes memory usage
    uint32_t parallelism = 1;       // number of threads and lanes

    // high-level API
    argon2i_hash_raw(t_cost, m_cost, parallelism, pwd, pwdlen, salt, SALTLEN, hash, HASHLEN);

    // // low-level API
    // argon2_context context = {
    //     hash,  /* output array, at least HASHLEN in size */
    //     HASHLEN, /* digest length */
    //     pwd, /* password array */
    //     pwdlen, /* password length */
    //     salt,  /* salt array */
    //     SALTLEN, /* salt length */
    //     NULL, 0, /* optional secret data */
    //     NULL, 0, /* optional associated data */
    //     t_cost, m_cost, parallelism, parallelism,
    //     ARGON2_VERSION_13, /* algorithm version */
    //     NULL, NULL, /* custom memory allocation / deallocation functions */
    //     /* by default only internal memory is cleared (pwd is not wiped) */
    //     ARGON2_DEFAULT_FLAGS
    // };

    // int rc = argon2i_ctx( &context );
    // if(ARGON2_OK != rc) {
    //     printf("Error: %s\n", argon2_error_message(rc));
    //     exit(1);
    // }

    free(pwd);
    return 0;
}