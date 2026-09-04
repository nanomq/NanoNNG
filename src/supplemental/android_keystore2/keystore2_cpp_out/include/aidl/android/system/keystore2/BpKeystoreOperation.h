/*
 * This file is auto-generated.  DO NOT MODIFY.
 * Using: /Users/alvin/Library/Android/sdk/build-tools/35.0.0/aidl --lang=ndk --structured --stability vintf -I /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/secureclock/aidl -o /Users/alvin/Downloads/keystore2_cpp_out/src -h /Users/alvin/Downloads/keystore2_cpp_out/include /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl/android/system/keystore2/IKeystoreOperation.aidl
 */
#pragma once

#include "aidl/android/system/keystore2/IKeystoreOperation.h"

#include <android/binder_ibinder.h>

namespace aidl {
namespace android {
namespace system {
namespace keystore2 {
class BpKeystoreOperation : public ::ndk::BpCInterface<IKeystoreOperation> {
public:
  explicit BpKeystoreOperation(const ::ndk::SpAIBinder& binder);
  virtual ~BpKeystoreOperation();

  ::ndk::ScopedAStatus updateAad(const std::vector<uint8_t>& in_aadInput) override;
  ::ndk::ScopedAStatus update(const std::vector<uint8_t>& in_input, std::optional<std::vector<uint8_t>>* _aidl_return) override;
  ::ndk::ScopedAStatus finish(const std::optional<std::vector<uint8_t>>& in_input, const std::optional<std::vector<uint8_t>>& in_signature, std::optional<std::vector<uint8_t>>* _aidl_return) override;
  ::ndk::ScopedAStatus abort() override;
};
}  // namespace keystore2
}  // namespace system
}  // namespace android
}  // namespace aidl
