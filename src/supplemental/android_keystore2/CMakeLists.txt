# 提供 CMake 选项供外部配置 (默认值用于模拟器测试，实车部署时按需覆盖)
set(NANOMQ_KEYSTORE2_ALIAS "ecu-client-certificate" CACHE STRING
    "Keystore2 certificate alias name")
set(NANOMQ_KEYSTORE2_NAMESPACE "-1" CACHE STRING
    "Keystore2 SELinux namespace (-1 for Domain::APP, e.g. 30000 for vehicle)")
option(KEYSTORE2_USE_DIGEST_NONE "Use DIGEST_NONE for TEE signing (V216 production, BoringSSL pre-hashes). OFF for emulator." ON)

if(ENABLE_ANDROID_KEYSTORE2 AND ANDROID)
    message(STATUS "Android Keystore2 mTLS feature is ENABLED")
    message(STATUS "  - Alias: ${NANOMQ_KEYSTORE2_ALIAS}")
    message(STATUS "  - Namespace: ${NANOMQ_KEYSTORE2_NAMESPACE}")
    message(STATUS "  - Digest NONE (V216): ${KEYSTORE2_USE_DIGEST_NONE}")

    # 注入 C/C++ 宏到 nng target (必须用 target_* 而非 add_compile_definitions,
    # 因为 add_compile_definitions 是目录级作用域，无法传递到 openssl/ 目录编译的 openssl.c)
    # ENABLE_ANDROID_KEYSTORE2 由顶层 CMakeLists.txt 统一定义
    target_compile_definitions(nng PRIVATE
        NANOMQ_KEYSTORE2_ALIAS="${NANOMQ_KEYSTORE2_ALIAS}"
        NANOMQ_KEYSTORE2_NAMESPACE=${NANOMQ_KEYSTORE2_NAMESPACE}
    )
    if(KEYSTORE2_USE_DIGEST_NONE)
        target_compile_definitions(nng PRIVATE KEYSTORE2_USE_DIGEST_NONE=1)
    endif()

    # --- 头文件包含目录 ---
    # Binder NDK 头文件来自 AOSP frameworks/native 仓库的 libs/binder/ndk/ 子目录
    # 已内置到 binder_headers/ 下（include_ndk, include_cpp, include_platform），无需外部依赖
    target_include_directories(nng PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/keystore2_cpp_out/include
        ${CMAKE_CURRENT_SOURCE_DIR}/binder_headers/include_ndk
        ${CMAKE_CURRENT_SOURCE_DIR}/binder_headers/include_cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/binder_headers/include_platform
    )

    # --- 只编译 KeystoreAdapter.cpp (AIDL 代码已预编译在 .so 中) ---
    nng_sources(src/KeystoreAdapter.cpp)

    # --- 链接从设备提取的预编译 .so 文件 ---
    # 三个 .so 都来自目标 Android 设备的 /system/lib64/，NDK stub 库不包含所需符号
    # 根据 Android ABI 选择对应的库子目录 (aarch64 / x86_64)
    if(ANDROID_ABI STREQUAL "arm64-v8a")
        set(KEYSTORE2_ABI_DIR "arm64-v8a")
    elseif(ANDROID_ABI STREQUAL "x86_64")
        set(KEYSTORE2_ABI_DIR "x86_64")
    else()
        message(FATAL_ERROR "Unsupported ANDROID_ABI: ${ANDROID_ABI} (supported: arm64-v8a, x86_64)")
    endif()
    set(KEYSTORE2_LIBS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/${KEYSTORE2_ABI_DIR}")
    message(STATUS "  - Android ABI: ${ANDROID_ABI} → ${KEYSTORE2_ABI_DIR}")
    message(STATUS "  - Libs dir: ${KEYSTORE2_LIBS_DIR}")

    # libbinder_ndk.so — 提供 AServiceManager_waitForService 等 platform API 符号
    add_library(binder_ndk SHARED IMPORTED)
    set_target_properties(binder_ndk PROPERTIES
        IMPORTED_LOCATION "${KEYSTORE2_LIBS_DIR}/libbinder_ndk.so"
    )

    # Keystore2 AIDL NDK 客户端库 (设备提取)
    add_library(keystore2_ndk SHARED IMPORTED)
    set_target_properties(keystore2_ndk PROPERTIES
        IMPORTED_LOCATION "${KEYSTORE2_LIBS_DIR}/android.system.keystore2-V4-ndk.so"
    )

    # KeyMint AIDL NDK 客户端库 (设备提取)
    add_library(keymint_ndk SHARED IMPORTED)
    set_target_properties(keymint_ndk PROPERTIES
        IMPORTED_LOCATION "${KEYSTORE2_LIBS_DIR}/android.hardware.security.keymint-V3-ndk.so"
    )

    # 链接到 nng 目标
    nng_link_libraries(binder_ndk keystore2_ndk keymint_ndk log)

    # --- Keystore2 自测工具 (keystore2_selftest) ---
    add_executable(keystore2_selftest src/keystore2_selftest.cpp)
    target_include_directories(keystore2_selftest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/keystore2_cpp_out/include
        ${CMAKE_CURRENT_SOURCE_DIR}/binder_headers/include_ndk
        ${CMAKE_CURRENT_SOURCE_DIR}/binder_headers/include_cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/binder_headers/include_platform
    )
    target_link_libraries(keystore2_selftest
        binder_ndk keystore2_ndk keymint_ndk log
    )

    # 16KB 页对齐 (Android 15+)
    add_link_options("-Wl,-z,max-page-size=16384")
endif()
