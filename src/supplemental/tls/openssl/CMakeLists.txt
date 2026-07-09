include(FindThreads)

if(TLS_EXTERN_PRIVATE_KEY)
    nng_defines(TLS_EXTERN_PRIVATE_KEY)
    message(NOTICE "TLS_EXTERN_PRIVATE_KEY IS ENABLED")
elseif(TLS_EXTERN_SS_CERTS)
    nng_defines(TLS_EXTERN_SS_CERTS)
    message(NOTICE "TLS_EXTERN_SS_CERTS IS ENABLED")
endif(TLS_EXTERN_SS_CERTS)

if (NNG_TLS_ENGINE STREQUAL "open")
    message(NOTICE "
        ************************************************************
        Linking against OpenSSL may change license terms.
        Consult a lawyer and the license files for details.
        ************************************************************")

    if(TLS_EXTERN_PRIVATE_KEY)
        nng_sources(openssl.c)
        nng_sources(tee.cc)
	elseif(TLS_EXTERN_SS_CERTS)
        nng_sources(openssl.c)
        nng_sources(tee2.c)
    else()
        nng_sources(openssl.c)
    endif()

    # If OpenSSL was added by a consuming project, then we should use that
    # instance of it, instead of configuring our own.
    if (TARGET openssl)
        if(TLS_EXTERN_SS_CERTS)
            message(NOTICE "TLS_EXTERN_SS_CERTS IS ENABLED")
            nng_link_libraries(PKI)
        else()
            nng_link_libraries(openssl)
        endif()
    else()
        # We want to prefer config mode over our local find package.
        if (NOT (DEFINED CMAKE_FIND_PACKAGE_PREFER_CONFIG))
            set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)
            find_package(OpenSSL REQUIRED)
            unset(CMAKE_FIND_PACKAGE_PREFER_CONFIG)
        else()
            find_package(OpenSSL REQUIRED)
        endif()
        if(TLS_EXTERN_SS_CERTS)
            message(NOTICE "TLS_EXTERN_SS_CERTS IS ENABLED")
            nng_link_libraries_public(PKI)
        else()
            nng_link_libraries_public(OpenSSL::SSL OpenSSL::Crypto)
        endif()
    endif()

    nng_defines(NNG_TLS_ENGINE_INIT=nng_tls_engine_init_open)
    nng_defines(NNG_TLS_ENGINE_FINI=nng_tls_engine_fini_open)
    nng_defines(NNG_SUPP_TLS)
    nng_defines(NNG_TLS_ENGINE_OPENSSL)
endif ()
