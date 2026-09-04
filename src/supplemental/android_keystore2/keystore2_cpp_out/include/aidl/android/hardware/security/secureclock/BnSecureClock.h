/*
 * This file is auto-generated.  DO NOT MODIFY.
 * Using: /Users/alvin/Library/Android/sdk/build-tools/35.0.0/aidl --lang=ndk --structured --stability vintf -I /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/secureclock/aidl -o /Users/alvin/Downloads/keystore2_cpp_out/src -h /Users/alvin/Downloads/keystore2_cpp_out/include /Users/alvin/Downloads/hardware_interfaces/security/secureclock/aidl/android/hardware/security/secureclock/ISecureClock.aidl
 */
#pragma once

#include "aidl/android/hardware/security/secureclock/ISecureClock.h"

#include <android/binder_ibinder.h>
#include <cassert>

#ifndef __BIONIC__
#ifndef __assert2
#define __assert2(a,b,c,d) ((void)0)
#endif
#endif

namespace aidl {
namespace android {
namespace hardware {
namespace security {
namespace secureclock {
class BnSecureClock : public ::ndk::BnCInterface<ISecureClock> {
public:
  BnSecureClock();
  virtual ~BnSecureClock();
protected:
  ::ndk::SpAIBinder createBinder() override;
private:
};
class ISecureClockDelegator : public BnSecureClock {
public:
  explicit ISecureClockDelegator(const std::shared_ptr<ISecureClock> &impl) : _impl(impl) {
  }

  ::ndk::ScopedAStatus generateTimeStamp(int64_t in_challenge, ::aidl::android::hardware::security::secureclock::TimeStampToken* _aidl_return) override {
    return _impl->generateTimeStamp(in_challenge, _aidl_return);
  }
protected:
private:
  std::shared_ptr<ISecureClock> _impl;
};

}  // namespace secureclock
}  // namespace security
}  // namespace hardware
}  // namespace android
}  // namespace aidl
