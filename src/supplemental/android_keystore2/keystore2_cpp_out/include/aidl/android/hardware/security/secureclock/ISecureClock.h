/*
 * This file is auto-generated.  DO NOT MODIFY.
 * Using: /Users/alvin/Library/Android/sdk/build-tools/35.0.0/aidl --lang=ndk --structured --stability vintf -I /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/secureclock/aidl -o /Users/alvin/Downloads/keystore2_cpp_out/src -h /Users/alvin/Downloads/keystore2_cpp_out/include /Users/alvin/Downloads/hardware_interfaces/security/secureclock/aidl/android/hardware/security/secureclock/ISecureClock.aidl
 */
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <android/binder_interface_utils.h>
#include <aidl/android/hardware/security/secureclock/TimeStampToken.h>
#ifdef BINDER_STABILITY_SUPPORT
#include <android/binder_stability.h>
#endif  // BINDER_STABILITY_SUPPORT

namespace aidl::android::hardware::security::secureclock {
class TimeStampToken;
}  // namespace aidl::android::hardware::security::secureclock
namespace aidl {
namespace android {
namespace hardware {
namespace security {
namespace secureclock {
class ISecureClockDelegator;

class ISecureClock : public ::ndk::ICInterface {
public:
  typedef ISecureClockDelegator DefaultDelegator;
  static const char* descriptor;
  ISecureClock();
  virtual ~ISecureClock();

  static const char* TIME_STAMP_MAC_LABEL;
  static constexpr uint32_t TRANSACTION_generateTimeStamp = FIRST_CALL_TRANSACTION + 0;

  static std::shared_ptr<ISecureClock> fromBinder(const ::ndk::SpAIBinder& binder);
  static binder_status_t writeToParcel(AParcel* parcel, const std::shared_ptr<ISecureClock>& instance);
  static binder_status_t readFromParcel(const AParcel* parcel, std::shared_ptr<ISecureClock>* instance);
  static bool setDefaultImpl(const std::shared_ptr<ISecureClock>& impl);
  static const std::shared_ptr<ISecureClock>& getDefaultImpl();
  virtual ::ndk::ScopedAStatus generateTimeStamp(int64_t in_challenge, ::aidl::android::hardware::security::secureclock::TimeStampToken* _aidl_return) = 0;
private:
  static std::shared_ptr<ISecureClock> default_impl;
};
class ISecureClockDefault : public ISecureClock {
public:
  ::ndk::ScopedAStatus generateTimeStamp(int64_t in_challenge, ::aidl::android::hardware::security::secureclock::TimeStampToken* _aidl_return) override;
  ::ndk::SpAIBinder asBinder() override;
  bool isRemote() override;
};
}  // namespace secureclock
}  // namespace security
}  // namespace hardware
}  // namespace android
}  // namespace aidl
