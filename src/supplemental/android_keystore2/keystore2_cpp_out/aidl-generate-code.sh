#!/bin/bash

# 开启严格模式：任何命令如果返回非 0 的退出状态码，脚本将立即终止
set -e 

# ==========================================
# 1. 定义核心工具和各种模块的包含路径
# ==========================================
AIDL_BIN="/Users/alvin/Library/Android/sdk/build-tools/35.0.0/aidl"

# AOSP 源文件路径 (请确保这些目录已被正确 clone 到本地)
SYS_INC="/Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl"
HW_INC="/Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl"
CLOCK_INC="/Users/alvin/Downloads/hardware_interfaces/security/secureclock/aidl"

# 生成的 C++ 代码输出路径
OUT_SRC="/Users/alvin/Downloads/keystore2_cpp_out/src"
OUT_INC="/Users/alvin/Downloads/keystore2_cpp_out/include"

# ==========================================
# 2. 准备输出目录
# ==========================================
echo "🛠️  正在清理并准备输出目录..."
mkdir -p "$OUT_SRC"
mkdir -p "$OUT_INC"

# ==========================================
# 3. 核心编译逻辑
# ==========================================
echo "🚀 开始跨模块联合编译 AIDL 接口..."

# 合并查找：同时在 Keystore2、KeyMint 和 SecureClock 三个目录下查找 .aidl，并剔除过期的 aidl_api 目录
for aidl_file in $(find "$SYS_INC" "$HW_INC" "$CLOCK_INC" -name "*.aidl" | grep -v "aidl_api"); do
    echo "  -> Compiling: $(basename "$aidl_file")"
    
    # 执行 NDK C++ 编译，注入所有的包含路径以解决跨模块依赖
    "$AIDL_BIN" \
      --lang=ndk \
      --structured \
      --stability vintf \
      -I "$SYS_INC" \
      -I "$HW_INC" \
      -I "$CLOCK_INC" \
      -o "$OUT_SRC" \
      -h "$OUT_INC" \
      "$aidl_file" || { 
        # Fail Fast：一旦某个文件编译失败，立即抛出致命错误并退出
        echo -e "\n❌ [FATAL] AIDL 编译崩溃！"
        echo "出错文件: $aidl_file"
        echo "请检查该文件内部的 import 依赖是否缺失。"
        exit 1
    }
done

# ==========================================
# 4. 完美收官
# ==========================================
echo -e "\n🎉 恭喜！Keystore2、KeyMint 与 SecureClock 的所有依赖已完美编译为 C++！"
echo "✅ 头文件目录: $OUT_INC"
echo "✅ 源文件目录: $OUT_SRC"