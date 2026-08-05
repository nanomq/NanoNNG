# Copyright 2026 Liebherr-Digital Development Center (LDC) <peter.bestler@liebherr.de>
#
# This software is supplied under the terms of the MIT License, a
# copy of which should be located in the distribution where this
# file was obtained (LICENSE.txt).

include(FindThreads)

function(nng_require_openssl3_for_pkcs11_provider)
    if (NOT NNG_REQUIRE_PKCS11_PROVIDER)
        return()
    endif ()

    set(NNG_OPENSSL_VERSION "")
    foreach(_version_var
        OpenSSL_VERSION
        OPENSSL_VERSION
        OpenSSL_VERSION_STRING
        OPENSSL_VERSION_STRING)
        if (DEFINED ${_version_var} AND
            NOT "${${_version_var}}" STREQUAL "")
            set(NNG_OPENSSL_VERSION "${${_version_var}}")
            break()
        endif ()
    endforeach ()

    if (NNG_OPENSSL_VERSION STREQUAL "")
        foreach(_openssl_target OpenSSL::SSL OpenSSL::Crypto openssl)
            if (TARGET ${_openssl_target})
                foreach(_version_prop VERSION OpenSSL_VERSION OPENSSL_VERSION)
                    get_target_property(NNG_OPENSSL_VERSION
                        ${_openssl_target} ${_version_prop})
                    if (NOT NNG_OPENSSL_VERSION STREQUAL
                        "NNG_OPENSSL_VERSION-NOTFOUND" AND
                        NOT "${NNG_OPENSSL_VERSION}" STREQUAL "")
                        break()
                    endif ()
                    set(NNG_OPENSSL_VERSION "")
                endforeach ()
            endif ()
            if (NOT "${NNG_OPENSSL_VERSION}" STREQUAL "")
                break()
            endif ()
        endforeach ()
    endif ()

    if (NNG_OPENSSL_VERSION STREQUAL "")
        message(FATAL_ERROR
            "NNG_REQUIRE_PKCS11_PROVIDER could not determine the OpenSSL "
            "version at configure time.")
    endif ()
    if (NNG_OPENSSL_VERSION VERSION_LESS "3.0.0")
        message(FATAL_ERROR
            "NNG_REQUIRE_PKCS11_PROVIDER requires OpenSSL >= 3.0, found "
            "${NNG_OPENSSL_VERSION}.")
    endif ()
endfunction()

if (NNG_TLS_ENGINE STREQUAL "open")
    message(NOTICE "
        ************************************************************
        Linking against OpenSSL may change license terms.
        Consult a lawyer and the license files for details.
        ************************************************************")
    nng_sources(openssl.c)

    # If OpenSSL was added by a consuming project, then we should use that
    # instance of it, instead of configuring our own.
    if (TARGET openssl)
        nng_link_libraries(openssl)
    else()
        # We want to prefer config mode over our local find package.
        if (NOT (DEFINED CMAKE_FIND_PACKAGE_PREFER_CONFIG))
            set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)
            find_package(OpenSSL REQUIRED)
            unset(CMAKE_FIND_PACKAGE_PREFER_CONFIG)
        else()
            find_package(OpenSSL REQUIRED)
        endif()
        nng_link_libraries_public(OpenSSL::SSL OpenSSL::Crypto)
    endif()
    nng_require_openssl3_for_pkcs11_provider()

    nng_defines(NNG_TLS_ENGINE_INIT=nng_tls_engine_init_open)
    nng_defines(NNG_TLS_ENGINE_FINI=nng_tls_engine_fini_open)
    nng_defines(NNG_SUPP_TLS)
    nng_defines(NNG_TLS_ENGINE_OPENSSL)

    if (NNG_TEST_PKCS11_PROVIDER)
        nng_test(pkcs11_test)
    endif ()
endif ()
