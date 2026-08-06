#include <vector>
#include <mutex>

// NDK Binder 基础类型
#include <android/binder_ibinder.h>
#include <android/binder_auto_utils.h>
#include <android/binder_process.h>

// AServiceManager API (AOSP platform header，NDK r27c 不含)
#include <android/binder_manager.h>

// AIDL 生成的头文件
#include <aidl/android/system/keystore2/IKeystoreService.h>
#include <aidl/android/system/keystore2/Domain.h>
#include <aidl/android/system/keystore2/KeyDescriptor.h>
#include <aidl/android/hardware/security/keymint/KeyPurpose.h>
#include <aidl/android/hardware/security/keymint/Digest.h>
#include <aidl/android/hardware/security/keymint/KeyParameter.h>
#include <aidl/android/hardware/security/keymint/KeyParameterValue.h>
#include <aidl/android/hardware/security/keymint/PaddingMode.h>

#include <nng/supplemental/nanolib/log.h>

#include "pki_adapter.h"

using namespace aidl::android::system::keystore2;
using namespace aidl::android::hardware::security::keymint;
using ndk::ScopedAStatus;
using ndk::SpAIBinder;

// 运行时可覆盖的 DIGEST_NONE 开关 (初始值为编译期宏)
#if KEYSTORE2_USE_DIGEST_NONE
static bool g_digest_none = true;
#else
static bool g_digest_none = false;
#endif

void keystore2_set_digest_none(bool val)
{
	g_digest_none = val;
	log_info("[mTLS] DIGEST_NONE mode: %s", val ? "ON (TEE)" : "OFF (software KeyMint)");
}

// Binder 线程池初始化（确保在首次 AIDL 调用前完成）
static void ensureBinderThreadPool()
{
	static std::once_flag flag;
	std::call_once(flag, []() {
		ABinderProcess_startThreadPool();
	});
}

// C 可见的初始化函数，由 open_config_init (主线程) 调用
extern "C" void keystore2_init(void)
{
	ensureBinderThreadPool();
}


// 将 TLS SignatureScheme (RFC 8446) 解析为 KeyMint Digest + PaddingMode
// 格式: 0xMMNN (MM=hash, NN=signature algorithm)
//   RSA-PKCS1: 0x0201(SHA1), 0x0401(SHA256), 0x0501(SHA384), 0x0601(SHA512)
//   ECDSA:     0x0203(SHA1), 0x0403(SHA256), 0x0503(SHA384), 0x0603(SHA512)
static bool parseSignatureScheme(uint16_t sig_alg, Digest &outDigest,
                                 PaddingMode &outPadding)
{
	int hash     = (sig_alg >> 8) & 0xFF;
	int signType = sig_alg & 0xFF;

	switch (hash) {
	case 2: outDigest = Digest::SHA1;        break;
	case 4: outDigest = Digest::SHA_2_256;   break;
	case 5: outDigest = Digest::SHA_2_384;   break;
	case 6: outDigest = Digest::SHA_2_512;   break;
	default: return false;
	}

	switch (signType) {
	case 1: // RSA PKCS#1 v1.5
		outPadding = PaddingMode::RSA_PKCS1_1_5_SIGN;
		return true;
	case 3: // ECDSA
		outPadding = PaddingMode::NONE;
		return true;
	default: return false;
	}
}


// 处理 TLS 1.3 RSA-PSS (RFC 8446 §4.2.3)
//   RSA-PSS-RSAE: 0x0804(SHA256), 0x0805(SHA384), 0x0806(SHA512)
//   RSA-PSS-PSS:  0x0809(SHA256), 0x080a(SHA384), 0x080b(SHA512)
// 注意: Ed25519(0x0807) 和 Ed448(0x0808) 落在 0x0800 前缀但被 default 排除
static bool parsePSSSignatureScheme(uint16_t sig_alg, Digest &outDigest)
{
	if ((sig_alg & 0xFF00) != 0x0800) return false;
	int variant = sig_alg & 0xFF;
	switch (variant) {
	case 0x04: outDigest = Digest::SHA_2_256; return true;
	case 0x05: outDigest = Digest::SHA_2_384; return true;
	case 0x06: outDigest = Digest::SHA_2_512; return true;
	case 0x09: outDigest = Digest::SHA_2_256; return true;
	case 0x0a: outDigest = Digest::SHA_2_384; return true;
	case 0x0b: outDigest = Digest::SHA_2_512; return true;
	default: return false; // Ed25519(0x07), Ed448(0x08) 被排除
	}
}


// C 可见的初始化函数，由 open_config_init (主线程) 调用
extern "C" int keystore2_sign(const char *alias_cstr, int namespace_id,
			      const uint8_t *digest, int digest_len,
			      uint16_t sig_alg,
			      uint8_t *sig_out, int sig_max)
{
	ensureBinderThreadPool();

	const char *serviceName =
		"android.system.keystore2.IKeystoreService/default";
	AIBinder *rawBinder =
		AServiceManager_waitForService(serviceName);
	if (rawBinder == nullptr) {
		log_error("[mTLS] Failed to connect to Keystore2 service!");
		return -1;
	}
	SpAIBinder binder(rawBinder);
	std::shared_ptr<IKeystoreService> keystoreService =
		IKeystoreService::fromBinder(binder);

	KeyDescriptor keyDesc;
	keyDesc.alias = std::string(alias_cstr);

	if (namespace_id == -1) {
		keyDesc.domain = Domain::APP;
		keyDesc.nspace = -1;
	} else {
		keyDesc.domain = Domain::SELINUX;
		keyDesc.nspace = namespace_id;
	}

	KeyEntryResponse entryResponse;
	ScopedAStatus status =
		keystoreService->getKeyEntry(keyDesc, &entryResponse);
	if (!status.isOk() || entryResponse.iSecurityLevel == nullptr) {
		int excCode = status.getExceptionCode();
		int svcErr  = status.getServiceSpecificError();
		log_error("[mTLS] getKeyEntry failed (exc=%d svc=%d)", excCode, svcErr);

		return -1;
	}

	// 解析 TLS SignatureScheme → KeyMint Digest + PaddingMode
	Digest      keymintDigest;
	PaddingMode keymintPadding = PaddingMode::RSA_PKCS1_1_5_SIGN;
	bool        isPSS = false;
	bool        parsed = false;

	if (g_digest_none) {
		// V216 实车 TEE 模式：BoringSSL 已完成数据摘要，KeyMint 直接签名原始数据
		if (parsePSSSignatureScheme(sig_alg, keymintDigest)) {
			isPSS          = true;
			keymintPadding = PaddingMode::RSA_PSS;
			keymintDigest  = Digest::NONE;
			parsed         = true;
		} else if (parseSignatureScheme(sig_alg, keymintDigest, keymintPadding)) {
			keymintDigest = Digest::NONE;
			parsed        = true;
		}
		// 未识别时: keymintPadding 维持默认值 RSA_PKCS1_1_5_SIGN
		keymintDigest = Digest::NONE;
	} else {
		// 模拟器软件 KeyMint 模式：需要指定具体 digest（不支持 DIGEST_NONE）
		if (parsePSSSignatureScheme(sig_alg, keymintDigest)) {
			isPSS          = true;
			keymintPadding = PaddingMode::RSA_PSS;
			parsed         = true;
		} else if (parseSignatureScheme(sig_alg, keymintDigest, keymintPadding)) {
			parsed = true;
		}
		if (!parsed) {
			keymintDigest  = Digest::SHA_2_256;
			keymintPadding = PaddingMode::RSA_PKCS1_1_5_SIGN;
		}
	}

	if (!parsed) {
		log_warn("[mTLS] Unrecognized sig_alg=0x%04x, falling back to digest=%d padding=%d",
		         (int)sig_alg, (int)keymintDigest, (int)keymintPadding);
	}

	// 组装 TEE 操作参数
	std::vector<KeyParameter> opParams;
	KeyParameter p;
	p.tag = Tag::PURPOSE;
	p.value = KeyParameterValue::make<KeyParameterValue::keyPurpose>(KeyPurpose::SIGN);
	opParams.push_back(p);

	KeyParameter d;
	d.tag = Tag::DIGEST;
	d.value = KeyParameterValue::make<KeyParameterValue::digest>(keymintDigest);
	opParams.push_back(d);

	KeyParameter pad;
	pad.tag = Tag::PADDING;
	pad.value = KeyParameterValue::make<KeyParameterValue::paddingMode>(keymintPadding);
	opParams.push_back(pad);

	log_info("[mTLS] sig_alg=0x%04x digest=%d padding=%d isPSS=%d",
	         (int)sig_alg, (int)keymintDigest, (int)keymintPadding, (int)isPSS);

	// 发起硬件签名会话
	CreateOperationResponse opResponse;
	status = entryResponse.iSecurityLevel->createOperation(
		keyDesc, opParams, true, &opResponse);
	if (!status.isOk() || opResponse.iOperation == nullptr) {
		log_error("[mTLS] createOperation failed! exc=%d svc=%d",
		          status.getExceptionCode(), status.getServiceSpecificError());
		return -1;
	}

	// 提交数据并完成签名
	std::optional<std::vector<uint8_t>> input_data =
		std::vector<uint8_t>(digest, digest + digest_len);
	std::optional<std::vector<uint8_t>> output_signature;

	status = opResponse.iOperation->finish(input_data, std::nullopt,
					       &output_signature);
	if (!status.isOk() || !output_signature.has_value()) {
		log_error("[mTLS] finish failed! exc=%d svc=%d input_len=%d",
		          status.getExceptionCode(), status.getServiceSpecificError(),
		          digest_len);
		return -1;
	}

	int sig_len = output_signature.value().size();
	if (sig_len > sig_max)
		return -1;

	std::copy(output_signature.value().begin(),
		  output_signature.value().end(), sig_out);
	log_info("[mTLS] TEE hardware signing succeeded! sig_alg=0x%04x size: %d bytes",
	         (int)sig_alg, sig_len);

	return sig_len;
}


// C 可见的初始化函数，由 open_config_init (主线程) 调用
extern "C" int keystore2_get_cert(const char *alias_cstr, int namespace_id,
				  uint8_t *cert_out, int cert_max)
{
	ensureBinderThreadPool();

	const char *serviceName =
		"android.system.keystore2.IKeystoreService/default";
	AIBinder *rawBinder =
		AServiceManager_waitForService(serviceName);
	if (rawBinder == nullptr) {
		log_error("[mTLS] Failed to connect to Keystore2 service!");
		return -1;
	}
	SpAIBinder binder(rawBinder);
	std::shared_ptr<IKeystoreService> keystoreService =
		IKeystoreService::fromBinder(binder);

	KeyDescriptor keyDesc;
	keyDesc.alias = std::string(alias_cstr);

	if (namespace_id == -1) {
		keyDesc.domain = Domain::APP;
		keyDesc.nspace = -1;
	} else {
		keyDesc.domain = Domain::SELINUX;
		keyDesc.nspace = namespace_id;
	}

	KeyEntryResponse entryResponse;
	ScopedAStatus status =
		keystoreService->getKeyEntry(keyDesc, &entryResponse);
	if (!status.isOk() || entryResponse.iSecurityLevel == nullptr) {
		int excCode = status.getExceptionCode();
		int svcErr  = status.getServiceSpecificError();
		log_error("[mTLS] getKeyEntry failed (exc=%d svc=%d)", excCode, svcErr);

		return -1;
	}

	if (!entryResponse.metadata.certificate.has_value())
		return -1;

	auto cert_bytes = entryResponse.metadata.certificate.value();
	int cert_len = cert_bytes.size();
	if (cert_len > cert_max)
		return -1;

	std::copy(cert_bytes.begin(), cert_bytes.end(), cert_out);
	log_info("[mTLS] Successfully retrieved certificate from Keystore2! size: %d bytes", cert_len);
	return cert_len;
}


// C 可见的初始化函数，由 open_config_init (主线程) 调用
extern "C" int keystore2_get_cert_chain(const char *alias_cstr, int namespace_id,
					 uint8_t *chain_out, int chain_max)
{
	ensureBinderThreadPool();

	const char *serviceName =
		"android.system.keystore2.IKeystoreService/default";
	AIBinder *rawBinder =
		AServiceManager_waitForService(serviceName);
	if (rawBinder == nullptr) {
		log_error("[mTLS] cert_chain: Failed to connect to Keystore2 service!");
		return -1;
	}
	SpAIBinder binder(rawBinder);
	std::shared_ptr<IKeystoreService> keystoreService =
		IKeystoreService::fromBinder(binder);

	KeyDescriptor keyDesc;
	keyDesc.alias = std::string(alias_cstr);

	if (namespace_id == -1) {
		keyDesc.domain = Domain::APP;
		keyDesc.nspace = -1;
	} else {
		keyDesc.domain = Domain::SELINUX;
		keyDesc.nspace = namespace_id;
	}

	KeyEntryResponse entryResponse;
	ScopedAStatus status =
		keystoreService->getKeyEntry(keyDesc, &entryResponse);
	if (!status.isOk() || entryResponse.iSecurityLevel == nullptr) {
		int excCode = status.getExceptionCode();
		int svcErr  = status.getServiceSpecificError();
		log_error("[mTLS] cert_chain: getKeyEntry failed (exc=%d svc=%d)", excCode, svcErr);
		return -1;
	}

	if (!entryResponse.metadata.certificateChain.has_value()) {
		log_warn("[mTLS] cert_chain: certificateChain not available in Keystore2");
		return -1;
	}

	auto chain_bytes = entryResponse.metadata.certificateChain.value();
	int chain_len = chain_bytes.size();
	if (chain_len > chain_max) {
		log_error("[mTLS] cert_chain: chain too large (%d > %d)", chain_len, chain_max);
		return -1;
	}

	std::copy(chain_bytes.begin(), chain_bytes.end(), chain_out);
	log_info("[mTLS] Successfully retrieved cert chain from Keystore2! size: %d bytes", chain_len);
	return chain_len;
}
