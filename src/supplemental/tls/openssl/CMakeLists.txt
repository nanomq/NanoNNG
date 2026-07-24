include(FindThreads)

if(TLS_EXTERN_PRIVATE_KEY)
    nng_defines(TLS_EXTERN_PRIVATE_KEY)
    if(NOT TLS_EXTERN_PRIVATE_KEY_LIBRARIES)
        message(FATAL_ERROR "TLS_EXTERN_PRIVATE_KEY requires TLS_EXTERN_PRIVATE_KEY_LIBRARIES to be set")
    endif()
    message(NOTICE "TLS external private key support is enabled")
endif(TLS_EXTERN_PRIVATE_KEY)

if (NNG_TLS_ENGINE STREQUAL "open")
    message(NOTICE "
        ************************************************************
        Linking against OpenSSL may change license terms.
        Consult a lawyer and the license files for details.
        ************************************************************")

    if(TLS_EXTERN_PRIVATE_KEY)
        nng_sources(openssl.c)
        nng_sources(tee.cc)
    else()
        nng_sources(openssl.c)
    endif()

    # If OpenSSL was added by a consuming project, then we should use that
    # instance of it, instead of configuring our own.
    if (TARGET openssl)
        if(TLS_EXTERN_PRIVATE_KEY)
            nng_link_libraries(${TLS_EXTERN_PRIVATE_KEY_LIBRARIES})
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
        if(TLS_EXTERN_PRIVATE_KEY)
            nng_link_libraries_public(${TLS_EXTERN_PRIVATE_KEY_LIBRARIES})
            # Comment the line above and uncomment the line following when DEBUG_PKI_LOCAL
            #nng_link_libraries_public(OpenSSL::SSL OpenSSL::Crypto)
        else()
            nng_link_libraries_public(OpenSSL::SSL OpenSSL::Crypto)
        endif()
    endif()

    nng_defines(NNG_TLS_ENGINE_INIT=nng_tls_engine_init_open)
    nng_defines(NNG_TLS_ENGINE_FINI=nng_tls_engine_fini_open)
    nng_defines(NNG_SUPP_TLS)
    nng_defines(NNG_TLS_ENGINE_OPENSSL)
endif ()
