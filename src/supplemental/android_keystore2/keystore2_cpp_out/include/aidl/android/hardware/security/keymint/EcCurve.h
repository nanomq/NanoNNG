/*
 * This file is auto-generated.  DO NOT MODIFY.
 * Using: /Users/alvin/Library/Android/sdk/build-tools/35.0.0/aidl --lang=ndk --structured --stability vintf -I /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/secureclock/aidl -o /Users/alvin/Downloads/keystore2_cpp_out/src -h /Users/alvin/Downloads/keystore2_cpp_out/include /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl/android/hardware/security/keymint/EcCurve.aidl
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
enum class EcCurve : int32_t {
  P_224 = 0,
  P_256 = 1,
  P_384 = 2,
  P_521 = 3,
  CURVE_25519 = 4,
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
[[nodiscard]] static inline std::string toString(EcCurve val) {
  switch(val) {
  case EcCurve::P_224:
    return "P_224";
  case EcCurve::P_256:
    return "P_256";
  case EcCurve::P_384:
    return "P_384";
  case EcCurve::P_521:
    return "P_521";
  case EcCurve::CURVE_25519:
    return "CURVE_25519";
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
constexpr inline std::array<aidl::android::hardware::security::keymint::EcCurve, 5> enum_values<aidl::android::hardware::security::keymint::EcCurve> = {
  aidl::android::hardware::security::keymint::EcCurve::P_224,
  aidl::android::hardware::security::keymint::EcCurve::P_256,
  aidl::android::hardware::security::keymint::EcCurve::P_384,
  aidl::android::hardware::security::keymint::EcCurve::P_521,
  aidl::android::hardware::security::keymint::EcCurve::CURVE_25519,
};
#pragma clang diagnostic pop
}  // namespace internal
}  // namespace ndk
