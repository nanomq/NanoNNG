#include <stdio.h>
#include <string.h>

#ifdef TLS_EXTERN_SS_CERTS

#include "nng/nng.h"
#include "core/nng_impl.h"
#include "nng/supplemental/tls/tee2.h"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/crypto.h>
#include <openssl/x509v3.h>
#include <openssl/engine.h>


static char *engine_name = NULL;
static char *engine_path = NULL;

/************************* openssl Engine ***********************/

ENGINE *engine_init(const char *name, const char *path)
{
    ENGINE *retengine = NULL;
    int number = 1;

    if ( (NULL == name) || (NULL == path) )
    {
        log_error("parameter is emtpy");

        return retengine;
    }

    // initialization
    ERR_print_errors_fp(stderr);
    OpenSSL_add_all_algorithms();
    OPENSSL_load_builtin_modules();
    OPENSSL_init_crypto(OPENSSL_INIT_ENGINE_DYNAMIC, NULL);

    // module loaded once
    while( (NULL == (retengine = ENGINE_by_id(name) ) ) &&
           (0 <= --number) )
    {
        log_info("loading engine");
        // building OpenSSL's configuration file path
        // loading configuration
        if (CONF_modules_load_file(path, "openssl_conf", 0) != 1)
        {
            log_error("Failed to load engine configureation");
            ERR_print_errors_fp(stderr);
        }
    }

    if (retengine != NULL)
    {
        log_info("engine loading successful");
    }
    else
    {
        log_error("engine loading failed");
    }

    if (-1 == ENGINE_init(retengine) )
    {
        log_error("failed to init engine");

        ENGINE_free(retengine);
        retengine = NULL;
    }

    return retengine;
}

bool loading_owner_cert_from_engine(ENGINE *engine, SSL_CTX *ctx)
{
    bool retstatus = false;
    X509 *leaf = NULL;
    STACK_OF(X509) *chain = NULL;

    if ( (NULL == engine) || (NULL == ctx) )
    {
        log_error("parameter is empty");

        return retstatus;
    }

    if (!ENGINE_load_ssl_client_cert(engine, NULL, NULL, &leaf, NULL, &chain, NULL, NULL))
    {
        log_error("load ssl leaf cert failed");

        goto GAME_OVER;
    }

    log_info("load ssl leaf cert success");

    if ( (NULL != leaf) &&
         (SSL_CTX_use_certificate(ctx, leaf) != 1) )
    {
        log_error("Failed to set certificate to SSL_CTX");
        goto GAME_OVER;
    }

    retstatus = true;

    if(NULL == chain)
    {
        goto GAME_OVER;
    }

    // loading chain
    for (int i = 0; i < sk_X509_num(chain); i++)
    {
        if (SSL_CTX_add0_chain_cert(ctx, sk_X509_value(chain, i) ) != 1)
        {
            retstatus = false;
            log_error("SSL_CTX_add0_chain_cert failed");
            break;
        }
    }

GAME_OVER:
    if (NULL != leaf)
    {
        X509_free(leaf);
    }

    if (NULL != chain)
    {
        sk_X509_free(chain);
    }

    return retstatus;
}

bool loading_owner_key_from_engine(ENGINE *engine, SSL_CTX *ctx)
{
    bool retstatus = false;
    EVP_PKEY *pkey = NULL;

    if ( (NULL == engine) || (NULL == ctx) )
    {
        log_error("parameter is empty");

        return retstatus;
    }

    pkey = ENGINE_load_private_key(engine, NULL, NULL, NULL);
    if (pkey == NULL)
    {
        log_error("failed to get key, pkey is null");

        return retstatus;
    }

    log_info("load private key success");

    if (1 != SSL_CTX_use_PrivateKey(ctx, pkey) )
    {
        log_error("ssl set private key failed [%s]", ERR_error_string(ERR_get_error(), NULL) );

        goto GAME_OVER;
    }

    retstatus = true;

GAME_OVER:
    if (pkey)
    {
        EVP_PKEY_free(pkey);
    }

    return retstatus;
}

char *get_value_of_key_value_pair(const char *pair, const char *key, const char separator)
{
    char *value_pointer = NULL;
    char *start_pointer = NULL, *end_pointer = NULL;
    size_t key_size, value_size;

    if ( (NULL == pair) || (NULL == key) )
    {
        log_error("parameter is emtpy");

        return value_pointer;
    }

    key_size = strlen(key);

    // parser key
    start_pointer = strstr(pair, key);
    start_pointer += start_pointer ? key_size : 0;
    if ( start_pointer &&
         ('\0' != *start_pointer) &&
         (separator != *start_pointer ) )
    {
        end_pointer = strchr(start_pointer, separator);
        end_pointer = end_pointer ? end_pointer : start_pointer + strlen(start_pointer);
        value_size = end_pointer - start_pointer;
        value_pointer = (char *)nng_alloc(value_size + 1);
        if (NULL == value_pointer)
        {
            log_error("allocate space is emtpy");

            return value_pointer;
        }

        // copy value
        memcpy(value_pointer, start_pointer, value_size);
        value_pointer[value_size] = '\0';
    }
    else
    {
        log_info("not find key:%s", key);
    }

    return value_pointer;
}

bool get_engin_info_in_passwd(const char *passwd)
{
    bool retstatus = false;
    const char *engine_path_prefix = "engine_path:";
    const char *engine_name_prefix = "engine_name:";
    const char separator = ';';

    if (NULL == passwd)
    {
        log_error("parameter is emtpy");

        return retstatus;
    }

    // parser engine_path
    engine_path = get_value_of_key_value_pair(passwd, engine_path_prefix, separator);
    if (engine_path)
    {
        log_info("engine_path: %s", engine_path);
    }

    // parser engine_name
    engine_name = get_value_of_key_value_pair(passwd, engine_name_prefix, separator);
    if (engine_name)
    {
        log_info("engine_name: %s", engine_name);
    }

    return true;
}

char *
tee_get_engine_name()
{
	return engine_name;
}

char *
tee_get_engine_path()
{
	return engine_path;
}

#endif // TLS_EXTERN_SS_CERTS
