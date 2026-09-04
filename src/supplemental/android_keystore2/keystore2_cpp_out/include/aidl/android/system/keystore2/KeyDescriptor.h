/*
 * This file is auto-generated.  DO NOT MODIFY.
 * Using: /Users/alvin/Library/Android/sdk/build-tools/35.0.0/aidl --lang=ndk --structured --stability vintf -I /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/secureclock/aidl -o /Users/alvin/Downloads/keystore2_cpp_out/src -h /Users/alvin/Downloads/keystore2_cpp_out/include /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl/android/system/keystore2/KeyDescriptor.aidl
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
#include <aidl/android/system/keystore2/Domain.h>
#ifdef BINDER_STABILITY_SUPPORT
#include <android/binder_stability.h>
#endif  // BINDER_STABILITY_SUPPORT

namespace aidl {
namespace android {
namespace system {
namespace keystore2 {
class KeyDescriptor {
public:
  typedef std::false_type fixed_size;
  static const char* descriptor;

  ::aidl::android::system::keystore2::Domain domain = ::aidl::android::system::keystore2::Domain::APP;
  int64_t nspace = 0L;
  std::optional<std::string> alias;
  std::optional<std::vector<uint8_t>> blob;

  binder_status_t readFromParcel(const AParcel* parcel);
  binder_status_t writeToParcel(AParcel* parcel) const;

  inline bool operator==(const KeyDescriptor& _rhs) const {
    return std::tie(domain, nspace, alias, blob) == std::tie(_rhs.domain, _rhs.nspace, _rhs.alias, _rhs.blob);
  }
  inline bool operator<(const KeyDescriptor& _rhs) const {
    return std::tie(domain, nspace, alias, blob) < std::tie(_rhs.domain, _rhs.nspace, _rhs.alias, _rhs.blob);
  }
  inline bool operator!=(const KeyDescriptor& _rhs) const {
    return !(*this == _rhs);
  }
  inline bool operator>(const KeyDescriptor& _rhs) const {
    return _rhs < *this;
  }
  inline bool operator>=(const KeyDescriptor& _rhs) const {
    return !(*this < _rhs);
  }
  inline bool operator<=(const KeyDescriptor& _rhs) const {
    return !(_rhs < *this);
  }

  static const ::ndk::parcelable_stability_t _aidl_stability = ::ndk::STABILITY_VINTF;
  inline std::string toString() const {
    std::ostringstream _aidl_os;
    _aidl_os << "KeyDescriptor{";
    _aidl_os << "domain: " << ::android::internal::ToString(domain);
    _aidl_os << ", nspace: " << ::android::internal::ToString(nspace);
    _aidl_os << ", alias: " << ::android::internal::ToString(alias);
    _aidl_os << ", blob: " << ::android::internal::ToString(blob);
    _aidl_os << "}";
    return _aidl_os.str();
  }
};
}  // namespace keystore2
}  // namespace system
}  // namespace android
}  // namespace aidl
