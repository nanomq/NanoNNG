#ifndef PKI_ADAPTER_H
#define PKI_ADAPTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 初始化 Binder 线程池 (必须在主线程调用一次)
void keystore2_init(void);

// 硬件签名接口 (对接 TLS 1.2/1.3 ClientCertificateVerify)
// @param alias: 证书别名 (如 "ecu-client-certificate")
// @param namespace_id: SELinux 命名空间 (-1 代表当前 APP Domain，30000 供实车跨域使用)
// @param digest: TLS 协议栈(BoringSSL)算好的摘要数据
// @param sig_alg: BoringSSL 传入的 TLS SignatureScheme (如 0x0401=RSA-PKCS1-SHA256)
// @param sig_out: 接收 TEE 硬件签名的缓冲区
int keystore2_sign(const char *alias, int namespace_id,
		   const uint8_t *digest, int digest_len,
		   uint16_t sig_alg,
		   uint8_t *sig_out, int sig_max);

// 提取公钥证书链接口 (对接 TLS 握手时的 Client Certificate 发送)
int keystore2_get_cert(const char *alias, int namespace_id,
		       uint8_t *cert_out, int cert_max);

// 获取客户端完整证书链 (DER 连续拼接: leaf + intermediate CA(s))
// 当外部未配置 CA 证书时，用作构建 X509_STORE 的信任锚
// 返回 DER blob 长度，失败返回 -1
int keystore2_get_cert_chain(const char *alias, int namespace_id,
			      uint8_t *chain_out, int chain_max);

// 运行期覆盖 Keystore2 配置 (在 open_config_init 之前调用)
// 不调用此函数时使用编译期宏默认值
void keystore2_engine_set_config(const char *alias, int namespace_id, bool digest_none);

// 运行期覆盖 DIGEST_NONE 模式 (TEE 硬件签名 vs 软件 KeyMint)
void keystore2_set_digest_none(bool val);

#ifdef __cplusplus
}
#endif

#endif // PKI_ADAPTER_H
