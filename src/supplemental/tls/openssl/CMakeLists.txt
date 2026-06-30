include(FindThreads)

# Keystore2 依赖 BoringSSL 的 SSL_PRIVATE_KEY_METHOD，自动启用 TLS_EXTERN_PRIVATE_KEY
if(ENABLE_ANDROID_KEYSTORE2 AND ANDROID)
    set(TLS_EXTERN_PRIVATE_KEY ON)
    message(STATUS "Auto-enabling TLS_EXTERN_PRIVATE_KEY for Keystore2 mTLS")
endif()

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
        # tee.cc 仅 CSMW/Desay 路径需要，Keystore2 路径不需要
        if(NOT ENABLE_ANDROID_KEYSTORE2 OR NOT ANDROID)
            nng_sources(tee.cc)
        endif()
    else()
        nng_sources(openssl.c)
    endif()

    # If OpenSSL was added by a consuming project, then we should use that
    # instance of it, instead of configuring our own.
    if(ENABLE_ANDROID_KEYSTORE2 AND ANDROID)
        # --- 编译 BoringSSL 源码 ---
        # BoringSSL 使用 -Werror + 新版 Clang 会产生大量编译警告
        # 暂时 patch BoringSSL CMakeLists.txt 移除 -Werror
        # 禁用 ASM 优化以避免 ARM AES/SHA 指令集兼容问题
        set(BORINGSSL_SRC_DIR "${CMAKE_SOURCE_DIR}/nng/extern/msquic/submodules/openssl/boringssl")
        set(BORINGSSL_CMAKE "${BORINGSSL_SRC_DIR}/CMakeLists.txt")
        file(READ "${BORINGSSL_CMAKE}" BORINGSSL_CMAKE_ORIG)
        string(REPLACE "-Werror " "" BORINGSSL_CMAKE_PATCHED "${BORINGSSL_CMAKE_ORIG}")
        string(REPLACE "\"-Werror\"" "\"\"" BORINGSSL_CMAKE_PATCHED "${BORINGSSL_CMAKE_PATCHED}")
        file(WRITE "${BORINGSSL_CMAKE}" "${BORINGSSL_CMAKE_PATCHED}")
        set(OPENSSL_NO_ASM ON CACHE BOOL "Disable BoringSSL assembly" FORCE)

        add_subdirectory(${BORINGSSL_SRC_DIR} ${CMAKE_BINARY_DIR}/boringssl)

        # 恢复 BoringSSL CMakeLists.txt 原始内容
        file(WRITE "${BORINGSSL_CMAKE}" "${BORINGSSL_CMAKE_ORIG}")

        # 直接链接 BoringSSL 的 ssl 和 crypto 目标
        target_include_directories(nng PRIVATE ${BORINGSSL_SRC_DIR}/include)
        # (parquet 的 crypto 依赖由 nng/CMakeLists.txt 中 NNG_ENABLE_PARQUET 块统一处理)
        nng_link_libraries(ssl crypto)
        # 将 BoringSSL 目标加入 nng 导出集合，避免 install(EXPORT) 报错
        install(TARGETS ssl crypto EXPORT nng-target)
    elseif (TARGET openssl)
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
            if(TLS_EXTERN_PRIVATE_KEY_8155)
                message(NOTICE "TLS_EXTERN_PRIVATE_KEY_8155 IS ENABLED")
                nng_link_libraries_public(csmw_crypto csmw_ssl csmw csmw_geelypki)
            else()
                message(NOTICE "TLS_EXTERN_PRIVATE_KEY_other IS ENABLED")
                nng_link_libraries_public(csmw_crypto csmw_ssl csmw csmw_desaypki)
            endif()
        else()
            nng_link_libraries_public(OpenSSL::SSL OpenSSL::Crypto)
        endif()
    endif()

    nng_defines(NNG_TLS_ENGINE_INIT=nng_tls_engine_init_open)
    nng_defines(NNG_TLS_ENGINE_FINI=nng_tls_engine_fini_open)
    nng_defines(NNG_SUPP_TLS)
    nng_defines(NNG_TLS_ENGINE_OPENSSL)
endif ()
