/*
 * This file is auto-generated.  DO NOT MODIFY.
 * Using: /Users/alvin/Library/Android/sdk/build-tools/35.0.0/aidl --lang=ndk --structured --stability vintf -I /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/secureclock/aidl -o /Users/alvin/Downloads/keystore2_cpp_out/src -h /Users/alvin/Downloads/keystore2_cpp_out/include /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl/android/hardware/security/keymint/IKeyMintOperation.aidl
 */
#pragma once

#include "aidl/android/hardware/security/keymint/IKeyMintOperation.h"

#include <android/binder_ibinder.h>

namespace aidl {
namespace android {
namespace hardware {
namespace security {
namespace keymint {
class BpKeyMintOperation : public ::ndk::BpCInterface<IKeyMintOperation> {
public:
  explicit BpKeyMintOperation(const ::ndk::SpAIBinder& binder);
  virtual ~BpKeyMintOperation();

  ::ndk::ScopedAStatus updateAad(const std::vector<uint8_t>& in_input, const std::optional<::aidl::android::hardware::security::keymint::HardwareAuthToken>& in_authToken, const std::optional<::aidl::android::hardware::security::secureclock::TimeStampToken>& in_timeStampToken) override;
  ::ndk::ScopedAStatus update(const std::vector<uint8_t>& in_input, const std::optional<::aidl::android::hardware::security::keymint::HardwareAuthToken>& in_authToken, const std::optional<::aidl::android::hardware::security::secureclock::TimeStampToken>& in_timeStampToken, std::vector<uint8_t>* _aidl_return) override;
  ::ndk::ScopedAStatus finish(const std::optional<std::vector<uint8_t>>& in_input, const std::optional<std::vector<uint8_t>>& in_signature, const std::optional<::aidl::android::hardware::security::keymint::HardwareAuthToken>& in_authToken, const std::optional<::aidl::android::hardware::security::secureclock::TimeStampToken>& in_timestampToken, const std::optional<std::vector<uint8_t>>& in_confirmationToken, std::vector<uint8_t>* _aidl_return) override;
  ::ndk::ScopedAStatus abort() override;
};
}  // namespace keymint
}  // namespace security
}  // namespace hardware
}  // namespace android
}  // namespace aidl
