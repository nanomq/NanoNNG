/*
 * This file is auto-generated.  DO NOT MODIFY.
 * Using: /Users/alvin/Library/Android/sdk/build-tools/35.0.0/aidl --lang=ndk --structured --stability vintf -I /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/secureclock/aidl -o /Users/alvin/Downloads/keystore2_cpp_out/src -h /Users/alvin/Downloads/keystore2_cpp_out/include /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl/android/hardware/security/keymint/KeyPurpose.aidl
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
enum class KeyPurpose : int32_t {
  ENCRYPT = 0,
  DECRYPT = 1,
  SIGN = 2,
  VERIFY = 3,
  WRAP_KEY = 5,
  AGREE_KEY = 6,
  ATTEST_KEY = 7,
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
[[nodiscard]] static inline std::string toString(KeyPurpose val) {
  switch(val) {
  case KeyPurpose::ENCRYPT:
    return "ENCRYPT";
  case KeyPurpose::DECRYPT:
    return "DECRYPT";
  case KeyPurpose::SIGN:
    return "SIGN";
  case KeyPurpose::VERIFY:
    return "VERIFY";
  case KeyPurpose::WRAP_KEY:
    return "WRAP_KEY";
  case KeyPurpose::AGREE_KEY:
    return "AGREE_KEY";
  case KeyPurpose::ATTEST_KEY:
    return "ATTEST_KEY";
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
constexpr inline std::array<aidl::android::hardware::security::keymint::KeyPurpose, 7> enum_values<aidl::android::hardware::security::keymint::KeyPurpose> = {
  aidl::android::hardware::security::keymint::KeyPurpose::ENCRYPT,
  aidl::android::hardware::security::keymint::KeyPurpose::DECRYPT,
  aidl::android::hardware::security::keymint::KeyPurpose::SIGN,
  aidl::android::hardware::security::keymint::KeyPurpose::VERIFY,
  aidl::android::hardware::security::keymint::KeyPurpose::WRAP_KEY,
  aidl::android::hardware::security::keymint::KeyPurpose::AGREE_KEY,
  aidl::android::hardware::security::keymint::KeyPurpose::ATTEST_KEY,
};
#pragma clang diagnostic pop
}  // namespace internal
}  // namespace ndk
