//
// Created by zbk on 2022/8/31.
//

//PKI SDK version 1.0.0

#ifndef INC_509M_CN_X509_API_H
#define INC_509M_CN_X509_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief 通用密钥类型枚举
 *
 * 上层仅使用业务密钥类型，实际的 CCU keyName 由 SDK 内部维护。
 */
#ifndef CN_X509_KEY_TYPE_DEFINED
#define CN_X509_KEY_TYPE_DEFINED
typedef enum {
    X509_KEY_TYPE_DiaConfigScriptDecKey = 0,  /* 诊断配置脚本解密密钥：AES-256-GCM */
    X509_KEY_TYPE_DCScriptDecKey = 1,         /* 数采配置脚本解密密钥：AES-256-GCM */
    /* 数值 2 已废弃，保留用于兼容旧版 ABI，不得重新分配。 */
    X509_KEY_TYPE_OtaPackageKEK = 3,          /* OTA 解密密钥的密钥：AES-256-CBC */
    X509_KEY_TYPE_EcbCryptoKey = 4,           /* 通用加密密钥：AES-256-ECB-PKCS7 */
    X509_KEY_TYPE_MAX = 5
} X509KeyType_t;
#endif



typedef struct HashContext HashContext;
typedef struct AesContext AesContext;

typedef enum {
    HASH_ALGORITHM_NONE = 1,
    HASH_ALGORITHM_SHA1,
    HASH_ALGORITHM_SHA256,
    HASH_ALGORITHM_SHA224,
    HASH_ALGORITHM_SHA384,
    HASH_ALGORITHM_SHA512,
    HASH_ALGORITHM_MD5,
    HASH_ALGORITHM_MAX,
    HASH_ALGORITHM_INVALID = 0x7FFFFFFF
} HashAlgorithm_t;


typedef enum {
    AES_OPERATION_ENCRYPT = 0,
    AES_OPERATION_DECRYPT = 1
} AesOperation_t;

typedef enum {
    CN_X509_PADDING_PKCS7 = 1,
    CN_X509_PADDING_NO_PAD = 2
} CnX509Padding_t;

#ifdef  __cplusplus
extern "C" {
#endif



/**
 * @brief 初始化流式哈希上下文
 *
 * @param[out] context   输出哈希上下文指针
 * @param[in]  algorithm  哈希算法
 *
 * @return 错误码
 * @retval 0       初始化成功
 * @retval 非0     初始化失败
 */
int HashInit(HashContext **context, HashAlgorithm_t algorithm);

/**
 * @brief 向流式哈希上下文追加一段数据
 *
 * @param[in] ctx   已初始化的哈希上下文
 * @param[in] data  待计算的数据
 * @param[in] len   数据长度
 *
 * @return 错误码
 * @retval 0       更新成功
 * @retval 非0     更新失败
 */
int HashUpdate(HashContext *context, const uint8_t *data, size_t len);

/**
 * @brief 完成流式哈希计算并输出摘要
 *
 * @param[in]  ctx        已初始化的哈希上下文
 * @param[out] digest     摘要输出缓冲区
 * @param[in,out] digestlen 输入为缓冲区容量，输出为摘要实际长度
 *
 * @return 错误码
 * @retval 0       计算成功
 * @retval 非0     计算失败
 */
int HashFinal(HashContext *context, uint8_t *digest, size_t *digestlen);

/**
 * @brief 释放流式哈希上下文
 *
 * @param[in] context  HashInit 初始化成功后返回的上下文
 */
void HashFree(HashContext *context);

/**
 * @brief 使用已灌装的业务密钥初始化流式 AES-GCM 或 AES-CBC 上下文
 *
 * SDK 根据 keyType 在内部获取固定 keyName 并从 CCU 安全存储读取密钥。
 * 密钥对应的模式由 SDK 配置决定：0～2 为 GCM，3 为 CBC；类型4的
 * ECB 密钥不适用于当前流式接口。GCM 使用12字节 nonce，CBC 使用16字节IV。
 *
 * @param[out] ctx     输出 AES 上下文指针
 * @param[in]  keyType 已通过 importKeyGeneric 灌装的业务密钥类型
 * @param[in]  operation AES_OPERATION_ENCRYPT 或 AES_OPERATION_DECRYPT
 * @param[in]  iv      初始化向量或 GCM nonce
 * @param[in]  ivlen   初始化向量或 nonce 长度
 *
 * @return 错误码
 * @retval 0       初始化成功
 * @retval 非0     初始化失败
 */
int AesInit(AesContext **context,
                 X509KeyType_t keyType, AesOperation_t operation,
                 const uint8_t *iv, size_t ivlen);

/**
 * @brief 向 GCM 上下文追加附加认证数据
 *
 * 必须在第一次调用 AesUpdate 之前完成。CBC 模式不支持 AAD。
 *
 * @param[in] ctx     已初始化的 AES 上下文
 * @param[in] aad     附加认证数据
 * @param[in] aadlen  附加认证数据长度
 *
 * @return 错误码
 * @retval 0                  更新成功
 * @retval 其他非0             更新失败
 */
int AesUpdateAad(AesContext *context,
                               const uint8_t *aad, size_t aadlen);

/**
 * @brief 处理一段流式 AES 输入数据
 *
 * @param[in]  ctx     已初始化的 AES 上下文
 * @param[in]  in      输入数据
 * @param[in]  inlen   输入数据长度
 * @param[out] out     输出缓冲区
 * @param[in,out] outlen 输入为输出缓冲区容量，输出为实际输出长度
 *
 * @return 错误码
 * @retval 0       处理成功
 * @retval 非0     处理失败
 */
int AesUpdate(AesContext *context,
                   const uint8_t *in, size_t inlen,
                   uint8_t *out, size_t *outlen);

/**
 * @brief 完成流式 AES 操作并生成或校验 GCM 标签
 *
 * GCM 加密时由接口生成 tag；GCM 解密时 tag 和 taglen 用于认证校验。
 * CBC 模式下 tag 和 taglen 必须传 NULL。
 *
 * @param[in]  ctx          已初始化的 AES 上下文
 * @param[out] out          final 阶段输出缓冲区
 * @param[in,out] outlen    输入为缓冲区容量，输出为实际输出长度
 * @param[in,out] tag       GCM 认证标签；加密时输出，解密时输入
 * @param[in,out] taglen    输入/输出认证标签长度
 *
 * @return 错误码
 * @retval 0          完成成功
 * @retval 其他非0    完成失败
 */
int AesFinal(AesContext *context,
                  uint8_t *out, size_t *outlen,
                  uint8_t *tag, size_t *taglen);

/**
 * @brief 释放流式 AES 上下文
 *
 * @param[in] context  AesInit 初始化成功后返回的上下文
 */
void AesFree(AesContext *context);

/**@enum CertState
* @brief 证书状态
*/

typedef enum{
    AVAILABLE_AND_ACTIVE,
    NO_CERTIFICATE_AVAILABLE,
    DAMAGED,
    AVAILABLE_AND_INACTIVE,
    DEVELOP_CERT
}CertState;

/**@struct CertInfo
* @brief 证书信息
*/
typedef struct{
    CertState certState;
    char certStartTime[32];
    char certEndTime[32];
    char cn[32];
    char serialNumber[64];
    char issuer[128];
    char subject[128];
}CertInfo;

/**
 * @brief 初始化
 *
 * @return 初始化结果
 *
 * @retval true成功，false失败
 */
bool pkiLibraryInit();


/**
* @brief 获取证书状态
*
* @param[out]	certInfo	证书信息
*
* @return 错误码
* @retval 0-成功 其他-错误
*/
int32_t getX509CertState(CertInfo *certInfo);

/**
* @brief 产生证书更新请求
*
* @param[out]	certApplyReq	证书请求
* @param[in,out]	certApplyReqLen	证书请求长度
*
* @return 错误码
* @retval 0-成功 其他-错误
*/
int32_t generateX509CertUpdateRequest(uint8_t *certApplyReq, int32_t *certApplyReqLen);

/**
* @brief 更新证书
*
* @param[in]	certData	证书数据
* @param[in]	certDataLen	证书数据长度
*
* @return 错误码
* @retval 0-成功 其他-错误
*/
int32_t updateIdentityCert(uint8_t *certData, int32_t certDataLen);

/**
* @brief 获取证书
*
* @param[out]	certData	证书数据
* @param[in,out]	certDataLen	证书数据长度
*
* @return 错误码
* @retval 0-成功 其他-错误
*/
int32_t getIdentityCert(uint8_t *certData, int32_t* certDataLen);

/**
* @brief 获取证书链
*
* @param[out]	certChainData	证书链数据
* @param[in,out]	certChainDataLen	证书链数据长度
*
* @return 错误码
* @retval 0-成功 其他-错误
*/
int32_t getX509CertChain( uint8_t *certChainData, int32_t* certChainDataLen);

/**
* @brief P7验签
**@param[in]	data	原文数据
* @param[in]	dataLen	原文数据长度
* @param[in]	sig		签名数据
* @param[in]	sigLen	签名数据长度
* @param[out]	certInfo	证书信息
*
* @return 错误码
* @retval 0-成功 其他-错误
*/
int32_t verifyP7Signature( uint8_t * data, uint32_t dataLen,  uint8_t *sig, uint32_t sigLen,CertInfo *certInfo);

/**
* @brief  获取SDK版本号
**@param[out]   buffer   缓冲区
*@param[in,out]   bufferLen   缓冲区长度
*
*
* @return 错误码
* @retval 0-成功 其他-错误
*/
int32_t getX509SDKVersion(char* buffer, uint32_t *bufferLen);

/**
 * @brief 产生证书请求
 *
 * @param[in]  id        唯一标识
 * @param[out] certApplyReq               证书请求
 * @param[in,out] certApplyReqLen               证书请求长度
 *
* @return 错误码
* @retval 0-成功 其他-错误
 */
int32_t generateX509CertApplyRequest( char *id, char *certApplyReq, int32_t *certApplyReqLen);

/**
 * @brief 证书导入
 *
 * @param[in]  id      唯一标识
 * @param[in]  certData         证书数据
 * @param[in,out]  certDataLen         证书数据长度
 *
* @return 错误码
* @retval 0-成功 其他-错误
 */
int32_t importIdentityCert(char* id, uint8_t *certData, int32_t certDataLen);

/**
 * @brief OTA密钥解密
 *
 * @param[in]  keyCipher      密钥密文
 * @param[in]  keyCipherLen   密钥密文长度
 * @param[in]  mode           模式（0-ECB，1-CBC）
 * @param[in]  iv             初始向量（CBC模式使用）
 * @param[out] keyData        密钥数据
 * @param[in,out] keyDataLen  输入为缓冲区容量，输出为密钥数据长度
 *
 * @return 错误码
 * @retval 0-成功 其他-错误
 */
int32_t decryptEncKeyByOTAKey(uint8_t *keyCipher, int32_t keyCipherLen, uint8_t mode,
                             uint8_t *iv, uint8_t *keyData, int32_t *keyDataLen);

/**
 * @brief OTA密钥导入
 *
 * @param[in] keyData     密钥数据
 * @param[in] keyDataLen  密钥数据长度
 *
 * @return 错误码
 * @retval 0-成功 其他-错误
 */
int32_t importOTAKey(uint8_t *keyData, int32_t keyDataLen);

/**
 * @brief 获取OTA密钥状态
 *
 * @param[out] keyState  密钥状态
 *
 * @return 错误码
 * @retval 0-成功 其他-错误
 */
int32_t getOTAKeyState(uint8_t *keyState);

/**
 * @brief 通用密钥导入
 *
 * @param[in]  keyType          密钥类型
 * @param[in]  keyData          密钥数据
 * @param[in]  keyDataLen       密钥数据长度
 *                              当前所有业务密钥均为 AES-256，长度必须为32字节；
 *                              实际长度以 SDK 内置业务密钥配置为准
 *
 * @return 错误码
 * @retval 0-成功 其他-错误
 */
int32_t importKeyGeneric(X509KeyType_t keyType, uint8_t *keyData, uint16_t keyDataLen);

/**
 * @brief 获取通用密钥状态
 *
 * 通过能否从 CCU TEE 正常导出指定密钥判断密钥是否存在，导出的密钥数据不会返回给调用方。
 *
 * @param[in]  keyType     密钥类型
 * @param[out] keyState    密钥状态：0-密钥存在，无需灌装；非0-密钥不存在，需要灌装
 *
 * @return 错误码
 * @retval 0-成功 其他-错误
 */
int32_t getCryptoKeyState(X509KeyType_t keyType, uint8_t *keyState);

/**
 * @brief 通用加解密参数结构体
 *
 * ECB 不使用 IV、AAD 和 Tag，对应指针应传 NULL、长度应传 0。
 * padding 可设置为 CN_X509_PADDING_PKCS7 或 CN_X509_PADDING_NO_PAD，
 * 用于选择 GCM/CBC/ECB 的填充方式。padding 为 0（默认值）或其他未支持值时，
 * SDK 使用对应密钥配置中的默认填充方式，以保持既有调用行为不变。
 */
typedef struct {
    X509KeyType_t keyType;      /* 密钥类型 */
    uint8_t *in;                /* 输入数据 */
    uint32_t inLen;             /* 输入数据长度 */
    uint8_t *iv;                /* 初始向量（ECB传NULL, CBC传16字节, GCM传12字节） */
    uint32_t ivLen;             /* IV长度 */
    uint8_t *aad;               /* 附加认证数据（GCM专用，其他传NULL） */
    uint32_t aadLen;            /* AAD长度 */
    uint8_t *tag;               /* 认证标签（GCM专用） */
    uint32_t tagLen;            /* Tag长度（GCM默认16字节） */
    uint32_t padding;           /* 可选填充：CN_X509_PADDING_PKCS7/CN_X509_PADDING_NO_PAD；0或非法值使用密钥默认值 */
} X509SymCryptoParam_t;

/**
 * @brief 通用加解密
 *
 * @param[in,out] param  加解密参数结构体
 * @param[out] out       输出数据
 * @param[in,out] outLen 输入时为输出缓冲区容量，输出时为实际数据长度
 * @param[in]  encDec    0-加密, 1-解密
 *
 * @return 错误码
 * @retval 0-成功 其他-错误
 */
int32_t symCryptoGeneric(X509SymCryptoParam_t *param, uint8_t *out, uint32_t *outLen, uint8_t encDec);

#ifdef  __cplusplus
}
#endif

#endif //INC_509M_CN_X509_API_H
