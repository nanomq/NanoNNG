/*
 * This file is auto-generated.  DO NOT MODIFY.
 * Using: /Users/alvin/Library/Android/sdk/build-tools/35.0.0/aidl --lang=ndk --structured --stability vintf -I /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/secureclock/aidl -o /Users/alvin/Downloads/keystore2_cpp_out/src -h /Users/alvin/Downloads/keystore2_cpp_out/include /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl/android/system/keystore2/AuthenticatorSpec.aidl
 */
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <android/binder_interface_utils.h>
#include <android/binder_parcelable_utils.h>
#include <android/binder_to_string.h>
#include <aidl/android/hardware/security/keymint/HardwareAuthenticatorType.h>
#ifdef BINDER_STABILITY_SUPPORT
#include <android/binder_stability.h>
#endif  // BINDER_STABILITY_SUPPORT

namespace aidl {
namespace android {
namespace system {
namespace keystore2 {
class AuthenticatorSpec {
public:
  typedef std::false_type fixed_size;
  static const char* descriptor;

  ::aidl::android::hardware::security::keymint::HardwareAuthenticatorType authenticatorType = ::aidl::android::hardware::security::keymint::HardwareAuthenticatorType::NONE;
  int64_t authenticatorId = 0L;

  binder_status_t readFromParcel(const AParcel* parcel);
  binder_status_t writeToParcel(AParcel* parcel) const;

  inline bool operator==(const AuthenticatorSpec& _rhs) const {
    return std::tie(authenticatorType, authenticatorId) == std::tie(_rhs.authenticatorType, _rhs.authenticatorId);
  }
  inline bool operator<(const AuthenticatorSpec& _rhs) const {
    return std::tie(authenticatorType, authenticatorId) < std::tie(_rhs.authenticatorType, _rhs.authenticatorId);
  }
  inline bool operator!=(const AuthenticatorSpec& _rhs) const {
    return !(*this == _rhs);
  }
  inline bool operator>(const AuthenticatorSpec& _rhs) const {
    return _rhs < *this;
  }
  inline bool operator>=(const AuthenticatorSpec& _rhs) const {
    return !(*this < _rhs);
  }
  inline bool operator<=(const AuthenticatorSpec& _rhs) const {
    return !(_rhs < *this);
  }

  static const ::ndk::parcelable_stability_t _aidl_stability = ::ndk::STABILITY_VINTF;
  inline std::string toString() const {
    std::ostringstream _aidl_os;
    _aidl_os << "AuthenticatorSpec{";
    _aidl_os << "authenticatorType: " << ::android::internal::ToString(authenticatorType);
    _aidl_os << ", authenticatorId: " << ::android::internal::ToString(authenticatorId);
    _aidl_os << "}";
    return _aidl_os.str();
  }
};
}  // namespace keystore2
}  // namespace system
}  // namespace android
}  // namespace aidl
