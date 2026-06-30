/*
 * This file is auto-generated.  DO NOT MODIFY.
 * Using: /Users/alvin/Library/Android/sdk/build-tools/35.0.0/aidl --lang=ndk --structured --stability vintf -I /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/secureclock/aidl -o /Users/alvin/Downloads/keystore2_cpp_out/src -h /Users/alvin/Downloads/keystore2_cpp_out/include /Users/alvin/Downloads/hardware_interfaces/security/secureclock/aidl/android/hardware/security/secureclock/ISecureClock.aidl
 */
#pragma once

#include "aidl/android/hardware/security/secureclock/ISecureClock.h"

#include <android/binder_ibinder.h>

namespace aidl {
namespace android {
namespace hardware {
namespace security {
namespace secureclock {
class BpSecureClock : public ::ndk::BpCInterface<ISecureClock> {
public:
  explicit BpSecureClock(const ::ndk::SpAIBinder& binder);
  virtual ~BpSecureClock();

  ::ndk::ScopedAStatus generateTimeStamp(int64_t in_challenge, ::aidl::android::hardware::security::secureclock::TimeStampToken* _aidl_return) override;
};
}  // namespace secureclock
}  // namespace security
}  // namespace hardware
}  // namespace android
}  // namespace aidl
