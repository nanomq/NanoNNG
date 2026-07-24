#!/bin/bash
# install-keystore2-sdk.sh
# 将 Keystore2 SDK (binder 头文件 + AIDL 生成代码 + libbinder_ndk.so) 安装到 NDK 路径
#
# 用法:
#   ./install-keystore2-sdk.sh [NDK_SYSROOT] [KEYSTORE2_SDK_DIR]
#
# 默认值:
#   NDK_SYSROOT       = /opt/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/sysroot
#   KEYSTORE2_SDK_DIR = 当前脚本所在目录 (即 android_keystore2/)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
NDK_SYSROOT="${1:-/opt/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/sysroot}"
SDK_DIR="${2:-$SCRIPT_DIR}"

CPP_DEST="$NDK_SYSROOT/usr/src/nanomq-keystore2"

INC_DEST="$NDK_SYSROOT/usr/include/android"
AIDL_DEST="$NDK_SYSROOT/usr/include/aidl"

echo "==> Installing Keystore2 SDK to NDK sysroot: $NDK_SYSROOT"

# 1. C++ binder wrapper headers (include_cpp) → sysroot android/
echo "  [1/5] C++ binder wrappers (include_cpp)"
for f in "$SDK_DIR/binder_headers/include_cpp/android/"*.h; do
    bn=$(basename "$f")
    if [ ! -f "$INC_DEST/$bn" ]; then
        cp -v "$f" "$INC_DEST/"
    fi
done

# 2. Platform binder headers (include_platform) — removed from NDK r27c
echo "  [2/5] Platform binder headers (include_platform)"
for f in "$SDK_DIR/binder_headers/include_platform/android/"*.h; do
    bn=$(basename "$f")
    if [ ! -f "$INC_DEST/$bn" ]; then
        cp -v "$f" "$INC_DEST/"
    fi
done

# 3. AIDL-generated Keystore2/KeyMint headers → sysroot aidl/
echo "  [3/5] AIDL headers (keystore2_cpp_out/include/aidl)"
mkdir -p "$AIDL_DEST"
cp -r "$SDK_DIR/keystore2_cpp_out/include/aidl/android" "$AIDL_DEST/"

# 4. AIDL-generated .cpp source files → NDK nanomq-keystore2-sdk/src/
echo "  [4/5] AIDL sources (keystore2_cpp_out/src)"
mkdir -p "$CPP_DEST"
cp -r "$SDK_DIR/keystore2_cpp_out/src/android" "$CPP_DEST/"

# 5. libbinder_ndk.so → sysroot lib/<abi>/
echo "  [5/5] libbinder_ndk.so"
for abi_dir in "$SDK_DIR/libs/"*/; do
    abi=$(basename "$abi_dir")
    case "$abi" in
        arm64-v8a)   ndk_abi="aarch64-linux-android" ;;
        x86_64)      ndk_abi="x86_64-linux-android" ;;
        armeabi-v7a) ndk_abi="arm-linux-androideabi" ;;
        *)           echo "  [skip] Unknown ABI: $abi"; continue ;;
    esac
    lib_dest="$NDK_SYSROOT/usr/lib/$ndk_abi"
    if [ -f "$abi_dir/libbinder_ndk.so" ]; then
        cp -v "$abi_dir/libbinder_ndk.so" "$lib_dest/"
    fi
done

echo ""
echo "==> Done! Keystore2 SDK installed."
echo ""
echo "  Headers:   $INC_DEST/"
echo "  AIDL:      $AIDL_DEST/"
echo "  Sources:   $CPP_DEST/"
echo "  Libraries: $NDK_SYSROOT/usr/lib/<abi>/libbinder_ndk.so"
echo ""
echo "  Build with:"
echo "    cmake .. \\"
echo "        -DENABLE_ANDROID_KEYSTORE2=ON \\"
echo "        -DNANOMQ_KEYSTORE2_SDK_SRC=$CPP_DEST"
