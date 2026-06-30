// Keystore2 自测工具 — 验证证书获取和 TEE 签名路径，无需网络
// 编译后 push 到模拟器以 App UID 运行即可验证全套 Keystore2 链路

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <vector>

#include <android/binder_ibinder.h>
#include <android/binder_auto_utils.h>
#include <android/binder_manager.h>

#include <aidl/android/system/keystore2/IKeystoreService.h>
#include <aidl/android/system/keystore2/Domain.h>
#include <aidl/android/system/keystore2/KeyDescriptor.h>
#include <aidl/android/system/keystore2/KeyEntryResponse.h>
#include <aidl/android/hardware/security/keymint/KeyParameter.h>
#include <aidl/android/hardware/security/keymint/KeyParameterValue.h>
#include <aidl/android/hardware/security/keymint/KeyPurpose.h>
#include <aidl/android/hardware/security/keymint/Digest.h>
#include <aidl/android/hardware/security/keymint/PaddingMode.h>
#include <aidl/android/hardware/security/keymint/Tag.h>

using namespace aidl::android::system::keystore2;
using namespace aidl::android::hardware::security::keymint;
using ndk::ScopedAStatus;
using ndk::SpAIBinder;

int main(int argc, char **argv)
{
    const char *alias = (argc > 1) ? argv[1] : "ecu-client-certificate";
    int namespace_id  = (argc > 2) ? atoi(argv[2]) : -1;

    printf("=== Keystore2 Self-Test ===\n");
    printf("Alias:     %s\n", alias);
    printf("Namespace: %d (%s)\n", namespace_id,
           namespace_id == -1 ? "Domain::APP" : "Domain::SELINUX");

    // 1. 连接 Keystore2 服务
    const char *svc = "android.system.keystore2.IKeystoreService/default";
    AIBinder *raw = AServiceManager_waitForService(svc);
    if (!raw) {
        printf("FAIL: 无法连接 Keystore2 服务\n");
        return 1;
    }
    SpAIBinder binder(raw);
    auto ks2 = IKeystoreService::fromBinder(binder);
    printf("PASS: Keystore2 服务已连接\n");

    // 2. 获取密钥条目
    KeyDescriptor keyDesc;
    keyDesc.alias = alias;
    if (namespace_id == -1) {
        keyDesc.domain = Domain::APP;
        keyDesc.nspace = -1;
    } else {
        keyDesc.domain = Domain::SELINUX;
        keyDesc.nspace = namespace_id;
    }

    KeyEntryResponse entryResponse;
    ScopedAStatus status = ks2->getKeyEntry(keyDesc, &entryResponse);
    int exceptionCode = status.getExceptionCode();
    int serviceError  = status.getServiceSpecificError();
    printf("getKeyEntry 返回: isOk=%d, exception=%d, serviceError=%d\n",
           status.isOk(), exceptionCode, serviceError);
    if (!status.isOk()) {
        if (serviceError != 0) {
            printf("FAIL: getKeyEntry 服务端错误 (serviceError=%d)\n", serviceError);
        } else {
            printf("FAIL: getKeyEntry 调用失败 (exceptionCode=%d)\n", exceptionCode);
        }
        // 尝试列出当前可访问的密钥
        printf("尝试诊断: 当前 UID=%d\n", getuid());
        return 1;
    }
    if (!entryResponse.iSecurityLevel) {
        printf("FAIL: SecurityLevel 为空 (密钥不存在或无权访问)\n");
        return 1;
    }
    printf("PASS: 密钥 '%s' 已找到\n", alias);

    // 3. 获取证书
    if (entryResponse.metadata.certificate.has_value()) {
        auto &cert = entryResponse.metadata.certificate.value();
        printf("PASS: 证书已获取 (%zu bytes)\n", cert.size());
    } else {
        printf("WARN: 无证书链 (metadata.certificate 为空)\n");
    }

    // 4. 尝试不同 (digest + padding) 组合签名
    // TLS 1.2 用 RSA-PKCS1-1.5, TLS 1.3 用 RSA-PSS
    struct { Digest digest; const char *dname; PaddingMode pad; const char *pname; } tests[] = {
        {Digest::SHA_2_256, "SHA256", PaddingMode::RSA_PKCS1_1_5_SIGN, "PKCS1_1.5"},
        {Digest::SHA_2_256, "SHA256", PaddingMode::RSA_PSS,           "PSS"},
        {Digest::SHA_2_384, "SHA384", PaddingMode::RSA_PKCS1_1_5_SIGN, "PKCS1_1.5"},
        {Digest::SHA_2_384, "SHA384", PaddingMode::RSA_PSS,           "PSS"},
        {Digest::SHA_2_512, "SHA512", PaddingMode::RSA_PKCS1_1_5_SIGN, "PKCS1_1.5"},
        {Digest::SHA_2_512, "SHA512", PaddingMode::RSA_PSS,           "PSS"},
    };

    bool signOk = false;
    for (auto &t : tests) {
        std::vector<KeyParameter> opParams;
        KeyParameter purposeP;
        purposeP.tag   = Tag::PURPOSE;
        purposeP.value = KeyParameterValue::make<KeyParameterValue::keyPurpose>(KeyPurpose::SIGN);
        opParams.push_back(purposeP);

        KeyParameter digestP;
        digestP.tag   = Tag::DIGEST;
        digestP.value = KeyParameterValue::make<KeyParameterValue::digest>(t.digest);
        opParams.push_back(digestP);

        KeyParameter padP;
        padP.tag   = Tag::PADDING;
        padP.value = KeyParameterValue::make<KeyParameterValue::paddingMode>(t.pad);
        opParams.push_back(padP);

        CreateOperationResponse opResp;
        status = entryResponse.iSecurityLevel->createOperation(
            keyDesc, opParams, true, &opResp);
        int excCode = status.getExceptionCode();
        int svcErr  = status.getServiceSpecificError();
        printf("createOperation(%s/%s): isOk=%d exc=%d svc=%d\n",
               t.dname, t.pname, status.isOk(), excCode, svcErr);

        if (status.isOk() && opResp.iOperation) {
            const char *testData = "TLS_handshake_test_data";
            size_t testDataLen = strlen(testData);
            auto input = std::vector<uint8_t>(testData, testData + testDataLen);
            std::optional<std::vector<uint8_t>> outputSig;

            ScopedAStatus finStatus = opResp.iOperation->finish(
                input, std::nullopt, &outputSig);
            if (finStatus.isOk() && outputSig.has_value()) {
                printf("PASS: TEE 签名成功! %s/%s 长度=%zu\n",
                       t.dname, t.pname, outputSig.value().size());
                signOk = true;
                break;
            } else {
                printf("  finish 失败: isOk=%d\n", finStatus.isOk());
            }
        }
    }

    if (!signOk) {
        printf("FAIL: 所有 digest 模式签名均失败\n");
        printf("模拟器 KeyMint 可能不支持所需签名模式\n");
        printf("建议在真机 (TEE 硬件) 上测试\n");
        return 1;
    }
    printf("=== 全部测试通过 ===\n");
    return 0;
}
