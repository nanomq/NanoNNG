/*
 * This file is auto-generated.  DO NOT MODIFY.
 * Using: /Users/alvin/Library/Android/sdk/build-tools/35.0.0/aidl --lang=ndk --structured --stability vintf -I /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/secureclock/aidl -o /Users/alvin/Downloads/keystore2_cpp_out/src -h /Users/alvin/Downloads/keystore2_cpp_out/include /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl/android/system/keystore2/IKeystoreService.aidl
 */
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <android/binder_interface_utils.h>
#include <aidl/android/hardware/security/keymint/SecurityLevel.h>
#include <aidl/android/hardware/security/keymint/Tag.h>
#include <aidl/android/system/keystore2/Domain.h>
#include <aidl/android/system/keystore2/IKeystoreSecurityLevel.h>
#include <aidl/android/system/keystore2/KeyDescriptor.h>
#include <aidl/android/system/keystore2/KeyEntryResponse.h>
#ifdef BINDER_STABILITY_SUPPORT
#include <android/binder_stability.h>
#endif  // BINDER_STABILITY_SUPPORT

namespace aidl::android::system::keystore2 {
class IKeystoreSecurityLevel;
class KeyDescriptor;
class KeyEntryResponse;
}  // namespace aidl::android::system::keystore2
namespace aidl {
namespace android {
namespace system {
namespace keystore2 {
class IKeystoreServiceDelegator;

class IKeystoreService : public ::ndk::ICInterface {
public:
  typedef IKeystoreServiceDelegator DefaultDelegator;
  static const char* descriptor;
  IKeystoreService();
  virtual ~IKeystoreService();

  static constexpr uint32_t TRANSACTION_getSecurityLevel = FIRST_CALL_TRANSACTION + 0;
  static constexpr uint32_t TRANSACTION_getKeyEntry = FIRST_CALL_TRANSACTION + 1;
  static constexpr uint32_t TRANSACTION_updateSubcomponent = FIRST_CALL_TRANSACTION + 2;
  static constexpr uint32_t TRANSACTION_listEntries = FIRST_CALL_TRANSACTION + 3;
  static constexpr uint32_t TRANSACTION_deleteKey = FIRST_CALL_TRANSACTION + 4;
  static constexpr uint32_t TRANSACTION_grant = FIRST_CALL_TRANSACTION + 5;
  static constexpr uint32_t TRANSACTION_ungrant = FIRST_CALL_TRANSACTION + 6;
  static constexpr uint32_t TRANSACTION_getNumberOfEntries = FIRST_CALL_TRANSACTION + 7;
  static constexpr uint32_t TRANSACTION_listEntriesBatched = FIRST_CALL_TRANSACTION + 8;
  static constexpr uint32_t TRANSACTION_getSupplementaryAttestationInfo = FIRST_CALL_TRANSACTION + 9;

  static std::shared_ptr<IKeystoreService> fromBinder(const ::ndk::SpAIBinder& binder);
  static binder_status_t writeToParcel(AParcel* parcel, const std::shared_ptr<IKeystoreService>& instance);
  static binder_status_t readFromParcel(const AParcel* parcel, std::shared_ptr<IKeystoreService>* instance);
  static bool setDefaultImpl(const std::shared_ptr<IKeystoreService>& impl);
  static const std::shared_ptr<IKeystoreService>& getDefaultImpl();
  virtual ::ndk::ScopedAStatus getSecurityLevel(::aidl::android::hardware::security::keymint::SecurityLevel in_securityLevel, std::shared_ptr<::aidl::android::system::keystore2::IKeystoreSecurityLevel>* _aidl_return) = 0;
  virtual ::ndk::ScopedAStatus getKeyEntry(const ::aidl::android::system::keystore2::KeyDescriptor& in_key, ::aidl::android::system::keystore2::KeyEntryResponse* _aidl_return) = 0;
  virtual ::ndk::ScopedAStatus updateSubcomponent(const ::aidl::android::system::keystore2::KeyDescriptor& in_key, const std::optional<std::vector<uint8_t>>& in_publicCert, const std::optional<std::vector<uint8_t>>& in_certificateChain) = 0;
  virtual ::ndk::ScopedAStatus listEntries(::aidl::android::system::keystore2::Domain in_domain, int64_t in_nspace, std::vector<::aidl::android::system::keystore2::KeyDescriptor>* _aidl_return) __attribute__((deprecated("use listEntriesBatched instead."))) = 0;
  virtual ::ndk::ScopedAStatus deleteKey(const ::aidl::android::system::keystore2::KeyDescriptor& in_key) = 0;
  virtual ::ndk::ScopedAStatus grant(const ::aidl::android::system::keystore2::KeyDescriptor& in_key, int32_t in_granteeUid, int32_t in_accessVector, ::aidl::android::system::keystore2::KeyDescriptor* _aidl_return) = 0;
  virtual ::ndk::ScopedAStatus ungrant(const ::aidl::android::system::keystore2::KeyDescriptor& in_key, int32_t in_granteeUid) = 0;
  virtual ::ndk::ScopedAStatus getNumberOfEntries(::aidl::android::system::keystore2::Domain in_domain, int64_t in_nspace, int32_t* _aidl_return) = 0;
  virtual ::ndk::ScopedAStatus listEntriesBatched(::aidl::android::system::keystore2::Domain in_domain, int64_t in_nspace, const std::optional<std::string>& in_startingPastAlias, std::vector<::aidl::android::system::keystore2::KeyDescriptor>* _aidl_return) = 0;
  virtual ::ndk::ScopedAStatus getSupplementaryAttestationInfo(::aidl::android::hardware::security::keymint::Tag in_tag, std::vector<uint8_t>* _aidl_return) = 0;
private:
  static std::shared_ptr<IKeystoreService> default_impl;
};
class IKeystoreServiceDefault : public IKeystoreService {
public:
  ::ndk::ScopedAStatus getSecurityLevel(::aidl::android::hardware::security::keymint::SecurityLevel in_securityLevel, std::shared_ptr<::aidl::android::system::keystore2::IKeystoreSecurityLevel>* _aidl_return) override;
  ::ndk::ScopedAStatus getKeyEntry(const ::aidl::android::system::keystore2::KeyDescriptor& in_key, ::aidl::android::system::keystore2::KeyEntryResponse* _aidl_return) override;
  ::ndk::ScopedAStatus updateSubcomponent(const ::aidl::android::system::keystore2::KeyDescriptor& in_key, const std::optional<std::vector<uint8_t>>& in_publicCert, const std::optional<std::vector<uint8_t>>& in_certificateChain) override;
  ::ndk::ScopedAStatus listEntries(::aidl::android::system::keystore2::Domain in_domain, int64_t in_nspace, std::vector<::aidl::android::system::keystore2::KeyDescriptor>* _aidl_return) override __attribute__((deprecated("use listEntriesBatched instead.")));
  ::ndk::ScopedAStatus deleteKey(const ::aidl::android::system::keystore2::KeyDescriptor& in_key) override;
  ::ndk::ScopedAStatus grant(const ::aidl::android::system::keystore2::KeyDescriptor& in_key, int32_t in_granteeUid, int32_t in_accessVector, ::aidl::android::system::keystore2::KeyDescriptor* _aidl_return) override;
  ::ndk::ScopedAStatus ungrant(const ::aidl::android::system::keystore2::KeyDescriptor& in_key, int32_t in_granteeUid) override;
  ::ndk::ScopedAStatus getNumberOfEntries(::aidl::android::system::keystore2::Domain in_domain, int64_t in_nspace, int32_t* _aidl_return) override;
  ::ndk::ScopedAStatus listEntriesBatched(::aidl::android::system::keystore2::Domain in_domain, int64_t in_nspace, const std::optional<std::string>& in_startingPastAlias, std::vector<::aidl::android::system::keystore2::KeyDescriptor>* _aidl_return) override;
  ::ndk::ScopedAStatus getSupplementaryAttestationInfo(::aidl::android::hardware::security::keymint::Tag in_tag, std::vector<uint8_t>* _aidl_return) override;
  ::ndk::SpAIBinder asBinder() override;
  bool isRemote() override;
};
}  // namespace keystore2
}  // namespace system
}  // namespace android
}  // namespace aidl
