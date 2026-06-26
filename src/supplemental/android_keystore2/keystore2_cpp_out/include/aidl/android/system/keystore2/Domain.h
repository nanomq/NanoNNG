/*
 * This file is auto-generated.  DO NOT MODIFY.
 * Using: /Users/alvin/Library/Android/sdk/build-tools/35.0.0/aidl --lang=ndk --structured --stability vintf -I /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/secureclock/aidl -o /Users/alvin/Downloads/keystore2_cpp_out/src -h /Users/alvin/Downloads/keystore2_cpp_out/include /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl/android/system/keystore2/Domain.aidl
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
namespace system {
namespace keystore2 {
enum class Domain : int32_t {
  APP = 0,
  GRANT = 1,
  SELINUX = 2,
  BLOB = 3,
  KEY_ID = 4,
};

}  // namespace keystore2
}  // namespace system
}  // namespace android
}  // namespace aidl
namespace aidl {
namespace android {
namespace system {
namespace keystore2 {
[[nodiscard]] static inline std::string toString(Domain val) {
  switch(val) {
  case Domain::APP:
    return "APP";
  case Domain::GRANT:
    return "GRANT";
  case Domain::SELINUX:
    return "SELINUX";
  case Domain::BLOB:
    return "BLOB";
  case Domain::KEY_ID:
    return "KEY_ID";
  default:
    return std::to_string(static_cast<int32_t>(val));
  }
}
}  // namespace keystore2
}  // namespace system
}  // namespace android
}  // namespace aidl
namespace ndk {
namespace internal {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc++17-extensions"
template <>
constexpr inline std::array<aidl::android::system::keystore2::Domain, 5> enum_values<aidl::android::system::keystore2::Domain> = {
  aidl::android::system::keystore2::Domain::APP,
  aidl::android::system::keystore2::Domain::GRANT,
  aidl::android::system::keystore2::Domain::SELINUX,
  aidl::android::system::keystore2::Domain::BLOB,
  aidl::android::system::keystore2::Domain::KEY_ID,
};
#pragma clang diagnostic pop
}  // namespace internal
}  // namespace ndk
