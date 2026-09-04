/*
 * This file is auto-generated.  DO NOT MODIFY.
 * Using: /Users/alvin/Library/Android/sdk/build-tools/35.0.0/aidl --lang=ndk --structured --stability vintf -I /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/secureclock/aidl -o /Users/alvin/Downloads/keystore2_cpp_out/src -h /Users/alvin/Downloads/keystore2_cpp_out/include /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl/android/hardware/security/keymint/IKeyMintDevice.aidl
 */
#pragma once

#include "aidl/android/hardware/security/keymint/IKeyMintDevice.h"

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
namespace keymint {
class BnKeyMintDevice : public ::ndk::BnCInterface<IKeyMintDevice> {
public:
  BnKeyMintDevice();
  virtual ~BnKeyMintDevice();
protected:
  ::ndk::SpAIBinder createBinder() override;
private:
};
class IKeyMintDeviceDelegator : public BnKeyMintDevice {
public:
  explicit IKeyMintDeviceDelegator(const std::shared_ptr<IKeyMintDevice> &impl) : _impl(impl) {
  }

  ::ndk::ScopedAStatus getHardwareInfo(::aidl::android::hardware::security::keymint::KeyMintHardwareInfo* _aidl_return) override {
    return _impl->getHardwareInfo(_aidl_return);
  }
  ::ndk::ScopedAStatus addRngEntropy(const std::vector<uint8_t>& in_data) override {
    return _impl->addRngEntropy(in_data);
  }
  ::ndk::ScopedAStatus generateKey(const std::vector<::aidl::android::hardware::security::keymint::KeyParameter>& in_keyParams, const std::optional<::aidl::android::hardware::security::keymint::AttestationKey>& in_attestationKey, ::aidl::android::hardware::security::keymint::KeyCreationResult* _aidl_return) override {
    return _impl->generateKey(in_keyParams, in_attestationKey, _aidl_return);
  }
  ::ndk::ScopedAStatus importKey(const std::vector<::aidl::android::hardware::security::keymint::KeyParameter>& in_keyParams, ::aidl::android::hardware::security::keymint::KeyFormat in_keyFormat, const std::vector<uint8_t>& in_keyData, const std::optional<::aidl::android::hardware::security::keymint::AttestationKey>& in_attestationKey, ::aidl::android::hardware::security::keymint::KeyCreationResult* _aidl_return) override {
    return _impl->importKey(in_keyParams, in_keyFormat, in_keyData, in_attestationKey, _aidl_return);
  }
  ::ndk::ScopedAStatus importWrappedKey(const std::vector<uint8_t>& in_wrappedKeyData, const std::vector<uint8_t>& in_wrappingKeyBlob, const std::vector<uint8_t>& in_maskingKey, const std::vector<::aidl::android::hardware::security::keymint::KeyParameter>& in_unwrappingParams, int64_t in_passwordSid, int64_t in_biometricSid, ::aidl::android::hardware::security::keymint::KeyCreationResult* _aidl_return) override {
    return _impl->importWrappedKey(in_wrappedKeyData, in_wrappingKeyBlob, in_maskingKey, in_unwrappingParams, in_passwordSid, in_biometricSid, _aidl_return);
  }
  ::ndk::ScopedAStatus upgradeKey(const std::vector<uint8_t>& in_keyBlobToUpgrade, const std::vector<::aidl::android::hardware::security::keymint::KeyParameter>& in_upgradeParams, std::vector<uint8_t>* _aidl_return) override {
    return _impl->upgradeKey(in_keyBlobToUpgrade, in_upgradeParams, _aidl_return);
  }
  ::ndk::ScopedAStatus deleteKey(const std::vector<uint8_t>& in_keyBlob) override {
    return _impl->deleteKey(in_keyBlob);
  }
  ::ndk::ScopedAStatus deleteAllKeys() override {
    return _impl->deleteAllKeys();
  }
  ::ndk::ScopedAStatus destroyAttestationIds() override {
    return _impl->destroyAttestationIds();
  }
  ::ndk::ScopedAStatus begin(::aidl::android::hardware::security::keymint::KeyPurpose in_purpose, const std::vector<uint8_t>& in_keyBlob, const std::vector<::aidl::android::hardware::security::keymint::KeyParameter>& in_params, const std::optional<::aidl::android::hardware::security::keymint::HardwareAuthToken>& in_authToken, ::aidl::android::hardware::security::keymint::BeginResult* _aidl_return) override {
    return _impl->begin(in_purpose, in_keyBlob, in_params, in_authToken, _aidl_return);
  }
  ::ndk::ScopedAStatus deviceLocked(bool in_passwordOnly, const std::optional<::aidl::android::hardware::security::secureclock::TimeStampToken>& in_timestampToken) override __attribute__((deprecated("Method has never been used due to design limitations"))) {
    return _impl->deviceLocked(in_passwordOnly, in_timestampToken);
  }
  ::ndk::ScopedAStatus earlyBootEnded() override {
    return _impl->earlyBootEnded();
  }
  ::ndk::ScopedAStatus convertStorageKeyToEphemeral(const std::vector<uint8_t>& in_storageKeyBlob, std::vector<uint8_t>* _aidl_return) override {
    return _impl->convertStorageKeyToEphemeral(in_storageKeyBlob, _aidl_return);
  }
  ::ndk::ScopedAStatus getKeyCharacteristics(const std::vector<uint8_t>& in_keyBlob, const std::vector<uint8_t>& in_appId, const std::vector<uint8_t>& in_appData, std::vector<::aidl::android::hardware::security::keymint::KeyCharacteristics>* _aidl_return) override {
    return _impl->getKeyCharacteristics(in_keyBlob, in_appId, in_appData, _aidl_return);
  }
  ::ndk::ScopedAStatus getRootOfTrustChallenge(std::array<uint8_t, 16>* _aidl_return) override {
    return _impl->getRootOfTrustChallenge(_aidl_return);
  }
  ::ndk::ScopedAStatus getRootOfTrust(const std::array<uint8_t, 16>& in_challenge, std::vector<uint8_t>* _aidl_return) override {
    return _impl->getRootOfTrust(in_challenge, _aidl_return);
  }
  ::ndk::ScopedAStatus sendRootOfTrust(const std::vector<uint8_t>& in_rootOfTrust) override {
    return _impl->sendRootOfTrust(in_rootOfTrust);
  }
  ::ndk::ScopedAStatus setAdditionalAttestationInfo(const std::vector<::aidl::android::hardware::security::keymint::KeyParameter>& in_info) override {
    return _impl->setAdditionalAttestationInfo(in_info);
  }
protected:
private:
  std::shared_ptr<IKeyMintDevice> _impl;
};

}  // namespace keymint
}  // namespace security
}  // namespace hardware
}  // namespace android
}  // namespace aidl
