/*
 * This file is auto-generated.  DO NOT MODIFY.
 * Using: /Users/alvin/Library/Android/sdk/build-tools/35.0.0/aidl --lang=ndk --structured --stability vintf -I /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/secureclock/aidl -o /Users/alvin/Downloads/keystore2_cpp_out/src -h /Users/alvin/Downloads/keystore2_cpp_out/include /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl/android/system/keystore2/CreateOperationResponse.aidl
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
#include <aidl/android/system/keystore2/IKeystoreOperation.h>
#include <aidl/android/system/keystore2/KeyParameters.h>
#include <aidl/android/system/keystore2/OperationChallenge.h>
#ifdef BINDER_STABILITY_SUPPORT
#include <android/binder_stability.h>
#endif  // BINDER_STABILITY_SUPPORT

namespace aidl::android::system::keystore2 {
class IKeystoreOperation;
class KeyParameters;
class OperationChallenge;
}  // namespace aidl::android::system::keystore2
namespace aidl {
namespace android {
namespace system {
namespace keystore2 {
class CreateOperationResponse {
public:
  typedef std::false_type fixed_size;
  static const char* descriptor;

  std::shared_ptr<::aidl::android::system::keystore2::IKeystoreOperation> iOperation;
  std::optional<::aidl::android::system::keystore2::OperationChallenge> operationChallenge;
  std::optional<::aidl::android::system::keystore2::KeyParameters> parameters;
  std::optional<std::vector<uint8_t>> upgradedBlob;

  binder_status_t readFromParcel(const AParcel* parcel);
  binder_status_t writeToParcel(AParcel* parcel) const;

  inline bool operator==(const CreateOperationResponse& _rhs) const {
    return std::tie(iOperation, operationChallenge, parameters, upgradedBlob) == std::tie(_rhs.iOperation, _rhs.operationChallenge, _rhs.parameters, _rhs.upgradedBlob);
  }
  inline bool operator<(const CreateOperationResponse& _rhs) const {
    return std::tie(iOperation, operationChallenge, parameters, upgradedBlob) < std::tie(_rhs.iOperation, _rhs.operationChallenge, _rhs.parameters, _rhs.upgradedBlob);
  }
  inline bool operator!=(const CreateOperationResponse& _rhs) const {
    return !(*this == _rhs);
  }
  inline bool operator>(const CreateOperationResponse& _rhs) const {
    return _rhs < *this;
  }
  inline bool operator>=(const CreateOperationResponse& _rhs) const {
    return !(*this < _rhs);
  }
  inline bool operator<=(const CreateOperationResponse& _rhs) const {
    return !(_rhs < *this);
  }

  static const ::ndk::parcelable_stability_t _aidl_stability = ::ndk::STABILITY_VINTF;
  inline std::string toString() const {
    std::ostringstream _aidl_os;
    _aidl_os << "CreateOperationResponse{";
    _aidl_os << "iOperation: " << ::android::internal::ToString(iOperation);
    _aidl_os << ", operationChallenge: " << ::android::internal::ToString(operationChallenge);
    _aidl_os << ", parameters: " << ::android::internal::ToString(parameters);
    _aidl_os << ", upgradedBlob: " << ::android::internal::ToString(upgradedBlob);
    _aidl_os << "}";
    return _aidl_os.str();
  }
};
}  // namespace keystore2
}  // namespace system
}  // namespace android
}  // namespace aidl
