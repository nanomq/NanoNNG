/*
 * This file is auto-generated.  DO NOT MODIFY.
 * Using: /Users/alvin/Library/Android/sdk/build-tools/35.0.0/aidl --lang=ndk --structured --stability vintf -I /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/secureclock/aidl -o /Users/alvin/Downloads/keystore2_cpp_out/src -h /Users/alvin/Downloads/keystore2_cpp_out/include /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl/android/hardware/security/keymint/KeyCharacteristics.aidl
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
#include <aidl/android/hardware/security/keymint/KeyParameter.h>
#include <aidl/android/hardware/security/keymint/SecurityLevel.h>
#ifdef BINDER_STABILITY_SUPPORT
#include <android/binder_stability.h>
#endif  // BINDER_STABILITY_SUPPORT

namespace aidl::android::hardware::security::keymint {
class KeyParameter;
}  // namespace aidl::android::hardware::security::keymint
namespace aidl {
namespace android {
namespace hardware {
namespace security {
namespace keymint {
class KeyCharacteristics {
public:
  typedef std::false_type fixed_size;
  static const char* descriptor;

  ::aidl::android::hardware::security::keymint::SecurityLevel securityLevel = ::aidl::android::hardware::security::keymint::SecurityLevel::SOFTWARE;
  std::vector<::aidl::android::hardware::security::keymint::KeyParameter> authorizations;

  binder_status_t readFromParcel(const AParcel* parcel);
  binder_status_t writeToParcel(AParcel* parcel) const;

  inline bool operator==(const KeyCharacteristics& _rhs) const {
    return std::tie(securityLevel, authorizations) == std::tie(_rhs.securityLevel, _rhs.authorizations);
  }
  inline bool operator<(const KeyCharacteristics& _rhs) const {
    return std::tie(securityLevel, authorizations) < std::tie(_rhs.securityLevel, _rhs.authorizations);
  }
  inline bool operator!=(const KeyCharacteristics& _rhs) const {
    return !(*this == _rhs);
  }
  inline bool operator>(const KeyCharacteristics& _rhs) const {
    return _rhs < *this;
  }
  inline bool operator>=(const KeyCharacteristics& _rhs) const {
    return !(*this < _rhs);
  }
  inline bool operator<=(const KeyCharacteristics& _rhs) const {
    return !(_rhs < *this);
  }

  static const ::ndk::parcelable_stability_t _aidl_stability = ::ndk::STABILITY_VINTF;
  inline std::string toString() const {
    std::ostringstream _aidl_os;
    _aidl_os << "KeyCharacteristics{";
    _aidl_os << "securityLevel: " << ::android::internal::ToString(securityLevel);
    _aidl_os << ", authorizations: " << ::android::internal::ToString(authorizations);
    _aidl_os << "}";
    return _aidl_os.str();
  }
};
}  // namespace keymint
}  // namespace security
}  // namespace hardware
}  // namespace android
}  // namespace aidl
