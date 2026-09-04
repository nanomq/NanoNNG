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

// BoringSSL — 用于解析 DER 证书链 + 构建 X509_STORE 验证信任链
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/x509_vfy.h>
#include <openssl/pem.h>
#include <openssl/bio.h>

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
    std::vector<uint8_t> leaf_cert;
    if (entryResponse.metadata.certificate.has_value()) {
        leaf_cert = entryResponse.metadata.certificate.value();
        printf("PASS: 证书已获取 (%zu bytes)\n", leaf_cert.size());
    } else {
        printf("WARN: 无证书 (metadata.certificate 为空)\n");
    }

    // 4. 解析证书链 (certificateChain: leaf + intermediate CAs, DER 连续拼接)
    if (entryResponse.metadata.certificateChain.has_value()) {
        auto &chain = entryResponse.metadata.certificateChain.value();
        printf("PASS: 证书链已获取 (%zu bytes)\n", chain.size());

        const uint8_t *cp = chain.data();
        long remaining = (long)chain.size();
        int cert_idx = 0;
        while (remaining > 0) {
            const uint8_t *before = cp;
            X509 *cert = d2i_X509(NULL, &cp, remaining);
            if (!cert) {
                printf("  [%d] 解析失败 at byte %ld\n", cert_idx,
                       (long)(cp - chain.data()));
                break;
            }
            remaining -= (cp - before);

            char *subj = X509_NAME_oneline(X509_get_subject_name(cert), NULL, 0);
            char *issuer = X509_NAME_oneline(X509_get_issuer_name(cert), NULL, 0);
            // 判断证书类型：自签名=root, issuer≠subject=intermediate or leaf
            bool self_signed = (X509_check_issued(cert, cert) == X509_V_OK);
            const char *type = self_signed ? "ROOT CA" :
                (cert_idx == 0) ? "LEAF (推测)" : "INTERMEDIATE CA (推测)";
            printf("  [%d] %s | Subject: %s\n", cert_idx, type, subj);
            printf("       Issuer : %s\n", issuer);

            OPENSSL_free(subj);
            OPENSSL_free(issuer);
            X509_free(cert);
            cert_idx++;
        }
        printf("PASS: 共解析 %d 个证书\n", cert_idx);

        // 5. 构建 X509_STORE 验证信任链 (用中间 CA 验证叶子证书)
        if (cert_idx >= 2 && !leaf_cert.empty()) {
            printf("\n--- X509_STORE 信任链验证 ---\n");

            // 5.1 重新解析叶子证书
            const uint8_t *leaf_ptr = leaf_cert.data();
            X509 *leaf = d2i_X509(NULL, &leaf_ptr, (long)leaf_cert.size());
            if (!leaf) {
                printf("FAIL: 无法重新解析叶子证书\n");
                goto skip_store_verify;
            }

            // 5.2 构建中间 CA 栈 (用于构建非自签发验证链)
            STACK_OF(X509) *intermediates = sk_X509_new_null();

            // 5.3 构建 X509_STORE，加入中间 CA 作为信任锚
            X509_STORE *store = X509_STORE_new();
            const uint8_t *cp2 = chain.data();
            long remaining2 = (long)chain.size();
            int ca_count = 0;
            for (int i = 0; i < cert_idx && remaining2 > 0; i++) {
                const uint8_t *before2 = cp2;
                X509 *cert = d2i_X509(NULL, &cp2, remaining2);
                if (!cert) break;
                remaining2 -= (cp2 - before2);
                if (i > 0) {
                    // 跳过 leaf (idx=0)，中间 CA 加入信任存储
                    X509_STORE_add_cert(store, cert);
                    sk_X509_push(intermediates, cert);  // stack 接管引用
                    ca_count++;
                    printf("  信任锚 #%d: 已加入 X509_STORE\n", ca_count);
                } else {
                    X509_free(cert);  // leaf 不加入 store
                }
            }

            // 5.4 构建验证上下文并执行验证
            X509_STORE_CTX *vfy_ctx = X509_STORE_CTX_new();
            X509_STORE_CTX_init(vfy_ctx, store, leaf, intermediates);

            int verify_result = X509_verify_cert(vfy_ctx);
            int verify_error = X509_STORE_CTX_get_error(vfy_ctx);
            printf("\n  X509_verify_cert 结果: %s (err=%d: %s)\n",
                   verify_result == 1 ? "✓ PASS" : "✗ FAIL",
                   verify_error,
                   X509_verify_cert_error_string(verify_error));

            // 5.5 打印完整验证链 (BoringSSL 构建的链)
            STACK_OF(X509) *built_chain = X509_STORE_CTX_get1_chain(vfy_ctx);
            if (built_chain) {
                int chain_depth = sk_X509_num(built_chain);
                printf("  BoringSSL 构建的验证链 (深度=%d):\n", chain_depth);
                for (int j = 0; j < chain_depth; j++) {
                    X509 *c = sk_X509_value(built_chain, j);
                    char *s = X509_NAME_oneline(X509_get_subject_name(c), NULL, 0);
                    printf("    [%d] %s\n", j, s);
                    OPENSSL_free(s);
                }
                sk_X509_free(built_chain);
            }

            X509_STORE_CTX_free(vfy_ctx);
            X509_free(leaf);
            sk_X509_free(intermediates);
            X509_STORE_free(store);

            if (verify_result != 1) {
                printf("\n说明: 如果验证失败，可能原因:\n");
                printf("  - 中间 CA 不在 certificateChain 中 (仅含 leaf)\n");
                printf("  - 根 CA 不在 chain 中 (Android Keystore2 不存储根 CA)\n");
                printf("  - 证书已过期或尚未生效\n");
            }
        }
skip_store_verify:
        printf("\n");
    } else {
        printf("WARN: 无证书链 (metadata.certificateChain 为空)\n");
        printf("  模拟器通常不填充 certificateChain 字段\n");
        printf("  请在 V216 实车 (TEE 硬件) 上测试完整证书链提取\n");
    }

    // 6. 尝试不同 (digest + padding) 组合签名
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
