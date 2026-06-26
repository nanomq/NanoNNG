/*
 * This file is auto-generated.  DO NOT MODIFY.
 * Using: /Users/alvin/Library/Android/sdk/build-tools/35.0.0/aidl --lang=ndk --structured --stability vintf -I /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/secureclock/aidl -o /Users/alvin/Downloads/keystore2_cpp_out/src -h /Users/alvin/Downloads/keystore2_cpp_out/include /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl/android/hardware/security/keymint/BeginResult.aidl
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
#include <aidl/android/hardware/security/keymint/IKeyMintOperation.h>
#include <aidl/android/hardware/security/keymint/KeyParameter.h>
#ifdef BINDER_STABILITY_SUPPORT
#include <android/binder_stability.h>
#endif  // BINDER_STABILITY_SUPPORT

namespace aidl::android::hardware::security::keymint {
class IKeyMintOperation;
class KeyParameter;
}  // namespace aidl::android::hardware::security::keymint
namespace aidl {
namespace android {
namespace hardware {
namespace security {
namespace keymint {
class BeginResult {
public:
  typedef std::false_type fixed_size;
  static const char* descriptor;

  int64_t challenge = 0L;
  std::vector<::aidl::android::hardware::security::keymint::KeyParameter> params;
  std::shared_ptr<::aidl::android::hardware::security::keymint::IKeyMintOperation> operation;

  binder_status_t readFromParcel(const AParcel* parcel);
  binder_status_t writeToParcel(AParcel* parcel) const;

  inline bool operator==(const BeginResult& _rhs) const {
    return std::tie(challenge, params, operation) == std::tie(_rhs.challenge, _rhs.params, _rhs.operation);
  }
  inline bool operator<(const BeginResult& _rhs) const {
    return std::tie(challenge, params, operation) < std::tie(_rhs.challenge, _rhs.params, _rhs.operation);
  }
  inline bool operator!=(const BeginResult& _rhs) const {
    return !(*this == _rhs);
  }
  inline bool operator>(const BeginResult& _rhs) const {
    return _rhs < *this;
  }
  inline bool operator>=(const BeginResult& _rhs) const {
    return !(*this < _rhs);
  }
  inline bool operator<=(const BeginResult& _rhs) const {
    return !(_rhs < *this);
  }

  static const ::ndk::parcelable_stability_t _aidl_stability = ::ndk::STABILITY_VINTF;
  inline std::string toString() const {
    std::ostringstream _aidl_os;
    _aidl_os << "BeginResult{";
    _aidl_os << "challenge: " << ::android::internal::ToString(challenge);
    _aidl_os << ", params: " << ::android::internal::ToString(params);
    _aidl_os << ", operation: " << ::android::internal::ToString(operation);
    _aidl_os << "}";
    return _aidl_os.str();
  }
};
}  // namespace keymint
}  // namespace security
}  // namespace hardware
}  // namespace android
}  // namespace aidl
