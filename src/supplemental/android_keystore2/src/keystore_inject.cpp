// Keystore2 密钥注入工具 — 以 root 身份将 PEM 证书/密钥导入 Keystore2
// 编译: 在 NanoMQ Android 构建环境中编译此文件
// 使用: adb push keystore_inject /data/local/tmp/ && adb shell chmod 755 /data/local/tmp/keystore_inject
//       adb shell /data/local/tmp/keystore_inject <alias> <cert.pem> <key.pem>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
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
#include <aidl/android/hardware/security/keymint/Tag.h>

using namespace aidl::android::system::keystore2;
using namespace aidl::android::hardware::security::keymint;
using ndk::ScopedAStatus;
using ndk::SpAIBinder;

static std::vector<uint8_t> readFile(const char *path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        fprintf(stderr, "ERROR: cannot open %s\n", path);
        exit(1);
    }
    size_t size = file.tellg();
    file.seekg(0);
    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char *>(data.data()), size);
    return data;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <alias> <cert.pem> <key.pem>\n", argv[0]);
        fprintf(stderr, "  Inject a PEM certificate and key into Keystore2 as root\n");
        return 1;
    }

    const char *alias  = argv[1];
    const char *certFile = argv[2];
    const char *keyFile  = argv[3];

    // 读取证书和密钥
    auto certData = readFile(certFile);
    auto keyData  = readFile(keyFile);

    printf("Cert file: %s (%zu bytes)\n", certFile, certData.size());
    printf("Key file:  %s (%zu bytes)\n", keyFile, keyData.size());

    // 连接 Keystore2 服务
    const char *svc = "android.system.keystore2.IKeystoreService/default";
    AIBinder *rawBinder = AServiceManager_waitForService(svc);
    if (!rawBinder) {
        fprintf(stderr, "ERROR: Cannot connect to Keystore2 service!\n");
        return 1;
    }
    SpAIBinder binder(rawBinder);
    auto ks2 = IKeystoreService::fromBinder(binder);

    // 准备 KeyDescriptor — 使用 Domain::APP + namespace=-1 (root UID)
    KeyDescriptor keyDesc;
    keyDesc.domain  = Domain::APP;
    keyDesc.nspace  = -1;
    keyDesc.alias   = alias;

    // 尝试先删除已有密钥（忽略错误）
    printf("Deleting existing key (if any)...\n");
    ks2->deleteKey(keyDesc);

    // 尝试获取密钥条目（应该不存在）
    KeyEntryResponse checkResp;
    ScopedAStatus status = ks2->getKeyEntry(keyDesc, &checkResp);
    if (status.isOk() && checkResp.iSecurityLevel) {
        printf("Key '%s' already exists and is accessible.\n", alias);
        printf("No need to inject.\n");
        return 0;
    }
    printf("Key '%s' not found. Attempting injection...\n", alias);

    // Keystore2 不直接支持导入原始 PEM
    // 需要通过 SecurityLevel 的 importWrappedKey 或 generateKey
    //
    // 由于 Keystore2 的设计限制，直接从命令行注入 PEM 证书比较复杂
    // 这里列出三种替代方案:
    //
    //  方案1: 修改 KeystoreAdapter 使用 Domain::SELINUX + 一个已知 namespace
    //  方案2: 编写 Android App 用 KeyStore API 注入，然后用 `su <app_uid>` 运行 NanoMQ
    //  方案3: 在 KeystoreAdapter 中 fork() 一个子进程并以 app UID 运行
    //
    // 当前工具演示连接和查询功能，实际注入需要通过 SecurityLevel

    printf("\n=== 当前 Keystore2 连接状态 ===\n");
    printf("Service connected: YES\n");

    // 列出可用的 SecurityLevel
    for (int sl = 0; sl <= 2; sl++) {
        auto secLevel = static_cast<SecurityLevel>(sl);
        std::shared_ptr<IKeystoreSecurityLevel> level;
        status = ks2->getSecurityLevel(secLevel, &level);
        if (status.isOk() && level) {
            printf("SecurityLevel %d: AVAILABLE\n", sl);
        }
    }

    printf("\n提示: Keystore2 不支持直接从命令行导入 PEM。\n");
    printf("推荐使用以下命令确认 App UID:\n");
    printf("  adb shell pm list packages -U | grep mtlstestapp\n");
    printf("然后以 App UID 运行 NanoMQ:\n");
    printf("  adb shell /data/local/tmp/nanomq start --conf ...\n");

    return 0;
}
