/*
 * This file is auto-generated.  DO NOT MODIFY.
 * Using: /Users/alvin/Library/Android/sdk/build-tools/35.0.0/aidl --lang=ndk --structured --stability vintf -I /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/secureclock/aidl -o /Users/alvin/Downloads/keystore2_cpp_out/src -h /Users/alvin/Downloads/keystore2_cpp_out/include /Users/alvin/Downloads/hardware_interfaces/security/secureclock/aidl/android/hardware/security/secureclock/TimeStampToken.aidl
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
#include <aidl/android/hardware/security/secureclock/Timestamp.h>
#ifdef BINDER_STABILITY_SUPPORT
#include <android/binder_stability.h>
#endif  // BINDER_STABILITY_SUPPORT

namespace aidl::android::hardware::security::secureclock {
class Timestamp;
}  // namespace aidl::android::hardware::security::secureclock
namespace aidl {
namespace android {
namespace hardware {
namespace security {
namespace secureclock {
class TimeStampToken {
public:
  typedef std::false_type fixed_size;
  static const char* descriptor;

  int64_t challenge = 0L;
  ::aidl::android::hardware::security::secureclock::Timestamp timestamp;
  std::vector<uint8_t> mac;

  binder_status_t readFromParcel(const AParcel* parcel);
  binder_status_t writeToParcel(AParcel* parcel) const;

  inline bool operator==(const TimeStampToken& _rhs) const {
    return std::tie(challenge, timestamp, mac) == std::tie(_rhs.challenge, _rhs.timestamp, _rhs.mac);
  }
  inline bool operator<(const TimeStampToken& _rhs) const {
    return std::tie(challenge, timestamp, mac) < std::tie(_rhs.challenge, _rhs.timestamp, _rhs.mac);
  }
  inline bool operator!=(const TimeStampToken& _rhs) const {
    return !(*this == _rhs);
  }
  inline bool operator>(const TimeStampToken& _rhs) const {
    return _rhs < *this;
  }
  inline bool operator>=(const TimeStampToken& _rhs) const {
    return !(*this < _rhs);
  }
  inline bool operator<=(const TimeStampToken& _rhs) const {
    return !(_rhs < *this);
  }

  static const ::ndk::parcelable_stability_t _aidl_stability = ::ndk::STABILITY_VINTF;
  inline std::string toString() const {
    std::ostringstream _aidl_os;
    _aidl_os << "TimeStampToken{";
    _aidl_os << "challenge: " << ::android::internal::ToString(challenge);
    _aidl_os << ", timestamp: " << ::android::internal::ToString(timestamp);
    _aidl_os << ", mac: " << ::android::internal::ToString(mac);
    _aidl_os << "}";
    return _aidl_os.str();
  }
};
}  // namespace secureclock
}  // namespace security
}  // namespace hardware
}  // namespace android
}  // namespace aidl
