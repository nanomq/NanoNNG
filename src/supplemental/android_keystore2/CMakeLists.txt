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

    # --- 编译适配代码 + AIDL 生成的 .cpp 文件 ---
    nng_sources(src/KeystoreAdapter.cpp)

    # 直接编译 AIDL 生成的 .cpp 文件替代预编译 .so
    # 优势: 跨 ABI 自动适配、二进制体积更小(链接器剥离未使用符号)、符合 AOSP 标准实践
    file(GLOB KEYSTORE2_AIDL_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/keystore2_cpp_out/src/android/system/keystore2/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/keystore2_cpp_out/src/android/hardware/security/keymint/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/keystore2_cpp_out/src/android/hardware/security/secureclock/*.cpp"
    )
    target_sources(nng PRIVATE ${KEYSTORE2_AIDL_SOURCES})

    # --- 链接从设备提取的 libbinder_ndk.so ---
    # libbinder_ndk.so 提供 AServiceManager_waitForService、ABinderProcess_startThreadPool
    # 等 platform API 符号，这些符号在 NDK r27c stub 中不存在，必须从设备 /system/lib64/ 提取
    # 根据 Android ABI 选择对应的库子目录 (arm64-v8a / x86_64)
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

    # 链接到 nng 目标 (AIDL 代码已直接编译，无需额外 .so)
    nng_link_libraries(binder_ndk log)

    # --- Keystore2 自测工具 (keystore2_selftest) ---
    add_executable(keystore2_selftest src/keystore2_selftest.cpp)
    set(BORINGSSL_SRC_DIR "${CMAKE_SOURCE_DIR}/nng/extern/msquic/submodules/openssl/boringssl")
    target_include_directories(keystore2_selftest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/keystore2_cpp_out/include
        ${CMAKE_CURRENT_SOURCE_DIR}/binder_headers/include_ndk
        ${CMAKE_CURRENT_SOURCE_DIR}/binder_headers/include_cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/binder_headers/include_platform
        ${BORINGSSL_SRC_DIR}/include
    )
    target_link_libraries(keystore2_selftest
        binder_ndk log crypto
    )
    # selftest 也需要编译 AIDL .cpp 文件 (与 nng 目标共享同一套源码)
    target_sources(keystore2_selftest PRIVATE ${KEYSTORE2_AIDL_SOURCES})

    # 16KB 页对齐 (Android 15+)
    add_link_options("-Wl,-z,max-page-size=16384")
endif()
