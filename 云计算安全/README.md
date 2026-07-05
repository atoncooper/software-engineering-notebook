# 云计算安全 · 面试、原理、学术与工业笔记

> 定位：四层深度并重——
> 1. **面试速查层**（高频问答骨架）
> 2. **原理层**（责任模型、加密、IAM、零信任、网络隔离、供应链等核心机制）
> 3. **学术层**（NIST/ISO/CSA 标准、密码学基础、形式化安全模型、论文）
> 4. **工业层**（云厂商实现、生产事故复盘、CIS Benchmarks、合规落地、CNAPP 实践）
>
> 原则：Correctness > Completeness > Speed；每章按四层递进。
> 约定：⭐高频 🔥工程重点 📜论文/标准 ⚠️易错点 🎓学术深度 🏭工业实战

---

## 0. 阅读指南

- **面试速查** → §21 + 每章「面试要点」
- **系统学习** → §1→§19 顺序，章间有依赖（责任模型 → IAM → 网络数据 → 工作负载 → 态势感知 → 响应 → 合规）
- **标准深读** → 每章「学术/标准」+ §22 标准与论文清单
- **工业实战** → 每章「工业实战」+ §20 生产事故案例库
- **云厂商对照** → §附录 D 五家云厂商服务映射表
- **标记**：⭐ 🔥 📜 ⚠️ 🎓 🏭

### 共享元数据文件

| 文件 | 用途 |
|------|------|
| [_章节模板.md](_章节模板.md) | 新建章节时复制的模板 |
| [_术语表.md](_术语表.md) | 中英对照术语表（持续更新） |
| [_符号约定.md](_符号约定.md) | 全仓数学/密码学符号统一 |
| [_标记规范.md](_标记规范.md) | ⭐🔥📜⚠️🎓🏭 使用边界 |
| [_Review清单.md](_Review清单.md) | 单章与全仓 review checklist |

---

## 0.1 快速导航表

| 章 | 文件 | 主题 | 关键词 | 状态 |
|----|------|------|--------|------|
| §1 | [01-云安全基础与责任模型.md](01-云安全基础与责任模型.md) | 基础与责任模型 | Shared Responsibility / IaaS·PaaS·SaaS / CSPM·CWPP·CNAPP / 成熟度 | 📝 |
| §2 | [02-身份与访问管理.md](02-身份与访问管理.md) | IAM | 最小权限 / RBAC·ABAC / SAML·OIDC / STS / PAM / JIT / CIEM | 📝 |
| §3 | [03-网络安全.md](03-网络安全.md) | 网络隔离与防护 | VPC / 安全组·NACL / WAF / DDoS / PrivateLink / mTLS | 📝 |
| §4 | [04-数据安全.md](04-数据安全.md) | 数据全生命周期 | 静态/传输/使用中加密 / KMS·HSM / 机密计算 / 分级 / Tokenization | 📝 |
| §5 | [05-工作负载安全.md](05-工作负载安全.md) | 容器/K8s/Serverless | 镜像扫描 / RBAC / NetworkPolicy / Gatekeeper / FaaS 注入 | 📝 |
| §6 | [06-零信任架构.md](06-零信任架构.md) | Zero Trust | NIST 800-207 / BeyondCorp / SDP / 微分段 / 持续验证 | 📝 |
| §7 | [07-云安全态势管理.md](07-云安全态势管理.md) | CSPM | CIS Benchmarks / 错误配置 / 漂移 / Security Hub / Defender | 📝 |
| §8 | [08-工作负载保护平台.md](08-工作负载保护平台.md) | CWPP | Agent vs Agentless / 漏洞管理 / 运行时检测 / FIM | 📝 |
| §9 | [09-云原生应用保护平台.md](09-云原生应用保护平台.md) | CNAPP | CSPM+CWPP+CIEM+KSPM / Wiz / Prisma / Lacework | 📝 |
| §10 | [10-DevSecOps.md](10-DevSecOps.md) | 安全左移 | SAST·DAST·SCA / IaC 扫描 / SBOM / SLSA / Pipeline | 📝 |
| §11 | [11-密钥与机密管理.md](11-密钥与机密管理.md) | KMS / Secret | KMS vs Secret Manager / Vault / ACM / CloudHSM | 📝 |
| §12 | [12-审计与可观测.md](12-审计与可观测.md) | 日志与审计 | CloudTrail / SIEM / 不可变日志 / 取证 | 📝 |
| §13 | [13-威胁检测与响应.md](13-威胁检测与响应.md) | TDR | GuardDuty / Chronicle / Cloud Forensics / IR Playbook | 📝 |
| §14 | [14-合规与治理.md](14-合规与治理.md) | 合规框架 | GDPR / HIPAA / PCI-DSS / SOC 2 / ISO 27001 / 等保 2.0 | 📝 |
| §15 | [15-多云与混合云安全.md](15-多云与混合云安全.md) | 多云策略 | 跨云 IAM / 一致性策略 / 数据主权 / 混合连接 | 📝 |
| §16 | [16-供应链安全.md](16-供应链安全.md) | 供应链 | SBOM (CycloneDX/SPDX) / SLSA / Sigstore / Cosign / 依赖治理 | 📝 |
| §17 | [17-Serverless安全.md](17-Serverless安全.md) | FaaS 安全 | 事件注入 / 函数 IAM / 冷启动 / 链路追踪 | 📝 |
| §18 | [18-AI云安全.md](18-AI云安全.md) | AI/ML 云安全 | 模型供应链 / Prompt Injection / OWASP LLM Top10 | 📝 |
| §19 | [19-灾难恢复与业务连续性.md](19-灾难恢复与业务连续性.md) | DR/BCP | RPO/RTO / 跨区容灾 / 不可变备份 / 勒索软件防护 | 📝 |
| §20 | [20-工业案例与事故库.md](20-工业案例与事故库.md) | 事故复盘 | Capital One / SolarWinds / Tesla K8s / Codecov / 各云事故 | 📝 |
| §21 | [21-面试高频问题.md](21-面试高频问题.md) | 面试速查 | 30+ 高频问答骨架 | 📝 |
| §22 | [22-标准与论文清单.md](22-标准与论文清单.md) | 标准与论文 | NIST / CSA / ISO / RFC + 学术论文 | 📝 |
| §23 | [23-附录.md](23-附录.md) | 附录 | 术语表 / 符号 / CIS 速查 / 五家云厂商服务映射 | 📝 |

**状态图例**：🚧 写作中 · ✅ 完成 · 📝 仅骨架 · 🔍 待 review

---

## 0.2 阅读顺序图

```
入门
  │
  ├─→ §1 责任模型与云安全全景
  │
  ├─→ §2 IAM（身份是云时代新边界）
  │
  ├─→ §3 网络安全（隔离与防护）
  │
  ├─→ §4 数据安全（加密 + 分级 + 生命周期）
  │
  └─→ §5 工作负载安全（容器 / K8s / Serverless）
                                              │
                                              ▼
                                  §6 零信任架构（贯穿全部）
                                              │
                          ┌───────────────────┼───────────────────┐
                          ▼                   ▼                   ▼
                    §7 CSPM              §8 CWPP              §9 CNAPP
                  (配置面)              (运行面)              (融合)
                          └───────────────────┬───────────────────┘
                                              ▼
                                     §10 DevSecOps（左移）
                                              │
                          ┌───────────────────┼───────────────────┐
                          ▼                   ▼                   ▼
                    §11 KMS             §12 审计             §13 威胁检测
                  (密钥/机密)          (日志/SIEM)          (检测/响应/取证)
                                              │
                                              ▼
                                     §14 合规与治理
                                              │
                          ┌───────────────────┼───────────────────┐
                          ▼                   ▼                   ▼
                    §15 多云             §16 供应链            §17 Serverless
                                              │
                                              ▼
                                     §18 AI 云安全（新兴）
                                              │
                                              ▼
                                     §19 DR/BCP
                                              │
                                              ▼
                                     §20 工业事故案例库
                                              │
                                              ▼
                            §21 面试  §22 标准  §23 附录
```

---

## 0.3 知识地图：云安全全景

```
                  ┌──────────────────────────────────────────┐
                  │         云计算安全 全景                   │
                  └────────────────────┬─────────────────────┘
                                       │
        ┌──────────────────┬───────────┼───────────┬──────────────────┐
        │                  │           │           │                  │
     治理与策略          身份与访问    数据安全    网络与工作负载     运营与响应
   ┌────┴────┐         ┌───┴───┐   ┌───┴───┐   ┌───┴────┐        ┌───┴────┐
   责任模型   合规      IAM    PAM  加密   KMS  VPC    WAF      CSPM   CWPP
   零信任     风险评估  联邦身份 JIT 分级   HSM  安全组  DDoS     漂移检测 运行时
   策略即代码 审计追踪  CIEM    最小 Token  机密 K8s    Service  SIEM    威胁检测
                       权限         计算   Vault Mesh    │       IR      取证
                       ABAC                              │       Forensics
                                                          │
                                                          ▼
                                                    §9 CNAPP 融合层
                                                          │
                                                          ▼
                                ┌─────────────────────────┴────────┐
                                │                                  │
                            DevSecOps                         供应链安全
                          SAST/DAST/SCA                       SBOM/SLSA
                          IaC 扫描                            Sigstore
                          Pipeline 安全                       依赖治理
```

---

## 1. 每章统一结构

每篇章节遵循如下 12 节结构（与分布式系统项目一致）：

1. **思维导图**：ASCII 树概览全章脉络
2. **问题定义**：解决什么问题、典型场景、云厂商差异
3. **核心概念与术语**：定义清楚后再展开
4. **原理与机制**：第一性原理推导，标明密码学/系统意义
5. **算法/流程**：伪代码 + 复杂度
6. **工业实现对照**：AWS / Azure / GCP / 阿里云 / 华为云 实现
7. **代码/配置示例**：Terraform / IaC / SDK / CLI
8. **常见陷阱与最佳实践**：⚠️ 工程化视角
9. **与其他章节关系**：横向对比表
10. **面试速答**：高频问题的一句话答案
11. **综合面试题**：由浅入深，含答题要点
12. **参考与延伸**：标准、论文、白皮书、跨文件链接

---

## 2. 面试高频考点速查

### 2.1 基础与责任模型
- Shared Responsibility Model 在 IaaS/PaaS/SaaS 下的分界
- 云安全 vs 传统数据中心安全的本质差异
- CSPM / CWPP / CNAPP / CIEM / KSPM 各自定位
- 云安全成熟度模型（CSA CMM）

### 2.2 IAM
- 最小权限原则的工程落地
- RBAC / ABAC / PBAC 的适用场景
- SAML vs OIDC vs OAuth2 的关系与差异
- STS 临时凭证的安全优势
- 权限蔓延（privilege creep）治理
- CIEM 是什么、解决什么问题

### 2.3 网络安全
- 安全组 vs NACL：状态化 vs 无状态
- VPC 设计的最佳实践（public/private/gateway 子网）
- WAF vs IDS/IPS 的边界
- DDoS 防护的三层架构
- PrivateLink / Private Endpoint 解决什么问题
- Service Mesh mTLS 的密钥轮换

### 2.4 数据安全
- 加密三态：at rest / in transit / in use
- KMS 中 CMK 的轮换与撤销
- 机密计算（SGX / SEV-SNP / Nitro Enclave）
- Tokenization vs Encryption 的本质区别
- 数据分级与最小化原则
- 跨境数据流动合规

### 2.5 工作负载安全
- 容器镜像扫描的层次（OS 包 / 应用依赖 / IaC / Secrets）
- Kubernetes RBAC 与 Pod Security Admission
- NetworkPolicy 默认拒绝的工程实现
- OPA Gatekeeper / Kyverno 的策略即代码
- Serverless 事件注入攻击

### 2.6 零信任
- NIST 800-207 的七项原则
- BeyondCorp 的核心组件
- SDP vs VPN
- 微分段 vs 传统网络分段
- 持续验证的实现（设备姿态 + 用户行为 + 上下文）

### 2.7 态势感知与响应
- CSPM 检测错误配置的典型规则
- CIS Benchmarks 的等级与覆盖
- GuardDuty / Defender for Cloud / Chronicle 的差异
- 云取证与传统取证的差异
- 不可变日志的实现（对象锁 / WORM）

### 2.8 DevSecOps 与供应链
- Shift Left 的成本-收益曲线
- SAST / DAST / IAST / SCA 的边界
- IaC 扫描（tfsec / Checkov / Terrascan）
- SBOM 的两种格式：CycloneDX vs SPDX
- SLSA 等级 1-4 的差异
- Sigstore / Cosign 的密钥签名流程

### 2.9 合规与治理
- GDPR / HIPAA / PCI-DSS / SOC 2 / ISO 27001 的覆盖差异
- 等保 2.0 与国际标准的对应
- 合规自动化的工具链
- 数据驻留（data residency）与数据主权

### 2.10 灾难恢复
- RPO / RTO 的定义与权衡
- 跨区域容灾的架构模式
- 不可变备份（immutable backup）防勒索
- 备份的 3-2-1 原则在云上的落地

---

## 3. 工业标准与合规框架索引

| 标准/框架 | 颁布方 | 覆盖范围 | 关键章节 |
|-----------|--------|----------|----------|
| NIST SP 800-53 | NIST | 美联邦信息系统控制 | §1, §14 |
| NIST SP 800-207 | NIST | 零信任架构 | §6 |
| NIST SP 800-190 | NIST | 容器安全指南 | §5 |
| NIST SP 800-204D | NIST | 微服务安全 | §5, §6 |
| NIST CSF | NIST | 网络安全框架 | §13 |
| CSA Cloud Controls Matrix (CCM) | CSA | 云控制矩阵 | §1, §14 |
| CSA Treacherous 12 | CSA | 云顶级威胁 | §20 |
| ISO/IEC 27001 | ISO/IEC | 信息安全管理体系 | §14 |
| ISO/IEC 27017 | ISO/IEC | 云服务安全 | §1, §14 |
| ISO/IEC 27018 | ISO/IEC | 云上 PII 保护 | §4, §14 |
| ISO/IEC 27036 | ISO/IEC | 供应商关系安全 | §16 |
| CIS Benchmarks | CIS | 配置基线（AWS/Azure/GCP/K8s） | §7 |
| PCI-DSS | PCI SSC | 卡支付数据安全 | §4, §14 |
| HIPAA | HHS | 医疗信息隐私 | §4, §14 |
| GDPR | EU | 个人数据保护 | §4, §14 |
| SOC 2 | AICPA | 服务组织控制 | §14 |
| FedRAMP | GSA | 美联邦云授权 | §14 |
| 等保 2.0（GB/T 22239） | 公安部 | 中国网络安全等级保护 | §14 |
| 关基保护条例 | 国务院 | 关键信息基础设施 | §14 |
| OWASP Top 10 | OWASP | Web 应用漏洞 | §3 |
| OWASP LLM Top 10 | OWASP | LLM 应用安全 | §18 |
| MITRE ATT&CK Cloud | MITRE | 云攻击行为矩阵 | §13, §20 |
| SLSA | OpenSSF | 软件供应链等级 | §16 |
| Cybersecurity Act / CRA | EU | 网络韧性法案 | §16 |

---

## 4. 五家云厂商服务映射表（速查，详见 §23 附录 D）

| 能力 | AWS | Azure | GCP | 阿里云 | 华为云 |
|------|-----|-------|-----|--------|--------|
| IAM | IAM + STS | Entra ID + RBAC | IAM + Workload Identity | RAM + STS | IAM + Agency |
| 网络 | VPC + SG + NACL | VNet + NSG + UDR | VPC + Firewall | VPC + SG + ACL | VPC + SG |
| KMS | KMS + CloudHSM | Key Vault / MHSM | Cloud KMS + HSM | KMS + HSM | KMS + DEW |
| WAF | WAF + Shield | WAF + Front Door | Cloud Armor | WAF + Anti-DDoS | WAF + AAD |
| CSPM | Security Hub | Defender for Cloud | Security Command Center | Config + Config Rules | Config + TMS |
| 威胁检测 | GuardDuty | Defender XDR | SCC Premium / Event Threat Detection | Threat Detection | HSS / SecMaster |
| 日志审计 | CloudTrail + CloudWatch | Activity Log + Monitor | Audit Logs + Logging | ActionTrail + SLS | CTS + LTS |
| 容器仓库 | ECR | ACR | Artifact Registry | ACR | SWR |
| 容器服务 | EKS / ECS / Fargate | AKS | GKE | ACK / ASK | CCE / CCI |
| 密钥机密 | Secrets Manager + Parameter Store | Key Vault Secrets | Secret Manager | KMS Secrets | CSMS / DEW |
| 备份 | AWS Backup | Azure Backup | Backup and DR | HBR | CBR |
| 合规报告 | Artifact | Service Trust Portal | Compliance Reports | 合规中心 | 合规中心 |

---

## 5. 参考资源

### 5.1 教材与权威指南
- 《Cloud Security: A Comprehensive Guide to Secure Cloud Computing》—— Rittinghouse, Ransome
- 《Architecting the Cloud》—— Wilder
- 《AWS Security Best Practices》—— AWS Whitepaper
- 《Azure Security Best Practices》—— Microsoft Docs
- 《Cloud Native Security》—— Pushkar Joglekar
- 《Hacking the Cloud》—— cloudsecwiki
- 《Zero Trust Networks》—— Gilman, Barth
- 《Container Security》—— Liz Rice
- 《Software Supply Chain Security》—— Cassie Crossley

### 5.2 标准与白皮书（详见 §22）
- NIST SP 800-53 / 207 / 190 / 204 / 219
- CSA CCM v4 / Treacherous 12 / Cloud Threats
- ISO/IEC 27001 / 27017 / 27018 / 27036
- CIS Benchmarks (AWS/Azure/GCP/K8s/OS)
- OWASP Top 10 / LLM Top 10
- MITRE ATT&CK for Cloud
- SLSA / Sigstore 文档

### 5.3 在线资源
- Cloud Security Alliance: https://cloudsecurityalliance.org
- NIST Cybersecurity: https://csrc.nist.gov
- MITRE ATT&CK Cloud Matrix: https://attack.mitre.org/matrices/enterprise/cloud
- CloudSecList: https://cloudseclist.com
- Hacking the Cloud: https://hackingthe.cloud
- Cloud Threats Year in Review (Sophos / Lacework / Wiz)

### 5.4 工具与开源生态
- IaC 扫描：`tfsec` / `Checkov` / `Terrascan` / `KICS`
- 容器扫描：`Trivy` / `Grype` / `Clair` / `Snyk`
- K8s 策略：`OPA Gatekeeper` / `Kyverno` / `Kubescape`
- SBOM：`Syft` / `CycloneDX` / `SPDX`
- 签名：`Cosign` / `Sigstore` / `Notation`
- 多云 CSPM：`Prowler` (AWS) / `ScoutSuite` / `CloudSploit`
- 密钥扫描：`Gitleaks` / `TruffleHog`
- 取证：`AWS Detective` / `Azure Sentinel` / `GRR`

---

## 6. 笔记约定

- **语言与框架**：IaC 示例以 Terraform 为主，辅以 AWS CLI / Azure CLI / gcloud / 阿里云 OpenAPI
- **云厂商中立**：正文概念中立，每章「工业实现对照」小节列出五家差异
- **数学/密码学符号**：统一见 [_符号约定.md](_符号约定.md)
- **图示**：优先 ASCII 图说明架构；复杂图标注来源
- **公式**：关键密码学/概率推导步骤必须标明语义
- **配置示例**：从零最小可运行版（教学）+ 生产级版（工程，含模块化/标签/审计）
- **跨文件链接**：相关概念使用相对路径链接，便于跳转
- **合规标注**：涉及合规要求时显式引用标准条款（如 "GDPR Art. 32"）

---

## 7. TODO / 待完善

- [ ] 按章节逐篇完善 §1 → §19 内容
- [ ] 每章补充真实事故案例（覆盖 §20）
- [ ] 补充五家云厂商 CLI/IaC 双版本示例
- [ ] 增加云安全架构图集（参考架构 / 多租户 / 多账号 / Landing Zone）
- [ ] 增加合规审计证据矩阵（控制项 → 服务 → 证据）
- [ ] 增加云安全面试真题汇编（按公司分类）
- [ ] 增加 IR Playbook 模板库
- [ ] 跟踪 OWASP LLM Top 10 与 EU CRA 时效性内容

---

## 8. 与仓库其他子项目的关系

- [../分布式系统/](../分布式系统/README.md)：复制/共识/事务的分布式安全基础
- [../数据库/](../数据库/README.md)：数据库加密、行列权限、TDE
- [../机器学习/](../机器学习/README.md)：模型供应链、对抗样本、数据中毒
- [../深度学习/](../深度学习/README.md)：LLM 安全、Prompt Injection
- [../Agent开发/](../Agent开发/README.md)：Agent 工具调用安全、权限边界
- [../infra开发/](../infra开发/README.md)：IaC / Terraform / K8s 与 DevSecOps 实践
- [../软件工程系统分析与设计/](../软件工程系统分析与设计/README.md)：威胁建模 (STRIDE)、安全设计原则
