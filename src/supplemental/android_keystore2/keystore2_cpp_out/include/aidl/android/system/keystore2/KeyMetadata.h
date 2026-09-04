/*
 * This file is auto-generated.  DO NOT MODIFY.
 * Using: /Users/alvin/Library/Android/sdk/build-tools/35.0.0/aidl --lang=ndk --structured --stability vintf -I /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/secureclock/aidl -o /Users/alvin/Downloads/keystore2_cpp_out/src -h /Users/alvin/Downloads/keystore2_cpp_out/include /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl/android/system/keystore2/KeyMetadata.aidl
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
#include <aidl/android/hardware/security/keymint/SecurityLevel.h>
#include <aidl/android/system/keystore2/Authorization.h>
#include <aidl/android/system/keystore2/KeyDescriptor.h>
#ifdef BINDER_STABILITY_SUPPORT
#include <android/binder_stability.h>
#endif  // BINDER_STABILITY_SUPPORT

namespace aidl::android::system::keystore2 {
class Authorization;
class KeyDescriptor;
}  // namespace aidl::android::system::keystore2
namespace aidl {
namespace android {
namespace system {
namespace keystore2 {
class KeyMetadata {
public:
  typedef std::false_type fixed_size;
  static const char* descriptor;

  ::aidl::android::system::keystore2::KeyDescriptor key;
  ::aidl::android::hardware::security::keymint::SecurityLevel keySecurityLevel = ::aidl::android::hardware::security::keymint::SecurityLevel::SOFTWARE;
  std::vector<::aidl::android::system::keystore2::Authorization> authorizations;
  std::optional<std::vector<uint8_t>> certificate;
  std::optional<std::vector<uint8_t>> certificateChain;
  int64_t modificationTimeMs = 0L;

  binder_status_t readFromParcel(const AParcel* parcel);
  binder_status_t writeToParcel(AParcel* parcel) const;

  inline bool operator==(const KeyMetadata& _rhs) const {
    return std::tie(key, keySecurityLevel, authorizations, certificate, certificateChain, modificationTimeMs) == std::tie(_rhs.key, _rhs.keySecurityLevel, _rhs.authorizations, _rhs.certificate, _rhs.certificateChain, _rhs.modificationTimeMs);
  }
  inline bool operator<(const KeyMetadata& _rhs) const {
    return std::tie(key, keySecurityLevel, authorizations, certificate, certificateChain, modificationTimeMs) < std::tie(_rhs.key, _rhs.keySecurityLevel, _rhs.authorizations, _rhs.certificate, _rhs.certificateChain, _rhs.modificationTimeMs);
  }
  inline bool operator!=(const KeyMetadata& _rhs) const {
    return !(*this == _rhs);
  }
  inline bool operator>(const KeyMetadata& _rhs) const {
    return _rhs < *this;
  }
  inline bool operator>=(const KeyMetadata& _rhs) const {
    return !(*this < _rhs);
  }
  inline bool operator<=(const KeyMetadata& _rhs) const {
    return !(_rhs < *this);
  }

  static const ::ndk::parcelable_stability_t _aidl_stability = ::ndk::STABILITY_VINTF;
  inline std::string toString() const {
    std::ostringstream _aidl_os;
    _aidl_os << "KeyMetadata{";
    _aidl_os << "key: " << ::android::internal::ToString(key);
    _aidl_os << ", keySecurityLevel: " << ::android::internal::ToString(keySecurityLevel);
    _aidl_os << ", authorizations: " << ::android::internal::ToString(authorizations);
    _aidl_os << ", certificate: " << ::android::internal::ToString(certificate);
    _aidl_os << ", certificateChain: " << ::android::internal::ToString(certificateChain);
    _aidl_os << ", modificationTimeMs: " << ::android::internal::ToString(modificationTimeMs);
    _aidl_os << "}";
    return _aidl_os.str();
  }
};
}  // namespace keystore2
}  // namespace system
}  // namespace android
}  // namespace aidl
