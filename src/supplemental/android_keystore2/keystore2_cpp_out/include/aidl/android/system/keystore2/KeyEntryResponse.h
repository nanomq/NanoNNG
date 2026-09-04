/*
 * This file is auto-generated.  DO NOT MODIFY.
 * Using: /Users/alvin/Library/Android/sdk/build-tools/35.0.0/aidl --lang=ndk --structured --stability vintf -I /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/secureclock/aidl -o /Users/alvin/Downloads/keystore2_cpp_out/src -h /Users/alvin/Downloads/keystore2_cpp_out/include /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl/android/system/keystore2/KeyEntryResponse.aidl
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
#include <aidl/android/system/keystore2/IKeystoreSecurityLevel.h>
#include <aidl/android/system/keystore2/KeyMetadata.h>
#ifdef BINDER_STABILITY_SUPPORT
#include <android/binder_stability.h>
#endif  // BINDER_STABILITY_SUPPORT

namespace aidl::android::system::keystore2 {
class IKeystoreSecurityLevel;
class KeyMetadata;
}  // namespace aidl::android::system::keystore2
namespace aidl {
namespace android {
namespace system {
namespace keystore2 {
class KeyEntryResponse {
public:
  typedef std::false_type fixed_size;
  static const char* descriptor;

  std::shared_ptr<::aidl::android::system::keystore2::IKeystoreSecurityLevel> iSecurityLevel;
  ::aidl::android::system::keystore2::KeyMetadata metadata;

  binder_status_t readFromParcel(const AParcel* parcel);
  binder_status_t writeToParcel(AParcel* parcel) const;

  inline bool operator==(const KeyEntryResponse& _rhs) const {
    return std::tie(iSecurityLevel, metadata) == std::tie(_rhs.iSecurityLevel, _rhs.metadata);
  }
  inline bool operator<(const KeyEntryResponse& _rhs) const {
    return std::tie(iSecurityLevel, metadata) < std::tie(_rhs.iSecurityLevel, _rhs.metadata);
  }
  inline bool operator!=(const KeyEntryResponse& _rhs) const {
    return !(*this == _rhs);
  }
  inline bool operator>(const KeyEntryResponse& _rhs) const {
    return _rhs < *this;
  }
  inline bool operator>=(const KeyEntryResponse& _rhs) const {
    return !(*this < _rhs);
  }
  inline bool operator<=(const KeyEntryResponse& _rhs) const {
    return !(_rhs < *this);
  }

  static const ::ndk::parcelable_stability_t _aidl_stability = ::ndk::STABILITY_VINTF;
  inline std::string toString() const {
    std::ostringstream _aidl_os;
    _aidl_os << "KeyEntryResponse{";
    _aidl_os << "iSecurityLevel: " << ::android::internal::ToString(iSecurityLevel);
    _aidl_os << ", metadata: " << ::android::internal::ToString(metadata);
    _aidl_os << "}";
    return _aidl_os.str();
  }
};
}  // namespace keystore2
}  // namespace system
}  // namespace android
}  // namespace aidl
