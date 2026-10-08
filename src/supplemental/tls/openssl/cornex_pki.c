//
// Cornex (楚能) PKI adapter for the nng OpenSSL TLS engine.
//
// 设计要点（详见 docs/cornex-pki-qnx-integration.md）：
//   - D5/D8：用 easysec ENGINE 取身份；engine 由代码显式加载（不用 openssl.cnf）
//   - D6   ：信任锚用 SDK 的证书链；中间 CA 额外作为自身链发出
//   - D9   ：启动期初始化，SDK/engine 不可用即让 nng 初始化失败
//

#include <stdlib.h>
#include <string.h>

// OpenSSL 3.x 把 ENGINE API 标为 deprecated，但楚能 PKI 只提供 engine 形态，
// 这里是有意使用（见 ADR-0001）。屏蔽本 translation unit 的 deprecated 告警。
#define OPENSSL_SUPPRESS_DEPRECATED

#include <openssl/bio.h>
#include <openssl/engine.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/ui.h>
#include <openssl/x509.h>
#include <openssl/x509err.h>
#include <openssl/x509v3.h> // X509_check_issued()

#include "core/nng_impl.h"

#include "cn_x509_api.h"
#include "cornex_pki.h"

// 编译期默认 engine 路径；运行期可用环境变量 NANOMQ_CORNEX_PKI_ENGINE 覆盖
#ifndef NANOMQ_CORNEX_PKI_ENGINE_PATH
#define NANOMQ_CORNEX_PKI_ENGINE_PATH "/oemapp/lib/libcn_x509_engine.so"
#endif

#define CORNEX_PKI_ENGINE_ID "easysec"
// ENGINE_load_private_key 的 key id，见厂商 client_socket.c 示例
#define CORNEX_PKI_KEY_ID "ec_key"
#define CORNEX_PKI_CHAIN_MAX 8192

static ENGINE *cornex_engine = NULL;

static void
cornex_pki_log_errors(const char *what)
{
	unsigned long err;
	char          buf[256];

	while ((err = ERR_get_error()) != 0) {
		ERR_error_string_n(err, buf, sizeof(buf));
		log_error("NNG-TLS-CORNEX" "%s: %s", what, buf);
	}
}

// 显式加载 dynamic engine（D8）：engine 的加载不经过配置文件（openssl.cnf / OPENSSL_CONF）
static ENGINE *
cornex_pki_load_engine(void)
{
	const char *path;
	ENGINE     *e;

	// 可能已被同进程其它组件加载过
	if ((e = ENGINE_by_id(CORNEX_PKI_ENGINE_ID)) != NULL) {
		log_info("NNG-TLS-CORNEX" "engine %s already loaded",
		    CORNEX_PKI_ENGINE_ID);
		return (e);
	}

	path = getenv("NANOMQ_CORNEX_PKI_ENGINE");
	if (path == NULL || path[0] == '\0') {
		path = NANOMQ_CORNEX_PKI_ENGINE_PATH;
	}

	ENGINE_load_dynamic();
	if ((e = ENGINE_by_id("dynamic")) == NULL) {
		log_error("NNG-TLS-CORNEX" "ENGINE_by_id(dynamic) failed");
		cornex_pki_log_errors("ENGINE_by_id(dynamic)");
		return (NULL);
	}
	if (ENGINE_ctrl_cmd_string(e, "SO_PATH", path, 0) != 1 ||
	    ENGINE_ctrl_cmd_string(e, "ID", CORNEX_PKI_ENGINE_ID, 0) != 1 ||
	    ENGINE_ctrl_cmd_string(e, "LIST_ADD", "1", 0) != 1 ||
	    ENGINE_ctrl_cmd_string(e, "LOAD", NULL, 0) != 1) {
		log_error("NNG-TLS-CORNEX" "failed to load engine from %s", path);
		cornex_pki_log_errors("dynamic engine ctrl");
		ENGINE_free(e);
		return (NULL);
	}
	if (ENGINE_init(e) != 1) {
		log_error("NNG-TLS-CORNEX" "ENGINE_init failed for %s", path);
		cornex_pki_log_errors("ENGINE_init");
		ENGINE_free(e);
		return (NULL);
	}
	// 释放 structural 引用；ENGINE_init() 持有的 functional 引用让 e 继续有效
	ENGINE_free(e);
	log_info("NNG-TLS-CORNEX" "engine %s loaded from %s",
	    CORNEX_PKI_ENGINE_ID, path);
	return (e);
}

// 只在 nng_tls_engine_init_open()（进程启动、单线程）里调用一次，因此这里不加锁；
// 若将来改成懒加载/多线程调用，需要补锁。
int
nni_cornex_pki_init(void)
{
	if (cornex_engine != NULL) {
		return (0);
	}
	if (!pkiLibraryInit()) {
		log_error("NNG-TLS-CORNEX" "pkiLibraryInit() failed");
		return (NNG_ECRYPTO);
	}
	if ((cornex_engine = cornex_pki_load_engine()) == NULL) {
		return (NNG_ECRYPTO);
	}
	return (0);
}

static int
cornex_pki_install_identity(SSL_CTX *ctx)
{
	X509     *cert = NULL;
	EVP_PKEY *pkey = NULL;

	// 身份证书由 engine 提供（底层走 SDK 存储）
	if (ENGINE_load_ssl_client_cert(
	        cornex_engine, NULL, NULL, &cert, NULL, NULL, NULL, NULL) != 1) {
		log_error("NNG-TLS-CORNEX" "ENGINE_load_ssl_client_cert failed");
		cornex_pki_log_errors("ENGINE_load_ssl_client_cert");
		return (NNG_ECRYPTO);
	}
	if (SSL_CTX_use_certificate(ctx, cert) != 1) {
		log_error("NNG-TLS-CORNEX" "SSL_CTX_use_certificate failed");
		cornex_pki_log_errors("SSL_CTX_use_certificate");
		X509_free(cert);
		return (NNG_ECRYPTO);
	}
	X509_free(cert);

	// 私钥永不离开 TEE：这里拿到的是 engine 支撑的 EVP_PKEY
	if ((pkey = ENGINE_load_private_key(
	         cornex_engine, CORNEX_PKI_KEY_ID, UI_OpenSSL(), NULL)) == NULL) {
		log_error("NNG-TLS-CORNEX" "ENGINE_load_private_key(%s) failed",
		    CORNEX_PKI_KEY_ID);
		cornex_pki_log_errors("ENGINE_load_private_key");
		return (NNG_ECRYPTO);
	}
	if (SSL_CTX_use_PrivateKey(ctx, pkey) != 1) {
		log_error("NNG-TLS-CORNEX" "SSL_CTX_use_PrivateKey failed");
		cornex_pki_log_errors("SSL_CTX_use_PrivateKey");
		EVP_PKEY_free(pkey);
		return (NNG_ECRYPTO);
	}
	EVP_PKEY_free(pkey);

	if (SSL_CTX_check_private_key(ctx) != 1) {
		log_error("NNG-TLS-CORNEX" "private key does not match certificate");
		cornex_pki_log_errors("SSL_CTX_check_private_key");
		return (NNG_ECRYPTO);
	}
	return (0);
}

static int
cornex_pki_install_chain(SSL_CTX *ctx)
{
	uint8_t   *buf;
	int32_t    len = CORNEX_PKI_CHAIN_MAX;
	BIO       *bio;
	X509      *cert;
	X509_STORE *store;
	int        rv = 0;

	if ((buf = nni_zalloc(CORNEX_PKI_CHAIN_MAX)) == NULL) {
		return (NNG_ENOMEM);
	}
	if (getX509CertChain(buf, &len) != 0 || len <= 0) {
		log_error("NNG-TLS-CORNEX" "getX509CertChain failed (%d)", len);
		nni_free(buf, CORNEX_PKI_CHAIN_MAX);
		return (NNG_ECRYPTO);
	}
	if ((bio = BIO_new_mem_buf(buf, len)) == NULL) {
		nni_free(buf, CORNEX_PKI_CHAIN_MAX);
		return (NNG_ENOMEM);
	}
	if ((store = X509_STORE_new()) == NULL) {
		BIO_free(bio);
		nni_free(buf, CORNEX_PKI_CHAIN_MAX);
		return (NNG_ENOMEM);
	}

	while ((cert = PEM_read_bio_X509(bio, NULL, 0, NULL)) != NULL) {
		// 信任锚：用于校验对端服务端（D6）
		if (X509_STORE_add_cert(store, cert) != 1) {
			unsigned long err = ERR_peek_last_error();
			// 链里有重复项不是错误，忽略即可
			if (ERR_GET_REASON(err) != X509_R_CERT_ALREADY_IN_HASH_TABLE) {
				log_error("NNG-TLS-CORNEX" "X509_STORE_add_cert failed");
				cornex_pki_log_errors("X509_STORE_add_cert");
				X509_free(cert);
				rv = NNG_ECRYPTO;
				break;
			}
			ERR_clear_error();
		}
		// 自身链：把中间 CA 一并发出；自签发的根（issuer==subject，X509_check_issued 判定）不加（D6）
		if (X509_check_issued(cert, cert) != X509_V_OK) {
			(void) SSL_CTX_add1_chain_cert(ctx, cert);
		}
		X509_free(cert);
	}
	ERR_clear_error(); // PEM 读到 EOF 会留下错误码

	SSL_CTX_set_cert_store(ctx, store); // store 交给 ctx 管理
	BIO_free(bio);
	nni_free(buf, CORNEX_PKI_CHAIN_MAX);
	return (rv);
}

int
nni_cornex_pki_apply(SSL_CTX *ctx)
{
	int rv;

	if ((rv = nni_cornex_pki_init()) != 0) {
		return (rv);
	}
	if ((rv = cornex_pki_install_identity(ctx)) != 0) {
		return (rv);
	}
	if ((rv = cornex_pki_install_chain(ctx)) != 0) {
		return (rv);
	}
	log_info("NNG-TLS-CORNEX" "client identity installed (PKI/TEE)");
	return (0);
}
