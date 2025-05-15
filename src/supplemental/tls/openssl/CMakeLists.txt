include(FindThreads)

if(TLS_EXTERN_PRIVATE_KEY)
    nng_defines(TLS_EXTERN_PRIVATE_KEY)
    message(NOTICE "TLS_EXTERN_PRIVATE_KEY IS ENABLED")
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
            if(TLS_EXTERN_PRIVATE_KEY_8155)
                message(NOTICE "TLS_EXTERN_PRIVATE_KEY_8155 IS ENABLED")
                nng_link_libraries(csmw_crypto csmw_ssl csmw csmw_geelypki)
            else()
                message(NOTICE "TLS_EXTERN_PRIVATE_KEY_other IS ENABLED")
                nng_link_libraries(csmw_crypto csmw_ssl csmw csmw_desaypki)
            endif()
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
            #nng_link_libraries_public(/home/wangha/Downloads/android-ndk-r23c/toolchains/llvm/prebuilt/linux-x86_64/sysroot/lib/libcsmw_crypto.so /home/wangha/Downloads/android-ndk-r23c/toolchains/llvm/prebuilt/linux-x86_64/sysroot/lib/libcsmw_ssl.so /home/wangha/Downloads/android-ndk-r23c/toolchains/llvm/prebuilt/linux-x86_64/sysroot/lib/libcsmw.so /home/wangha/Downloads/android-ndk-r23c/toolchains/llvm/prebuilt/linux-x86_64/sysroot/lib/libcsmw_desaypki.so)
            if(TLS_EXTERN_PRIVATE_KEY_8155)
                message(NOTICE "TLS_EXTERN_PRIVATE_KEY_8155 IS ENABLED")
                nng_link_libraries_public(csmw_crypto csmw_ssl csmw csmw_geelypki)
            else()
                message(NOTICE "TLS_EXTERN_PRIVATE_KEY_other IS ENABLED")
                nng_link_libraries_public(csmw_crypto csmw_ssl csmw csmw_desaypki)
            endif()
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
