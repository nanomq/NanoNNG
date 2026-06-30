/*
 * This file is auto-generated.  DO NOT MODIFY.
 * Using: /Users/alvin/Library/Android/sdk/build-tools/35.0.0/aidl --lang=ndk --structured --stability vintf -I /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/secureclock/aidl -o /Users/alvin/Downloads/keystore2_cpp_out/src -h /Users/alvin/Downloads/keystore2_cpp_out/include /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl/android/hardware/security/keymint/IKeyMintOperation.aidl
 */
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <android/binder_ibinder_platform.h>
#include <android/binder_interface_utils.h>
#include <android/binder_parcel_platform.h>
#include <aidl/android/hardware/security/keymint/HardwareAuthToken.h>
#include <aidl/android/hardware/security/secureclock/TimeStampToken.h>
#ifdef BINDER_STABILITY_SUPPORT
#include <android/binder_stability.h>
#endif  // BINDER_STABILITY_SUPPORT

namespace aidl::android::hardware::security::keymint {
class HardwareAuthToken;
}  // namespace aidl::android::hardware::security::keymint
namespace aidl::android::hardware::security::secureclock {
class TimeStampToken;
}  // namespace aidl::android::hardware::security::secureclock
namespace aidl {
namespace android {
namespace hardware {
namespace security {
namespace keymint {
class IKeyMintOperationDelegator;

class IKeyMintOperation : public ::ndk::ICInterface {
public:
  typedef IKeyMintOperationDelegator DefaultDelegator;
  static const char* descriptor;
  IKeyMintOperation();
  virtual ~IKeyMintOperation();

  static constexpr uint32_t TRANSACTION_updateAad = FIRST_CALL_TRANSACTION + 0;
  static constexpr uint32_t TRANSACTION_update = FIRST_CALL_TRANSACTION + 1;
  static constexpr uint32_t TRANSACTION_finish = FIRST_CALL_TRANSACTION + 2;
  static constexpr uint32_t TRANSACTION_abort = FIRST_CALL_TRANSACTION + 3;

  static std::shared_ptr<IKeyMintOperation> fromBinder(const ::ndk::SpAIBinder& binder);
  static binder_status_t writeToParcel(AParcel* parcel, const std::shared_ptr<IKeyMintOperation>& instance);
  static binder_status_t readFromParcel(const AParcel* parcel, std::shared_ptr<IKeyMintOperation>* instance);
  static bool setDefaultImpl(const std::shared_ptr<IKeyMintOperation>& impl);
  static const std::shared_ptr<IKeyMintOperation>& getDefaultImpl();
  virtual ::ndk::ScopedAStatus updateAad(const std::vector<uint8_t>& in_input, const std::optional<::aidl::android::hardware::security::keymint::HardwareAuthToken>& in_authToken, const std::optional<::aidl::android::hardware::security::secureclock::TimeStampToken>& in_timeStampToken) = 0;
  virtual ::ndk::ScopedAStatus update(const std::vector<uint8_t>& in_input, const std::optional<::aidl::android::hardware::security::keymint::HardwareAuthToken>& in_authToken, const std::optional<::aidl::android::hardware::security::secureclock::TimeStampToken>& in_timeStampToken, std::vector<uint8_t>* _aidl_return) = 0;
  virtual ::ndk::ScopedAStatus finish(const std::optional<std::vector<uint8_t>>& in_input, const std::optional<std::vector<uint8_t>>& in_signature, const std::optional<::aidl::android::hardware::security::keymint::HardwareAuthToken>& in_authToken, const std::optional<::aidl::android::hardware::security::secureclock::TimeStampToken>& in_timestampToken, const std::optional<std::vector<uint8_t>>& in_confirmationToken, std::vector<uint8_t>* _aidl_return) = 0;
  virtual ::ndk::ScopedAStatus abort() = 0;
private:
  static std::shared_ptr<IKeyMintOperation> default_impl;
};
class IKeyMintOperationDefault : public IKeyMintOperation {
public:
  ::ndk::ScopedAStatus updateAad(const std::vector<uint8_t>& in_input, const std::optional<::aidl::android::hardware::security::keymint::HardwareAuthToken>& in_authToken, const std::optional<::aidl::android::hardware::security::secureclock::TimeStampToken>& in_timeStampToken) override;
  ::ndk::ScopedAStatus update(const std::vector<uint8_t>& in_input, const std::optional<::aidl::android::hardware::security::keymint::HardwareAuthToken>& in_authToken, const std::optional<::aidl::android::hardware::security::secureclock::TimeStampToken>& in_timeStampToken, std::vector<uint8_t>* _aidl_return) override;
  ::ndk::ScopedAStatus finish(const std::optional<std::vector<uint8_t>>& in_input, const std::optional<std::vector<uint8_t>>& in_signature, const std::optional<::aidl::android::hardware::security::keymint::HardwareAuthToken>& in_authToken, const std::optional<::aidl::android::hardware::security::secureclock::TimeStampToken>& in_timestampToken, const std::optional<std::vector<uint8_t>>& in_confirmationToken, std::vector<uint8_t>* _aidl_return) override;
  ::ndk::ScopedAStatus abort() override;
  ::ndk::SpAIBinder asBinder() override;
  bool isRemote() override;
};
}  // namespace keymint
}  // namespace security
}  // namespace hardware
}  // namespace android
}  // namespace aidl
