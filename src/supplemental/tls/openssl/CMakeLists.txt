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
        # BoringSSL 源码树必须显式指定（构建脚本按固定 SHA 拉取后传入）。
        # 不再回退到 msquic 嵌套的 2017 版库：那种库 TLS1.3 默认关闭且是
        # draft 线格式，能编过但握手必失败，这里直接硬失败。
        # 注意：外部 BoringSSL 源码保持 pristine，不在配置期改写其文件。
        set(NNG_BORINGSSL_SOURCE_DIR "" CACHE PATH
            "BoringSSL source tree used for Android Keystore2 mTLS")
        if(NOT NNG_BORINGSSL_SOURCE_DIR)
            message(FATAL_ERROR
                "Android Keystore2 build requires -DNNG_BORINGSSL_SOURCE_DIR=<BoringSSL source tree>")
        endif()
        if(NOT EXISTS "${NNG_BORINGSSL_SOURCE_DIR}/include/openssl/ssl.h")
            message(FATAL_ERROR
                "NNG_BORINGSSL_SOURCE_DIR='${NNG_BORINGSSL_SOURCE_DIR}' is not a BoringSSL source tree")
        endif()
        set(BORINGSSL_SRC_DIR "${NNG_BORINGSSL_SOURCE_DIR}")
        # 禁用 ASM 优化以避免 ARM AES/SHA 指令集兼容问题
        set(OPENSSL_NO_ASM ON CACHE BOOL "Disable BoringSSL assembly" FORCE)

        # BoringSSL 作为子目录编译时会继承本项目的告警开关，其自身的
        # -Werror 会把本项目开启的 -Wunused-parameter 变成错误。这里仅在
        # 子目录作用域内关闭该告警，不改写外部源码（保持 pristine）。
        set(NNG_SAVED_C_FLAGS "${CMAKE_C_FLAGS}")
        set(NNG_SAVED_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        string(APPEND CMAKE_C_FLAGS " -Wno-unused-parameter")
        string(APPEND CMAKE_CXX_FLAGS " -Wno-unused-parameter")
        add_subdirectory(${BORINGSSL_SRC_DIR} ${CMAKE_BINARY_DIR}/boringssl)
        set(CMAKE_C_FLAGS "${NNG_SAVED_C_FLAGS}")
        set(CMAKE_CXX_FLAGS "${NNG_SAVED_CXX_FLAGS}")

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
