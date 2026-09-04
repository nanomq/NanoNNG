/*
 * This file is auto-generated.  DO NOT MODIFY.
 * Using: /Users/alvin/Library/Android/sdk/build-tools/35.0.0/aidl --lang=ndk --structured --stability vintf -I /Users/alvin/Downloads/system_hardware_interfaces/keystore2/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl -I /Users/alvin/Downloads/hardware_interfaces/security/secureclock/aidl -o /Users/alvin/Downloads/keystore2_cpp_out/src -h /Users/alvin/Downloads/keystore2_cpp_out/include /Users/alvin/Downloads/hardware_interfaces/security/keymint/aidl/android/hardware/security/keymint/IKeyMintDevice.aidl
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
#include <aidl/android/hardware/security/keymint/AttestationKey.h>
#include <aidl/android/hardware/security/keymint/BeginResult.h>
#include <aidl/android/hardware/security/keymint/HardwareAuthToken.h>
#include <aidl/android/hardware/security/keymint/KeyCharacteristics.h>
#include <aidl/android/hardware/security/keymint/KeyCreationResult.h>
#include <aidl/android/hardware/security/keymint/KeyFormat.h>
#include <aidl/android/hardware/security/keymint/KeyMintHardwareInfo.h>
#include <aidl/android/hardware/security/keymint/KeyParameter.h>
#include <aidl/android/hardware/security/keymint/KeyPurpose.h>
#include <aidl/android/hardware/security/secureclock/TimeStampToken.h>
#ifdef BINDER_STABILITY_SUPPORT
#include <android/binder_stability.h>
#endif  // BINDER_STABILITY_SUPPORT

namespace aidl::android::hardware::security::keymint {
class AttestationKey;
class BeginResult;
class HardwareAuthToken;
class KeyCharacteristics;
class KeyCreationResult;
class KeyMintHardwareInfo;
class KeyParameter;
}  // namespace aidl::android::hardware::security::keymint
namespace aidl::android::hardware::security::secureclock {
class TimeStampToken;
}  // namespace aidl::android::hardware::security::secureclock
namespace aidl {
namespace android {
namespace hardware {
namespace security {
namespace keymint {
class IKeyMintDeviceDelegator;

class IKeyMintDevice : public ::ndk::ICInterface {
public:
  typedef IKeyMintDeviceDelegator DefaultDelegator;
  static const char* descriptor;
  IKeyMintDevice();
  virtual ~IKeyMintDevice();

  enum : int32_t { AUTH_TOKEN_MAC_LENGTH = 32 };
  static constexpr uint32_t TRANSACTION_getHardwareInfo = FIRST_CALL_TRANSACTION + 0;
  static constexpr uint32_t TRANSACTION_addRngEntropy = FIRST_CALL_TRANSACTION + 1;
  static constexpr uint32_t TRANSACTION_generateKey = FIRST_CALL_TRANSACTION + 2;
  static constexpr uint32_t TRANSACTION_importKey = FIRST_CALL_TRANSACTION + 3;
  static constexpr uint32_t TRANSACTION_importWrappedKey = FIRST_CALL_TRANSACTION + 4;
  static constexpr uint32_t TRANSACTION_upgradeKey = FIRST_CALL_TRANSACTION + 5;
  static constexpr uint32_t TRANSACTION_deleteKey = FIRST_CALL_TRANSACTION + 6;
  static constexpr uint32_t TRANSACTION_deleteAllKeys = FIRST_CALL_TRANSACTION + 7;
  static constexpr uint32_t TRANSACTION_destroyAttestationIds = FIRST_CALL_TRANSACTION + 8;
  static constexpr uint32_t TRANSACTION_begin = FIRST_CALL_TRANSACTION + 9;
  static constexpr uint32_t TRANSACTION_deviceLocked = FIRST_CALL_TRANSACTION + 10;
  static constexpr uint32_t TRANSACTION_earlyBootEnded = FIRST_CALL_TRANSACTION + 11;
  static constexpr uint32_t TRANSACTION_convertStorageKeyToEphemeral = FIRST_CALL_TRANSACTION + 12;
  static constexpr uint32_t TRANSACTION_getKeyCharacteristics = FIRST_CALL_TRANSACTION + 13;
  static constexpr uint32_t TRANSACTION_getRootOfTrustChallenge = FIRST_CALL_TRANSACTION + 14;
  static constexpr uint32_t TRANSACTION_getRootOfTrust = FIRST_CALL_TRANSACTION + 15;
  static constexpr uint32_t TRANSACTION_sendRootOfTrust = FIRST_CALL_TRANSACTION + 16;
  static constexpr uint32_t TRANSACTION_setAdditionalAttestationInfo = FIRST_CALL_TRANSACTION + 17;

  static std::shared_ptr<IKeyMintDevice> fromBinder(const ::ndk::SpAIBinder& binder);
  static binder_status_t writeToParcel(AParcel* parcel, const std::shared_ptr<IKeyMintDevice>& instance);
  static binder_status_t readFromParcel(const AParcel* parcel, std::shared_ptr<IKeyMintDevice>* instance);
  static bool setDefaultImpl(const std::shared_ptr<IKeyMintDevice>& impl);
  static const std::shared_ptr<IKeyMintDevice>& getDefaultImpl();
  virtual ::ndk::ScopedAStatus getHardwareInfo(::aidl::android::hardware::security::keymint::KeyMintHardwareInfo* _aidl_return) = 0;
  virtual ::ndk::ScopedAStatus addRngEntropy(const std::vector<uint8_t>& in_data) = 0;
  virtual ::ndk::ScopedAStatus generateKey(const std::vector<::aidl::android::hardware::security::keymint::KeyParameter>& in_keyParams, const std::optional<::aidl::android::hardware::security::keymint::AttestationKey>& in_attestationKey, ::aidl::android::hardware::security::keymint::KeyCreationResult* _aidl_return) = 0;
  virtual ::ndk::ScopedAStatus importKey(const std::vector<::aidl::android::hardware::security::keymint::KeyParameter>& in_keyParams, ::aidl::android::hardware::security::keymint::KeyFormat in_keyFormat, const std::vector<uint8_t>& in_keyData, const std::optional<::aidl::android::hardware::security::keymint::AttestationKey>& in_attestationKey, ::aidl::android::hardware::security::keymint::KeyCreationResult* _aidl_return) = 0;
  virtual ::ndk::ScopedAStatus importWrappedKey(const std::vector<uint8_t>& in_wrappedKeyData, const std::vector<uint8_t>& in_wrappingKeyBlob, const std::vector<uint8_t>& in_maskingKey, const std::vector<::aidl::android::hardware::security::keymint::KeyParameter>& in_unwrappingParams, int64_t in_passwordSid, int64_t in_biometricSid, ::aidl::android::hardware::security::keymint::KeyCreationResult* _aidl_return) = 0;
  virtual ::ndk::ScopedAStatus upgradeKey(const std::vector<uint8_t>& in_keyBlobToUpgrade, const std::vector<::aidl::android::hardware::security::keymint::KeyParameter>& in_upgradeParams, std::vector<uint8_t>* _aidl_return) = 0;
  virtual ::ndk::ScopedAStatus deleteKey(const std::vector<uint8_t>& in_keyBlob) = 0;
  virtual ::ndk::ScopedAStatus deleteAllKeys() = 0;
  virtual ::ndk::ScopedAStatus destroyAttestationIds() = 0;
  virtual ::ndk::ScopedAStatus begin(::aidl::android::hardware::security::keymint::KeyPurpose in_purpose, const std::vector<uint8_t>& in_keyBlob, const std::vector<::aidl::android::hardware::security::keymint::KeyParameter>& in_params, const std::optional<::aidl::android::hardware::security::keymint::HardwareAuthToken>& in_authToken, ::aidl::android::hardware::security::keymint::BeginResult* _aidl_return) = 0;
  virtual ::ndk::ScopedAStatus deviceLocked(bool in_passwordOnly, const std::optional<::aidl::android::hardware::security::secureclock::TimeStampToken>& in_timestampToken) __attribute__((deprecated("Method has never been used due to design limitations"))) = 0;
  virtual ::ndk::ScopedAStatus earlyBootEnded() = 0;
  virtual ::ndk::ScopedAStatus convertStorageKeyToEphemeral(const std::vector<uint8_t>& in_storageKeyBlob, std::vector<uint8_t>* _aidl_return) = 0;
  virtual ::ndk::ScopedAStatus getKeyCharacteristics(const std::vector<uint8_t>& in_keyBlob, const std::vector<uint8_t>& in_appId, const std::vector<uint8_t>& in_appData, std::vector<::aidl::android::hardware::security::keymint::KeyCharacteristics>* _aidl_return) = 0;
  virtual ::ndk::ScopedAStatus getRootOfTrustChallenge(std::array<uint8_t, 16>* _aidl_return) = 0;
  virtual ::ndk::ScopedAStatus getRootOfTrust(const std::array<uint8_t, 16>& in_challenge, std::vector<uint8_t>* _aidl_return) = 0;
  virtual ::ndk::ScopedAStatus sendRootOfTrust(const std::vector<uint8_t>& in_rootOfTrust) = 0;
  virtual ::ndk::ScopedAStatus setAdditionalAttestationInfo(const std::vector<::aidl::android::hardware::security::keymint::KeyParameter>& in_info) = 0;
private:
  static std::shared_ptr<IKeyMintDevice> default_impl;
};
class IKeyMintDeviceDefault : public IKeyMintDevice {
public:
  ::ndk::ScopedAStatus getHardwareInfo(::aidl::android::hardware::security::keymint::KeyMintHardwareInfo* _aidl_return) override;
  ::ndk::ScopedAStatus addRngEntropy(const std::vector<uint8_t>& in_data) override;
  ::ndk::ScopedAStatus generateKey(const std::vector<::aidl::android::hardware::security::keymint::KeyParameter>& in_keyParams, const std::optional<::aidl::android::hardware::security::keymint::AttestationKey>& in_attestationKey, ::aidl::android::hardware::security::keymint::KeyCreationResult* _aidl_return) override;
  ::ndk::ScopedAStatus importKey(const std::vector<::aidl::android::hardware::security::keymint::KeyParameter>& in_keyParams, ::aidl::android::hardware::security::keymint::KeyFormat in_keyFormat, const std::vector<uint8_t>& in_keyData, const std::optional<::aidl::android::hardware::security::keymint::AttestationKey>& in_attestationKey, ::aidl::android::hardware::security::keymint::KeyCreationResult* _aidl_return) override;
  ::ndk::ScopedAStatus importWrappedKey(const std::vector<uint8_t>& in_wrappedKeyData, const std::vector<uint8_t>& in_wrappingKeyBlob, const std::vector<uint8_t>& in_maskingKey, const std::vector<::aidl::android::hardware::security::keymint::KeyParameter>& in_unwrappingParams, int64_t in_passwordSid, int64_t in_biometricSid, ::aidl::android::hardware::security::keymint::KeyCreationResult* _aidl_return) override;
  ::ndk::ScopedAStatus upgradeKey(const std::vector<uint8_t>& in_keyBlobToUpgrade, const std::vector<::aidl::android::hardware::security::keymint::KeyParameter>& in_upgradeParams, std::vector<uint8_t>* _aidl_return) override;
  ::ndk::ScopedAStatus deleteKey(const std::vector<uint8_t>& in_keyBlob) override;
  ::ndk::ScopedAStatus deleteAllKeys() override;
  ::ndk::ScopedAStatus destroyAttestationIds() override;
  ::ndk::ScopedAStatus begin(::aidl::android::hardware::security::keymint::KeyPurpose in_purpose, const std::vector<uint8_t>& in_keyBlob, const std::vector<::aidl::android::hardware::security::keymint::KeyParameter>& in_params, const std::optional<::aidl::android::hardware::security::keymint::HardwareAuthToken>& in_authToken, ::aidl::android::hardware::security::keymint::BeginResult* _aidl_return) override;
  ::ndk::ScopedAStatus deviceLocked(bool in_passwordOnly, const std::optional<::aidl::android::hardware::security::secureclock::TimeStampToken>& in_timestampToken) override __attribute__((deprecated("Method has never been used due to design limitations")));
  ::ndk::ScopedAStatus earlyBootEnded() override;
  ::ndk::ScopedAStatus convertStorageKeyToEphemeral(const std::vector<uint8_t>& in_storageKeyBlob, std::vector<uint8_t>* _aidl_return) override;
  ::ndk::ScopedAStatus getKeyCharacteristics(const std::vector<uint8_t>& in_keyBlob, const std::vector<uint8_t>& in_appId, const std::vector<uint8_t>& in_appData, std::vector<::aidl::android::hardware::security::keymint::KeyCharacteristics>* _aidl_return) override;
  ::ndk::ScopedAStatus getRootOfTrustChallenge(std::array<uint8_t, 16>* _aidl_return) override;
  ::ndk::ScopedAStatus getRootOfTrust(const std::array<uint8_t, 16>& in_challenge, std::vector<uint8_t>* _aidl_return) override;
  ::ndk::ScopedAStatus sendRootOfTrust(const std::vector<uint8_t>& in_rootOfTrust) override;
  ::ndk::ScopedAStatus setAdditionalAttestationInfo(const std::vector<::aidl::android::hardware::security::keymint::KeyParameter>& in_info) override;
  ::ndk::SpAIBinder asBinder() override;
  bool isRemote() override;
};
}  // namespace keymint
}  // namespace security
}  // namespace hardware
}  // namespace android
}  // namespace aidl
