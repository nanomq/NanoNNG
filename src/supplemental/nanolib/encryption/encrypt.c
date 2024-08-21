#include "nng/supplemental/nanolib/argon2.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define HASHLEN 32
#define SALTLEN 16

uint8_t* get_encryption(char* password)
{
    uint8_t hash1[HASHLEN];
    uint8_t hash2[HASHLEN];

    uint8_t salt[SALTLEN];
    memset( salt, 0x00, SALTLEN );

    uint8_t *pwd = (uint8_t *)strdup(password);
    uint32_t pwdlen = strlen((char *)pwd);

    uint32_t t_cost = 2;            // 2-pass computation
    uint32_t m_cost = (1<<16);      // 64 mebibytes memory usage
    uint32_t parallelism = 1;       // number of threads and lanes

    // high-level API
    argon2i_hash_raw(t_cost, m_cost, parallelism, pwd, pwdlen, salt, SALTLEN, hash1, HASHLEN);

    // low-level API
    argon2_context context = {
        hash2,  /* output array, at least HASHLEN in size */
        HASHLEN, /* digest length */
        pwd, /* password array */
        pwdlen, /* password length */
        salt,  /* salt array */
        SALTLEN, /* salt length */
        NULL, 0, /* optional secret data */
        NULL, 0, /* optional associated data */
        t_cost, m_cost, parallelism, parallelism,
        ARGON2_VERSION_13, /* algorithm version */
        NULL, NULL, /* custom memory allocation / deallocation functions */
        /* by default only internal memory is cleared (pwd is not wiped) */
        ARGON2_DEFAULT_FLAGS
    };

    int rc = argon2i_ctx( &context );
    if(ARGON2_OK != rc) {
        printf("Error: %s\n", argon2_error_message(rc));
        exit(1);
    }
    free(pwd);

    // for( int i=0; i<HASHLEN; ++i ) printf( "%02x", hash1[i] ); printf( "\n" );
    // if (memcmp(hash1, hash2, HASHLEN)) {
    //     for( int i=0; i<HASHLEN; ++i ) {
    //         printf( "%02x", hash2[i] );
    //     }
    //     printf("\nfail\n");
    // }
    // else printf("ok\n");
    return hash1;
}