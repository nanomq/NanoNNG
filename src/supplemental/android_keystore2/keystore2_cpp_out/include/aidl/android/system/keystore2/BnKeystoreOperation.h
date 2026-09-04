/*
 * This file is auto-generated.  DO NOT MODIFY.
 * Using: /Users/alvin/Library/Android/sdk/build-tools/35.0.0/aidl --lang=ndk --structured --stability vintf -I /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/secureclock/aidl -o /Users/alvin/Downloads/keystore2_cpp_out/src -h /Users/alvin/Downloads/keystore2_cpp_out/include /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl/android/system/keystore2/IKeystoreOperation.aidl
 */
#pragma once

#include "aidl/android/system/keystore2/IKeystoreOperation.h"

#include <android/binder_ibinder.h>
#include <cassert>

#ifndef __BIONIC__
#ifndef __assert2
#define __assert2(a,b,c,d) ((void)0)
#endif
#endif

namespace aidl {
namespace android {
namespace system {
namespace keystore2 {
class BnKeystoreOperation : public ::ndk::BnCInterface<IKeystoreOperation> {
public:
  BnKeystoreOperation();
  virtual ~BnKeystoreOperation();
protected:
  ::ndk::SpAIBinder createBinder() override;
private:
};
class IKeystoreOperationDelegator : public BnKeystoreOperation {
public:
  explicit IKeystoreOperationDelegator(const std::shared_ptr<IKeystoreOperation> &impl) : _impl(impl) {
  }

  ::ndk::ScopedAStatus updateAad(const std::vector<uint8_t>& in_aadInput) override {
    return _impl->updateAad(in_aadInput);
  }
  ::ndk::ScopedAStatus update(const std::vector<uint8_t>& in_input, std::optional<std::vector<uint8_t>>* _aidl_return) override {
    return _impl->update(in_input, _aidl_return);
  }
  ::ndk::ScopedAStatus finish(const std::optional<std::vector<uint8_t>>& in_input, const std::optional<std::vector<uint8_t>>& in_signature, std::optional<std::vector<uint8_t>>* _aidl_return) override {
    return _impl->finish(in_input, in_signature, _aidl_return);
  }
  ::ndk::ScopedAStatus abort() override {
    return _impl->abort();
  }
protected:
private:
  std::shared_ptr<IKeystoreOperation> _impl;
};

}  // namespace keystore2
}  // namespace system
}  // namespace android
}  // namespace aidl
