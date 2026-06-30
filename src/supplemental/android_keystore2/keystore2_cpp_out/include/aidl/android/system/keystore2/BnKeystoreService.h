/*
 * This file is auto-generated.  DO NOT MODIFY.
 * Using: /Users/alvin/Library/Android/sdk/build-tools/35.0.0/aidl --lang=ndk --structured --stability vintf -I /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/secureclock/aidl -o /Users/alvin/Downloads/keystore2_cpp_out/src -h /Users/alvin/Downloads/keystore2_cpp_out/include /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl/android/system/keystore2/IKeystoreService.aidl
 */
#pragma once

#include "aidl/android/system/keystore2/IKeystoreService.h"

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
class BnKeystoreService : public ::ndk::BnCInterface<IKeystoreService> {
public:
  BnKeystoreService();
  virtual ~BnKeystoreService();
protected:
  ::ndk::SpAIBinder createBinder() override;
private:
};
class IKeystoreServiceDelegator : public BnKeystoreService {
public:
  explicit IKeystoreServiceDelegator(const std::shared_ptr<IKeystoreService> &impl) : _impl(impl) {
  }

  ::ndk::ScopedAStatus getSecurityLevel(::aidl::android::hardware::security::keymint::SecurityLevel in_securityLevel, std::shared_ptr<::aidl::android::system::keystore2::IKeystoreSecurityLevel>* _aidl_return) override {
    return _impl->getSecurityLevel(in_securityLevel, _aidl_return);
  }
  ::ndk::ScopedAStatus getKeyEntry(const ::aidl::android::system::keystore2::KeyDescriptor& in_key, ::aidl::android::system::keystore2::KeyEntryResponse* _aidl_return) override {
    return _impl->getKeyEntry(in_key, _aidl_return);
  }
  ::ndk::ScopedAStatus updateSubcomponent(const ::aidl::android::system::keystore2::KeyDescriptor& in_key, const std::optional<std::vector<uint8_t>>& in_publicCert, const std::optional<std::vector<uint8_t>>& in_certificateChain) override {
    return _impl->updateSubcomponent(in_key, in_publicCert, in_certificateChain);
  }
  ::ndk::ScopedAStatus listEntries(::aidl::android::system::keystore2::Domain in_domain, int64_t in_nspace, std::vector<::aidl::android::system::keystore2::KeyDescriptor>* _aidl_return) override __attribute__((deprecated("use listEntriesBatched instead."))) {
    return _impl->listEntries(in_domain, in_nspace, _aidl_return);
  }
  ::ndk::ScopedAStatus deleteKey(const ::aidl::android::system::keystore2::KeyDescriptor& in_key) override {
    return _impl->deleteKey(in_key);
  }
  ::ndk::ScopedAStatus grant(const ::aidl::android::system::keystore2::KeyDescriptor& in_key, int32_t in_granteeUid, int32_t in_accessVector, ::aidl::android::system::keystore2::KeyDescriptor* _aidl_return) override {
    return _impl->grant(in_key, in_granteeUid, in_accessVector, _aidl_return);
  }
  ::ndk::ScopedAStatus ungrant(const ::aidl::android::system::keystore2::KeyDescriptor& in_key, int32_t in_granteeUid) override {
    return _impl->ungrant(in_key, in_granteeUid);
  }
  ::ndk::ScopedAStatus getNumberOfEntries(::aidl::android::system::keystore2::Domain in_domain, int64_t in_nspace, int32_t* _aidl_return) override {
    return _impl->getNumberOfEntries(in_domain, in_nspace, _aidl_return);
  }
  ::ndk::ScopedAStatus listEntriesBatched(::aidl::android::system::keystore2::Domain in_domain, int64_t in_nspace, const std::optional<std::string>& in_startingPastAlias, std::vector<::aidl::android::system::keystore2::KeyDescriptor>* _aidl_return) override {
    return _impl->listEntriesBatched(in_domain, in_nspace, in_startingPastAlias, _aidl_return);
  }
  ::ndk::ScopedAStatus getSupplementaryAttestationInfo(::aidl::android::hardware::security::keymint::Tag in_tag, std::vector<uint8_t>* _aidl_return) override {
    return _impl->getSupplementaryAttestationInfo(in_tag, _aidl_return);
  }
protected:
private:
  std::shared_ptr<IKeystoreService> _impl;
};

}  // namespace keystore2
}  // namespace system
}  // namespace android
}  // namespace aidl
