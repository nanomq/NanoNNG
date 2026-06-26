/*
 * This file is auto-generated.  DO NOT MODIFY.
 * Using: /Users/alvin/Library/Android/sdk/build-tools/35.0.0/aidl --lang=ndk --structured --stability vintf -I /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/secureclock/aidl -o /Users/alvin/Downloads/keystore2_cpp_out/src -h /Users/alvin/Downloads/keystore2_cpp_out/include /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl/android/hardware/security/keymint/BlockMode.aidl
 */
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <android/binder_enums.h>
#ifdef BINDER_STABILITY_SUPPORT
#include <android/binder_stability.h>
#endif  // BINDER_STABILITY_SUPPORT

namespace aidl {
namespace android {
namespace hardware {
namespace security {
namespace keymint {
enum class BlockMode : int32_t {
  ECB = 1,
  CBC = 2,
  CTR = 3,
  GCM = 32,
};

}  // namespace keymint
}  // namespace security
}  // namespace hardware
}  // namespace android
}  // namespace aidl
namespace aidl {
namespace android {
namespace hardware {
namespace security {
namespace keymint {
[[nodiscard]] static inline std::string toString(BlockMode val) {
  switch(val) {
  case BlockMode::ECB:
    return "ECB";
  case BlockMode::CBC:
    return "CBC";
  case BlockMode::CTR:
    return "CTR";
  case BlockMode::GCM:
    return "GCM";
  default:
    return std::to_string(static_cast<int32_t>(val));
  }
}
}  // namespace keymint
}  // namespace security
}  // namespace hardware
}  // namespace android
}  // namespace aidl
namespace ndk {
namespace internal {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc++17-extensions"
template <>
constexpr inline std::array<aidl::android::hardware::security::keymint::BlockMode, 4> enum_values<aidl::android::hardware::security::keymint::BlockMode> = {
  aidl::android::hardware::security::keymint::BlockMode::ECB,
  aidl::android::hardware::security::keymint::BlockMode::CBC,
  aidl::android::hardware::security::keymint::BlockMode::CTR,
  aidl::android::hardware::security::keymint::BlockMode::GCM,
};
#pragma clang diagnostic pop
}  // namespace internal
}  // namespace ndk
