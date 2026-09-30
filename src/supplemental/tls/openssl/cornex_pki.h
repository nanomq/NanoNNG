//
// Cornex (楚能) PKI adapter for the nng OpenSSL TLS engine.
//
// 身份证书与私钥都来自 Cornex PKI SDK / TEE：
//   pkiLibraryInit()          初始化 SDK
//   easysec OpenSSL engine    提供客户端身份证书与 TEE 内的私钥
//   getX509CertChain()        提供证书链（信任锚；中间 CA 一并作为自身链发出）
//
// 相关决策见 docs/cornex-pki-qnx-integration.md 与
// docs/adr/0001-openssl-3x-for-cornex-pki.md（TLS 层必须是 OpenSSL 3.x）。
//

#ifndef NNG_TLS_OPENSSL_CORNEX_PKI_H
#define NNG_TLS_OPENSSL_CORNEX_PKI_H

#include <openssl/ssl.h>

// 进程启动期初始化：pkiLibraryInit() + 加载 easysec engine。
// 失败返回非 0，调用方应让初始化失败（见集成文档 D9）。
extern int nni_cornex_pki_init(void);

// 给客户端 SSL_CTX 装上 PKI 身份（身份证书 + TEE 私钥）与 SDK 证书链。
// 只在 client 模式的 config 上调用。
extern int nni_cornex_pki_apply(SSL_CTX *ctx);

#endif // NNG_TLS_OPENSSL_CORNEX_PKI_H
