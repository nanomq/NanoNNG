# Parquet Metadata 设计（V1）

## 1. 背景与目标

当前 Parquet 文件已具备加密与查询能力，但文件本身缺少可复用的业务上下文。外部用户拿到单个文件时，通常仍需依赖运行时配置或文件名规则才能完成解析与解密，存在迁移和排障成本。

本方案目标：

- 将 topic、加密关键信息、元数据版本等写入 Parquet metadata。
- 保证旧文件（未写 metadata）在历史查询链路下可继续读取。
- 在不泄露明文密钥的前提下，提升文件自描述与跨环境可移植性。

## 2. 设计原则

- 向后兼容：新 reader 必须兼容旧文件；metadata 缺失不能导致历史查询失败。
- 安全优先：禁止写入明文 data key，只允许写入密钥密文（wrapped key）及解包描述。
- 渐进落地：先写后读，分阶段上线，避免一次性改动影响生产路径。
- 可观测性：metadata 命中率、回退率、校验失败率需要可观测。
- 低侵入：优先复用现有写入点与读取路径，减少协议和接口变更。

## 3. 元数据模型（V1）

建议统一使用 `nmq.*` 命名空间，避免与第三方 key 冲突。

### 3.1 必选字段

- `nmq.meta.version` : `1`
- `nmq.topic` : 主题名
- `nmq.enc.cipher` : 数据加密方式（如 `AES_GCM_V1` / `AES_GCM_CTR_V1`）
- `nmq.key.id` : 密钥标识（key id）
- `nmq.key.wrap_alg` : 密钥封装算法（当前为 `NMQ_CONF_CIPHER_AES_GCM_BASE64`）
- `nmq.key.wrapped` : 配置文件中的密文 key（Base64 密文串，非明文 key）

### 3.2 建议字段

- `nmq.created_by` : `NanoMQ`
- `nmq.created_at` : Unix 时间戳（秒或毫秒，需统一）
- `nmq.file.topic_source` : `metadata|filename|runtime`（便于调试链路）
- `number` : Raw stream parquet 的自增序号（兼容保留字段，非 `nmq.*`）

### 3.3 可选完整性字段（推荐）

- `nmq.meta.sig_alg` : `HMAC-SHA256` / `ECDSA-P256-SHA256`
- `nmq.meta.sig` : 对关键字段签名值（Base64）

## 4. 写入方案

### 4.1 写入位置

在 Parquet 文件写入阶段，通过 `AddKeyValueMetadata` 注入 `nmq.*` 字段。

### 4.2 写入流程

1. 收集 metadata 输入：topic、cipher、key id、wrapped key、wrap alg。
2. 运行时先通过 `conf_parse_cipher` 解密配置中的 parquet key，供落盘与查询使用。
3. 组装 `KeyValueMetadata`，其中 `nmq.key.wrapped` 写入配置密文 key。
4. 可选写入建议字段与完整性字段。
5. 对 Raw stream parquet 同步写入兼容字段 `number`（自增序号）。
6. 执行字段有效性检查（空值、版本合法性、Base64 合法性）。
7. 关闭 writer 并落盘。

### 4.3 失败处理

- metadata 写入失败：本次文件写入失败，返回错误，不生成半成品文件。
- 非必选字段失败：记录告警，不阻塞写入。

## 5. 读取与回退方案（兼容旧文件）

### 5.1 读取优先级

读取侧采用“metadata 优先、旧逻辑回退”：

1. 尝试读取 `nmq.meta.version`。
2. 若 metadata 可用且字段完整，优先使用 metadata 信息。
3. 若 metadata 缺失/损坏/字段不足，回退旧逻辑（文件名解析 + 运行时配置）。

### 5.2 字段回退规则

- topic：`nmq.topic` -> 文件名解析 -> 运行时上下文。
- cipher：`nmq.enc.cipher` -> `conf->encryption.type`。
- key id：`nmq.key.id` -> `conf->encryption.key_id`。
- wrapped key：`nmq.key.wrapped` 缺失时，沿用旧解密路径或本地配置。
- plaintext key：统一使用 `conf_parse_cipher` 已解密后的 `conf->encryption.key` 进行查询解密。

### 5.3 容错策略

- 不识别 `nmq.meta.version`：按旧文件处理。
- 可选字段缺失：记录 `debug/warn`，不失败。
- 签名校验失败（若启用）：记录 `warn/error`，可配置为“拒绝读取”或“回退读取”。

## 6. 历史查询影响评估

- 对旧文件：无 metadata，查询行为保持不变。
- 对新文件：可直接使用 metadata，减少对文件名规则的耦合。
- 对混合数据集：同一查询中允许新旧文件并存，逐文件判定并回退。

结论：该方案为增强型兼容，不会破坏既有历史查询能力。

## 7. 安全与合规

- 明确禁止存储明文 data key、口令、token 等敏感信息。
- `nmq.key.wrapped` 仅为密钥密文，要求由 KMS/HSM 或公钥机制生成。
- 若启用 `nmq.meta.sig`，签名覆盖至少包含所有必选字段，防止字段替换攻击。
- 日志中不得打印完整 `nmq.key.wrapped` 与签名值。

## 8. 迁移与上线计划

### 阶段 A：写入增强

- 新写入文件带 `nmq.*` metadata。
- 读取路径暂不强依赖 metadata。

### 阶段 B：读取增强

- 读取链路启用 metadata 优先策略。
- 开启回退日志与指标采集。

### 阶段 C：安全增强

- 开启签名校验能力（可配置开关）。
- 对异常 metadata 增加审计告警。

## 9. 测试与验收标准

### 9.1 测试用例建议

- 新文件写入后可读取全部必选字段。
- 旧文件无 metadata，历史查询结果与改造前一致。
- metadata 部分缺失时可回退读取。
- metadata 篡改时可被识别并按策略处理。

### 9.2 验收标准

- 新写入链路稳定，无额外崩溃与性能回退。
- 混合新旧文件查询通过率为 100%（功能等价）。
- 日志与指标可准确体现 metadata 命中/回退情况。

## 10. 风险与开放问题

- wrapped key 的来源和解包责任边界（NanoMQ 还是外部服务）需明确。
- `nmq.created_at` 时间单位需全局统一（建议毫秒）。
- 签名算法默认值与密钥管理策略需与安全团队对齐。
