include(FindThreads)

if(TLS_EXTERN_PRIVATE_KEY)
    nng_defines(TLS_EXTERN_PRIVATE_KEY)
    message(NOTICE "TLS_EXTERN_PRIVATE_KEY IS ENABLED")
endif(TLS_EXTERN_PRIVATE_KEY)

if(CORNEX_PKI)
    if(NOT NNG_TLS_ENGINE STREQUAL "open")
        message(FATAL_ERROR "CORNEX_PKI requires NNG_TLS_ENGINE=open (got '${NNG_TLS_ENGINE}')")
    endif()
    if(TLS_EXTERN_PRIVATE_KEY)
        message(FATAL_ERROR "CORNEX_PKI and TLS_EXTERN_PRIVATE_KEY are mutually exclusive")
    endif()
    if(NOT CORNEX_PKI_ROOT)
        set(CORNEX_PKI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../extern/cornex_pki")
    endif()
    if(NOT EXISTS "${CORNEX_PKI_ROOT}/lib/libcn_x509_proxy.so")
        message(FATAL_ERROR "Cornex PKI SDK not found under '${CORNEX_PKI_ROOT}' (expected lib/libcn_x509_proxy.so)")
    endif()
    message(NOTICE "CORNEX_PKI IS ENABLED (SDK root: ${CORNEX_PKI_ROOT})")
    nng_defines(TLS_EXTERN_CORNEX_PKI)
    nng_sources(cornex_pki.c)
    nng_sources(cornex_pki.h)
    nng_include_directories("${CORNEX_PKI_ROOT}/include")
    if(CORNEX_PKI_ENGINE_PATH)
        nng_defines(NANOMQ_CORNEX_PKI_ENGINE_PATH="${CORNEX_PKI_ENGINE_PATH}")
    endif()
    # 只链接证书管理库；libcn_x509_engine.so 由 cornex_pki.c 显式 dlopen（D8）
    nng_link_libraries_public(
        "${CORNEX_PKI_ROOT}/lib/libcn_x509_proxy.so"
        "${CORNEX_PKI_ROOT}/lib/libcn_x509_crypto.so"
        "${CORNEX_PKI_ROOT}/lib/libcn_x509_common.so")
    # 这些库引用 libsecureclient.so.1（TEE 客户端库，由整车平台提供，不随包）。
    # 链接期允许共享库的未解析符号，运行期由平台库满足。
    target_link_options(nng INTERFACE "-Wl,--allow-shlib-undefined")
    # 这些库引用了 libgcc 的软浮点助手（__multf3/__getf2）；用共享 libgcc 避免
    # "hidden symbol ... referenced by DSO" 链接错误。
    target_link_options(nng INTERFACE "-shared-libgcc")
endif(CORNEX_PKI)

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
