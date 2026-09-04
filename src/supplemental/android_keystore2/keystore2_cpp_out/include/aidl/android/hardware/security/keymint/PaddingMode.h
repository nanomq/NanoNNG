/*
 * This file is auto-generated.  DO NOT MODIFY.
 * Using: /Users/alvin/Library/Android/sdk/build-tools/35.0.0/aidl --lang=ndk --structured --stability vintf -I /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/secureclock/aidl -o /Users/alvin/Downloads/keystore2_cpp_out/src -h /Users/alvin/Downloads/keystore2_cpp_out/include /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl/android/hardware/security/keymint/PaddingMode.aidl
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
enum class PaddingMode : int32_t {
  NONE = 1,
  RSA_OAEP = 2,
  RSA_PSS = 3,
  RSA_PKCS1_1_5_ENCRYPT = 4,
  RSA_PKCS1_1_5_SIGN = 5,
  PKCS7 = 64,
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
[[nodiscard]] static inline std::string toString(PaddingMode val) {
  switch(val) {
  case PaddingMode::NONE:
    return "NONE";
  case PaddingMode::RSA_OAEP:
    return "RSA_OAEP";
  case PaddingMode::RSA_PSS:
    return "RSA_PSS";
  case PaddingMode::RSA_PKCS1_1_5_ENCRYPT:
    return "RSA_PKCS1_1_5_ENCRYPT";
  case PaddingMode::RSA_PKCS1_1_5_SIGN:
    return "RSA_PKCS1_1_5_SIGN";
  case PaddingMode::PKCS7:
    return "PKCS7";
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
constexpr inline std::array<aidl::android::hardware::security::keymint::PaddingMode, 6> enum_values<aidl::android::hardware::security::keymint::PaddingMode> = {
  aidl::android::hardware::security::keymint::PaddingMode::NONE,
  aidl::android::hardware::security::keymint::PaddingMode::RSA_OAEP,
  aidl::android::hardware::security::keymint::PaddingMode::RSA_PSS,
  aidl::android::hardware::security::keymint::PaddingMode::RSA_PKCS1_1_5_ENCRYPT,
  aidl::android::hardware::security::keymint::PaddingMode::RSA_PKCS1_1_5_SIGN,
  aidl::android::hardware::security::keymint::PaddingMode::PKCS7,
};
#pragma clang diagnostic pop
}  // namespace internal
}  // namespace ndk
