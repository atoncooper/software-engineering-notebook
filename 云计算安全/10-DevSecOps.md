# §10 - DevSecOps

> 一句话定位：DevSecOps = 把安全嵌入 SDLC 全流程，Shift Left 让安全成为所有人的责任。
>
> 标记：⭐高频 🔥工程重点 📜论文/标准 ⚠️易错点 🎓学术深度 🏭工业实战

---

## 〇、思维导图

```
                       DevSecOps
                          │
        ┌──────┬──────────┼──────────┬──────────┐
        │      │          │          │          │
     Shift Left  测试类型  IaC 安全   供应链     Pipeline
        │      │          │          │          │
   代码扫描   SAST        Terraform   SBOM       CI/CD 集成
   依赖审查   DAST        tfsec       SLSA       GitOps
   提交前     IAST        Checkov     Sigstore   ArgoCD
   PR 检查    SCA         KICS        Cosign     安全策略
```

---

## 一、问题定义

### 1.1 解决什么问题

传统安全在 SDLC 末端介入，导致：
- 修复成本高 (生产修复 = 开发修复 × 100)
- 安全成瓶颈 (上线前才扫描)
- 文化对立 (dev vs sec)

🔥 **Shift Left 经济学** (NIST 研究)：
- 需求阶段发现：修复成本 1×
- 设计阶段：5×
- 编码阶段：10×
- 测试阶段：50×
- 生产阶段：200×

DevSecOps 解决：
1. **早发现** — CI/CD 每个阶段都扫描
2. **自动化** — 工具替代人工 review
3. **文化** — Dev/SRE/Sec 共担责任

### 1.2 与传统安全的差异

| 维度 | 传统安全 | DevSecOps |
|------|----------|-----------|
| 介入时机 | 上线前 | SDLC 每阶段 |
| 责任 | 安全团队 | 全员 |
| 工具 | 手动 + 末端 | 自动化 + 全程 |
| 速度 | 慢 (周/月) | 快 (分钟) |
| 心态 | 阻断 | 赋能 |
| 度量 | 漏洞数 | MTTR + Shift Left 比例 |

### 1.3 云厂商差异概述

| 概念 | AWS | Azure | GCP | 阿里云 | 华为云 |
|------|-----|-------|-----|--------|--------|
| 代码扫描 | CodeGuru Security | Defender for DevOps | Cloud Code Security | 代码检测 | 代码检测 |
| IaC 扫描 | Inspector + cfnnag | Azure Policy | Forseti | 配置审计 | Config |
| 依赖扫描 | Inspector | Defender | Artifact Analysis | 漏洞扫描 | VSS |
| 流水线 | CodePipeline | Azure Pipelines | Cloud Build | 云效 | CodeArts |
| GitOps | Flux/ArgoCD | Flux | Config Sync | ArgoCD | ArgoCD |

---

## 二、核心概念与术语

| 术语 | 全称 | 含义 |
|------|------|------|
| SDLC | Software Development Life Cycle | 软件开发生命周期 |
| Shift Left | 安全左移 | 早介入 |
| SAST | Static Application Security Testing | 静态代码扫描 |
| DAST | Dynamic Application Security Testing | 动态测试 |
| IAST | Interactive Application Security Testing | 交互式测试 |
| SCA | Software Composition Analysis | 软件组成分析 |
| IaC | Infrastructure as Code | 基础设施即代码 |
| SBOM | Software Bill of Materials | 软件物料清单 |
| SLSA | Supply Chain Levels for Software Artifacts | 供应链等级 |
| Pipeline | 流水线 | CI/CD 流程 |
| GitOps | Git 驱动部署 | 声明式 |
| DORA | DevOps Research and Assessment | 度量指标 |
| MFA | Mean Time to Acknowledge | 度量 |
| MTTR | Mean Time to Remediate | 平均修复时间 |

---

## 三、原理与机制

### 3.1 SDLC 各阶段安全活动

```
计划 (Plan)
  └─ 威胁建模 (STRIDE)

开发 (Code)
  ├─ IDE 插件 (实时检测)
  ├─ pre-commit hook (secrets 扫描)
  └─ SAST (PR 检查)

构建 (Build)
  ├─ SCA (依赖漏洞)
  ├─ 镜像扫描
  └─ SBOM 生成

测试 (Test)
  ├─ DAST (运行时扫描)
  ├─ IAST (插桩测试)
  └─ Fuzzing

部署 (Deploy)
  ├─ IaC 扫描
  ├─ 策略验证 (OPA)
  └─ 准入控制 (Kyverno)

运行 (Operate)
  ├─ CSPM/CWPP
  ├─ 运行时保护
  └─ 监控告警

监控 (Monitor)
  ├─ 日志分析
  ├─ 威胁检测
  └─ 反馈到计划
```

### 3.2 SAST/DAST/IAST/SCA 对比

| 类型 | 时机 | 方式 | 优势 | 劣势 |
|------|------|------|------|------|
| SAST | 编译前 | 静态代码分析 | 早发现 + 高覆盖 | 误报多 |
| DAST | 运行时 | 黑盒扫描 | 真实漏洞 | 晚发现 + 覆盖低 |
| IAST | 运行时 | 插桩 + 流量 | 准确率高 | 需 agent |
| SCA | 任何时候 | 依赖分析 | CVE 关联 | 仅依赖漏洞 |

### 3.3 SLSA 等级

📜 **SLSA (Supply Chain Levels for Software Artifacts)** v1.0：

| 级别 | 要求 | 防御 |
|------|------|------|
| L1 | 构建过程文档化 | 防止无文档构建 |
| L2 | 托管构建服务 | 防止构建机被入侵 |
| L3 | 构建隔离可验证 | 防止源代码篡改 |
| L4 | 双人审核 + 可重现构建 | 防止内部作恶 |

### 3.4 Sigstore / Cosign 签名链

```
   开发者 / CI
       │
       │ 1. OIDC token
       ▼
   ┌─────────┐
   │ Fulcio  │  ← 短期证书 CA
   └────┬────┘
        │ 2. 证书 + 签名
        ▼
   ┌─────────┐
   │ Rekor   │  ← 透明日志 (Merkle 树)
   └────┬────┘
        │ 3. 记录
        ▼
   ┌─────────┐
   │ Registry│  ← 镜像 + 签名附件
   └─────────┘
        │
        ▼ 4. 部署时验证
   Kyverno / Connaisseur
```

---

## 四、算法 / 流程

### 4.1 SAST 扫描流程

```
输入: 源代码
输出: 漏洞列表

1. 解析 AST (抽象语法树)
2. 数据流分析 (taint analysis)
   └─ 用户输入 → 危险函数 (sink)?
3. 控制流分析
4. 模式匹配 (已知漏洞模式)
5. 上下文过滤 (false positive 减少)
6. 输出: 行号 + 漏洞类型 + 严重度 + 修复建议
```

### 4.2 SCA 扫描流程

```
输入: package.json / pom.xml / requirements.txt
输出: 漏洞列表

1. 解析依赖文件
2. 递归解析传递依赖
3. 对每个包查询 CVE 数据库:
   ├─ NVD (National Vulnerability Database)
   ├─ OSV (Open Source Vulnerabilities)
   ├─ GHSA (GitHub Security Advisory)
   └─ 厂商公告
4. 输出: 包 + 版本 + CVE + 严重度 + 修复版本
```

### 4.3 IaC 扫描流程

```
输入: Terraform / CloudFormation / K8s YAML
输出: 配置错误列表

1. 解析 IaC 文件
2. 与规则集匹配:
   ├─ CIS Benchmark
   ├─ 厂商最佳实践
   └─ 企业自定义
3. 输出: 资源 + 规则 + 严重度 + 修复建议
```

---

## 五、工业实现对照

### 5.1 DevSecOps 工具栈

| 阶段 | 工具 |
|------|------|
| IDE | Semgrep / Snyk / CodeQL |
| Pre-commit | git-secrets / TruffleHog / Gitleaks |
| SAST | SonarQube / Checkmarx / CodeQL / Semgrep |
| SCA | Snyk / Dependabot / Renovate / OWASP DC |
| 镜像扫描 | Trivy / Grype / Snyk Container |
| IaC 扫描 | tfsec / Checkov / KICS / Terrascan |
| DAST | OWASP ZAP / Burp / Nuclei |
| 签名 | Cosign / Notation |
| SBOM | Syft / CycloneDX |
| 准入 | Kyverno / OPA Gatekeeper |
| GitOps | ArgoCD / Flux |

### 5.2 流水线平台对比

| 平台 | 厂商 | 安全集成 |
|------|------|----------|
| GitHub Actions | GitHub | CodeQL + Dependabot 原生 |
| GitLab CI | GitLab | SAST/SCA/Secret Detection 内置 |
| Jenkins | 开源 | 插件丰富 |
| CircleCI | SaaS | 第三方 orb |
| Argo Workflows | K8s | Container 内任意工具 |
| CodePipeline (AWS) | AWS | 与 Inspector/Security Hub 集成 |
| Azure Pipelines | Microsoft | 与 Defender 集成 |

---

## 六、代码 / 配置示例

### 6.1 最小可运行版（教学）：GitHub Actions DevSecOps

```yaml
# .github/workflows/devsecops.yml
name: DevSecOps Pipeline

on: [push, pull_request]

jobs:
  # ============================================
  # 1. Secret 扫描 (Pre-commit 阶段)
  # ============================================
  secret-scan:
    runs-on: ubuntu-latest
    steps:
    - uses: actions/checkout@v4
      with:
        fetch-depth: 0   # 完整历史
    - name: Gitleaks
      uses: gitleaks/gitleaks-action@v2
      env:
        GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
  
  # ============================================
  # 2. SAST (静态代码扫描)
  # ============================================
  sast:
    runs-on: ubuntu-latest
    steps:
    - uses: actions/checkout@v4
    
    # CodeQL (GitHub 原生)
    - uses: github/codeql-action/init@v3
      with:
        languages: python, javascript
    
    - uses: github/codeql-action/analyze@v3
    
    # Semgrep (额外规则)
    - uses: returntocorp/semgrep-action@v1
      with:
        config: >-
          p/owasp-top-ten
          p/python
          p/javascript
  
  # ============================================
  # 3. SCA (依赖扫描)
  # ============================================
  sca:
    runs-on: ubuntu-latest
    steps:
    - uses: actions/checkout@v4
    
    # Dependabot (自动 PR 升级)
    # 已在 .github/dependabot.yml 配置
    
    # npm audit / pip-audit / mvn dependency-check
    - name: Run Snyk
      uses: snyk/actions/python@master
      env:
        SNYK_TOKEN: ${{ secrets.SNYK_TOKEN }}
      with:
        args: --severity-threshold=high --fail-on=upgradable
  
  # ============================================
  # 4. 镜像构建 + 扫描 + 签名
  # ============================================
  build:
    needs: [secret-scan, sast, sca]
    runs-on: ubuntu-latest
    permissions:
      contents: read
      packages: write
      id-token: write   # OIDC for keyless signing
    
    steps:
    - uses: actions/checkout@v4
    
    - uses: docker/setup-buildx-action@v3
    
    # 构建镜像
    - uses: docker/build-push-action@v5
      id: build
      with:
        context: .
        tags: ghcr.io/myorg/myapp:${{ github.sha }}
        load: true
        provenance: true
        sbom: true
    
    # Trivy 扫描
    - name: Trivy Scan
      uses: aquasecurity/trivy-action@master
      with:
        image-ref: ghcr.io/myorg/myapp:${{ github.sha }}
        format: json
        exit-code: 1
        severity: CRITICAL,HIGH
        ignore-unfixed: true
    
    # 生成 SBOM
    - name: Generate SBOM
      run: |
        curl -sSfL https://raw.githubusercontent.com/anchore/syft/main/install.sh | sh
        syft ghcr.io/myorg/myapp:${{ github.sha }} -o spdx-json > sbom.spdx.json
    
    # Cosign 签名 (Keyless)
    - uses: sigstore/cosign-installer@v3
    - name: Sign Image
      env:
        COSIGN_EXPERIMENTAL: 1
      run: |
        cosign sign --yes ghcr.io/myorg/myapp@${{ steps.build.outputs.digest }}
    
    # 附加 SBOM 证明
    - name: Attest SBOM
      env:
        COSIGN_EXPERIMENTAL: 1
      run: |
        cosign attest --yes \
          --predicate sbom.spdx.json \
          --type spdxjson \
          ghcr.io/myorg/myapp@${{ steps.build.outputs.digest }}
  
  # ============================================
  # 5. IaC 扫描
  # ============================================
  iac-scan:
    runs-on: ubuntu-latest
    steps:
    - uses: actions/checkout@v4
    
    # tfsec
    - name: tfsec
      uses: aquasecurity/tfsec-action@v1.0.3
    
    # Checkov
    - uses: bridgecrewio/checkov-action@v12
      with:
        directory: .
        framework: terraform
        output_format: cli,sarif
        output_file_path: console,results.sarif
    
    # KICS
    - name: KICS
      run: |
        docker run -v $(pwd):/path checkmarx/kics:latest scan \
          -p /path -o /path/results --fail-on high
  
  # ============================================
  # 6. DAST (部署到 staging 后)
  # ============================================
  dast:
    needs: build
    runs-on: ubuntu-latest
    steps:
    - name: OWASP ZAP Scan
      uses: zaproxy/action-baseline@v0.10.0
      with:
        target: 'https://staging.myapp.com'
        cmd_options: '-a -j'
```

### 6.2 生产级版（工程）：GitOps + 策略即代码

```yaml
# ============================================
# ArgoCD Application + 策略验证
# ============================================
apiVersion: argoproj.io/v1alpha1
kind: Application
metadata:
  name: myapp-prod
  namespace: argocd
  finalizers:
  - resources-finalizer.argocd.argoproj.io
spec:
  project: production
  source:
    repoURL: https://github.com/myorg/myapp-deploy
    path: production
    targetRevision: HEAD
  destination:
    server: https://kubernetes.default.svc
    namespace: production
  syncPolicy:
    automated:
      prune: true
      selfHeal: true
    syncOptions:
    - CreateNamespace=false
    - ApplyOutOfSyncOnly=true
  # 部署前 hook: 验证签名 + 策略
  hooks:
  - name: verify-policy
    hook:
      exec:
        command: ["sh", "-c"]
        args:
        - |
          # 用 OPA 验证 K8s 清单
          curl -s https://github.com/open-policy-agent/conftest/releases/latest/download/conftest_linux_amd64.tar.gz | tar xz
          ./conftest test production/ --policy policies/
---
# policies/security.rego (OPA 策略)
package main

# 拒绝特权容器
deny[msg] {
  container := input.spec.template.spec.containers[_]
  container.securityContext.privileged == true
  msg := sprintf("容器 %v 是特权", [container.name])
}

# 拒绝 latest tag
deny[msg] {
  container := input.spec.template.spec.containers[_]
  endswith(container.image, ":latest")
  msg := sprintf("容器 %v 使用 latest tag", [container.name])
}

# 必须设资源限制
deny[msg] {
  container := input.spec.template.spec.containers[_]
  not container.resources.limits.cpu
  msg := sprintf("容器 %v 缺少 CPU limit", [container.name])
}

# 必须以非 root 运行
deny[msg] {
  container := input.spec.template.spec.containers[_]
  not container.securityContext.runAsNonRoot
  msg := sprintf("容器 %v 未设 runAsNonRoot", [container.name])
}
```

### 6.3 依赖管理 (Dependabot + Renovate)

```yaml
# .github/dependabot.yml
version: 2
updates:
  # Python 依赖
  - package-ecosystem: "pip"
    directory: "/"
    schedule:
      interval: "weekly"
      day: "monday"
    open-pull-requests-limit: 10
    reviewers: ["myorg/security-team"]
    labels: ["dependencies", "security"]
    # 仅高危
    allow:
    - dependency-type: "all"
  
  # Docker 基础镜像
  - package-ecosystem: "docker"
    directory: "/"
    schedule:
      interval: "weekly"
  
  # GitHub Actions
  - package-ecosystem: "github-actions"
    directory: "/"
    schedule:
      interval: "weekly"
  
  # Terraform
  - package-ecosystem: "terraform"
    directory: "/infrastructure"
    schedule:
      interval: "weekly"
```

```json
// renovate.json (Renovate 替代方案)
{
  "extends": ["config:base", ":automergeMinor"],
  "schedule": ["before 6am on Monday"],
  "vulnerabilityAlerts": {
    "enabled": true,
    "labels": ["security"],
    "schedule": "at any time"
  },
  "packageRules": [
    {
      "matchUpdateTypes": ["major"],
      "dependencyDashboardApproval": true
    },
    {
      "matchCategories": ["python"],
      "addLabels": ["python"]
    }
  ]
}
```

---

## 七、常见陷阱与最佳实践 ⚠️

| 陷阱 | 后果 | 对策 |
|------|------|------|
| Shift Left 但阻断 CI | 工程师绕过 | 严重度分级,不阻断 low |
| SAST 误报多 | 工程师麻木 | 持续调规则 + suppress 误报 |
| SCA 不更新数据库 | 漏报 | 多源 (NVD + OSV + GHSA) |
| 不锁依赖版本 | 供应链攻击 | lock file + integrity hash |
| 镜像扫描仅 OS | 漏应用依赖 | 加语言层 + secrets 扫描 |
| IaC 扫描不进 CI | 部署后才发现 | CI 必跑 tfsec/Checkov |
| 签名不验证 | 签了等于没签 | Kyverno 准入强制 |
| SBOM 不存档 | 漏洞响应慢 | CI 生成 + 长期存档 |
| DAST 仅 staging | 测试不充分 | 加 prod 但 read-only |
| Pipeline 凭证泄露 | 上游供应链 | OIDC + 短期 token |

### 7.1 工程重点 (🔥)

1. **Pipeline 安全第一** — CI 凭证 = 上游供应链
2. **OIDC 替代长期凭证** — Keyless 签名
3. **Shift Left 但不阻断** — 严重度分级
4. **多工具交叉** — SAST + SCA + IaC
5. **签名 + 准入验证** — 完整信任链
6. **SBOM 持久化** — 漏洞响应
7. **策略即代码** — OPA/Kyverno
8. **GitOps** — Git 为 source of truth

---

## 八、与其他章节关系

| 章节 | 关系 |
|------|------|
| §1 责任模型 | DevSecOps 是租户责任的核心 |
| §2 IAM | Pipeline 凭证管理 |
| §5 工作负载 | 镜像供应链 |
| §7 CSPM | IaC 扫描是 Shift Left |
| §9 CNAPP | CNAPP 是 DevSecOps 工具 |
| §11 KMS | CI 凭证加密 |
| §16 供应链 | SLSA + SBOM |
| §17 Serverless | FaaS CI/CD |

---

## 九、面试速答 ⭐

| 问 | 答 |
|----|-----|
| Shift Left 含义 | 安全左移到 SDLC 早期阶段 |
| SAST vs DAST | SAST 静态代码 (早);DAST 运行时 (晚但真) |
| SCA 是什么 | 软件组成分析,扫描依赖漏洞 |
| SBOM 是什么 | 软件物料清单,所有依赖列表 |
| SLSA 等级 | L1 文档/L2 托管构建/L3 隔离可验/L4 双审+可重现 |
| Cosign Keyless | 用 OIDC 短期证书,无需管理私钥 |
| IaC 扫描工具 | tfsec / Checkov / KICS |
| GitOps 安全 | Git 为 source of truth + 签名 + 准入验证 |
| Dependabot | GitHub 自动 PR 升级依赖 |
| OPA/Rego | 策略即代码,跨平台 |

---

## 十、综合面试题

1. **设计一个生产级 DevSecOps 流水线。**
   - 要点:pre-commit secrets;SAST (CodeQL+Semgrep);SCA (Snyk);IaC (Checkov);镜像扫描 (Trivy);SBOM 生成;Cosign 签名;Kyverno 准入;GitOps (ArgoCD);DAST staging。

2. **如何衡量 DevSecOps 成熟度?**
   - 要点:DORA 指标 (部署频率 + MTTR + 失败率);Shift Left 比例 (问题在 CI 发现 vs 生产);漏洞密度;修复时间;自动化覆盖率。

3. **Shift Left 的成本收益?**
   - 要点:需求阶段修复 1×;生产 200×;早发现节省成本;但过度 Shift Left 增加 CI 时间;平衡:CI 仅 high+ critical 阻断。

4. **SolarWinds 事件给 DevSecOps 的启示?**
   - 要点:构建过程被入侵;签名 key 被盗;SLSA L3+ 可防御 (隔离构建 + 可验证出处);Sigstore Keyless 替代长期 key。

5. **如何处理 SAST 大量误报?**
   - 要点:规则调优;suppress 误报 + 周期复审;多个 SAST 交叉验证;IAST 提升准确率;业务上下文过滤。

6. **SBOM 在漏洞响应中的价值?**
   - 要点:新 CVE 披露后,秒级查询受影响产品;无需重新扫描;支持溯源;合规要求 (EU CRA)。

7. **Cosign Keyless 签名原理?**
   - 要点:CI 拿 OIDC token;Fulcio 颁发短期证书;签名 + 写 Rekor 透明日志;验证者查 Rekor + 验 OIDC issuer。

8. **IaC 扫描如何集成 CI?**
   - 要点:PR 阶段 tfsec/Checkov;严重问题阻断合并;false positive 用注释跳过;baseline 文件忽略已知问题。

9. **GitOps 与传统 CI/CD 的安全差异?**
   - 要点:Git 为 source of truth;PR review 替代手动 kubectl;签名 + 准入验证;自动 drift 检测;回滚快。

10. **如何防止 Pipeline 自身被入侵?**
    - 要点:OIDC 替代长期凭证;最小权限 (token 作用域);隔离 runner;签名验证依赖;SBOM 比对;secret 扫描。

---

## 十一、参考与延伸

### 11.1 标准 / 白皮书
- 📜 **NIST SP 800-218** — SSDF (Secure Software Development Framework)
- 📜 **SLSA v1.0** — https://slsa.dev
- 📜 **OWASP SAMM** — Software Assurance Maturity Model
- 📜 **OWASP Top 10 CI/CD** — CI/CD 安全风险
- 📜 **EU CRA** — Cyber Resilience Act
- 📜 **DORA** — DevOps Research and Assessment

### 11.2 论文
- 🎓 "Shift Left: A Survey" (Linux Foundation, 2023)
- 🎓 "Software Supply Chain Security" (Ladisa et al.)
- 🎓 "Threat Modeling for DevOps" (Tuma et al.)

### 11.3 工具
- SonarQube / Checkmarx / CodeQL / Semgrep (SAST)
- Snyk / Dependabot / Renovate (SCA)
- Trivy / Grype (镜像)
- tfsec / Checkov / KICS (IaC)
- Cosign / Notation (签名)
- Syft (SBOM)
- OPA / Kyverno (策略)
- ArgoCD / Flux (GitOps)

### 11.4 跨文件链接
- [README](./README.md)
- [§5 工作负载安全](./05-工作负载安全.md)
- [§7 CSPM](./07-云安全态势管理.md)
- [§16 供应链安全](./16-供应链安全.md)
- [§17 Serverless](./17-Serverless安全.md)
