# 14 - GitOps 与现代发布

> GitOps 把 Git 作为系统的唯一可信源（Single Source of Truth），声明式描述系统状态、版本化所有变更、自动同步与漂移检测。本章梳理 GitOps 原则、ArgoCD vs Flux、多环境管理、Kustomize/Helm、Secret 治理，以及大厂 LLM 推理平台的发布流水线。

---

## 一、思维导图

```
                      GitOps
                        │
        ┌───────────────┼───────────────┐
        ▼               ▼               ▼
   ┌─────────┐     ┌─────────┐     ┌─────────┐
   │ 原则    │     │ 工具    │     │ 模式    │
   │ 声明式  │     │ ArgoCD  │     │ 多环境  │
   │ 版本化  │     │ Flux    │     │ Secret  │
   │ 自动同步│     │ Argo    │     │ Helm/   │
   │ 漂移检测│     │ Rollouts│     │ Kustomize│
   └─────────┘     └─────────┘     └─────────┘
```

---

## 二、问题定义与边界

### 2.1 解决什么

- **声明式发布**：Git 提交即部署，可审计、可回滚
- **多环境一致性**：dev/staging/prod 同一份 manifest
- **漂移检测**：手动改了集群立刻发现并纠正
- **Secret 治理**：加密存储、轮换、审计
- **发布编排**：与渐进式发布（15 章）结合

### 2.2 不解决什么

- 不深入渐进式发布策略（15 章）
- 不覆盖 CI 流水线（Docker 16 章）
- 不覆盖服务网格（20 章）

---

## 三、直觉解释

### 3.1 为什么需要 GitOps

```
传统发布 (kubectl apply):
  - 谁改的? 不知道
  - 改了什么? 不知道
  - 当前是什么版本? 不知道
  - 怎么回滚? 翻历史
  - 漂移? 没人知道

GitOps:
  - 谁改的: git blame
  - 改了什么: git diff
  - 当前版本: git rev-parse HEAD
  - 怎么回滚: git revert
  - 漂移: 控制面 diff 报警
```

### 3.2 GitOps 四原则（OpenGitOps 1.0）

```
1. 声明式 (Declarative)
   系统状态用声明式描述, 不是过程式脚本

2. 版本化 (Versioned and Immutable)
   所有状态存在 Git, 不可变, 有完整历史

3. 自动拉取 (Pulled Automatically)
   系统自动从 Git 拉取, 不是 CI 推送 (Push vs Pull)

4. 持续协调 (Continuously Reconciled)
   控制面持续比对实际状态与期望状态, 漂移即纠正
```

### 3.3 Push vs Pull 模型

| 模型 | CI Push | GitOps Pull |
|------|---------|-------------|
| 触发 | CI 完成后 kubectl apply | 集群内 Agent 拉取 |
| 凭证 | CI 持有集群 kubeconfig | 集群内 Agent 持有 Git 凭证 |
| 安全 | CI 凭证泄露风险高 | 集群内最小权限 |
| 可观测 | CI 日志 | Agent 持续 reconcile |
| 多集群 | 需要每集群一套 kubeconfig | Agent 自治 |
| 工具 | Jenkins/GitHub Actions | ArgoCD/Flux |

---

## 四、核心概念与架构

### 4.1 ArgoCD 架构

```
┌─────────────────────────────────────────────┐
│                Git Repository               │
│  (manifests/helm/kustomize)                 │
└──────────────────┬──────────────────────────┘
                   │ pull
                   ▼
┌─────────────────────────────────────────────┐
│            ArgoCD Controller                │
│  ┌────────────┐  ┌────────────┐             │
│  │ API Server │  │ Repo Server│             │
│  │  (gRPC/REST)│ │ (manifest) │             │
│  └────────────┘  └────────────┘             │
│  ┌────────────────────────────┐             │
│  │ Application Controller     │             │
│  │ - 比对 Git vs Cluster      │             │
│  │ - 触发 Sync                │             │
│  └────────────────────────────┘             │
└──────────────────┬──────────────────────────┘
                   │ kubectl apply
                   ▼
┌─────────────────────────────────────────────┐
│                Kubernetes                   │
│  ┌─────────────────────────────────────┐    │
│  │ argocd-application-controller       │    │
│  │ (集群内 Agent, 持续 reconcile)      │    │
│  └─────────────────────────────────────┘    │
└─────────────────────────────────────────────┘
```

### 4.2 ArgoCD Application CRD

```yaml
apiVersion: argoproj.io/v1alpha1
kind: Application
metadata:
  name: vllm-prod
  namespace: argocd
spec:
  project: default
  source:
    repoURL: https://github.com/org/llm-manifests
    targetRevision: main
    path: prod/vllm
    helm:
      valueFiles:
        - values-prod.yaml
  destination:
    server: https://kubernetes.default.svc
    namespace: vllm-prod
  syncPolicy:
    automated:
      prune: true            # 删除 Git 中已移除的资源
      selfHeal: true         # 漂移自动纠正
      allowEmpty: false
    syncOptions:
      - CreateNamespace=true
      - PrunePropagationPolicy=foreground
      - PruneLast=true
    retry:
      limit: 5
      backoff:
        duration: 5s
        factor: 2
        maxDuration: 3m
```

### 4.3 Flux 架构

```
┌─────────────────────────────────────────────┐
│                Git Repository               │
└──────────────────┬──────────────────────────┘
                   │
       ┌───────────┴───────────┐
       ▼                       ▼
┌──────────────┐         ┌──────────────┐
│ source       │         │ kustomize    │
│ controller   │         │ controller   │
│ (拉 Git/Helm)│         │ (渲染+apply) │
└──────────────┘         └──────────────┘
       │                       │
       └───────────┬───────────┘
                   ▼
┌─────────────────────────────────────────────┐
│             notification controller         │
│  (Slack/Teams/告警)                         │
└─────────────────────────────────────────────┘
```

### 4.4 Flux 核心资源

```yaml
# 1. GitRepository Source
apiVersion: source.toolkit.fluxcd.io/v1
kind: GitRepository
metadata:
  name: llm-manifests
  namespace: flux-system
spec:
  interval: 1m
  url: https://github.com/org/llm-manifests
  ref:
    branch: main
  secretRef:
    name: github-deploy-key
---
# 2. Kustomization
apiVersion: kustomize.toolkit.fluxcd.io/v1
kind: Kustomization
metadata:
  name: vllm-prod
  namespace: flux-system
spec:
  interval: 5m
  path: ./prod/vllm
  sourceRef:
    kind: GitRepository
    name: llm-manifests
  prune: true
  healthChecks:
    - apiVersion: apps/v1
      kind: Deployment
      name: vllm
      namespace: vllm-prod
  postBuild:
    substitute:
      cluster_name: prod-cn-east-1
    substituteFrom:
      - kind: ConfigMap
        name: cluster-vars
```

### 4.5 ArgoCD vs Flux 对比

| 维度 | ArgoCD | Flux |
|------|--------|------|
| 模型 | Application CRD | source + kustomize/helm controller |
| UI | 完整 GUI | 轻量, 主要 CLI |
| 多集群 | 中心化 (一套 ArgoCD 管多集群) | 联邦式 (每集群一套 Flux) |
| RBAC | 内置完整 | 依赖 K8s RBAC |
| Sync 策略 | 自动/手动, selfHeal | 自动, 健康检查 |
| Helm | 完整支持 | 完整支持 |
| Kustomize | 完整支持 | 原生 |
| 生态 | Argo Rollouts/Workflows/Events | Flux + Flagger |
| 学习曲线 | 中等 | 较低 |
| 适用 | 中大型企业, 需要 UI 与 RBAC | 中小团队, GitOps 原教旨 |

---

## 五、操作流程与配置

### 5.1 多环境管理（Kustomize）

```
llm-manifests/
├── base/
│   ├── vllm-deployment.yaml
│   ├── vllm-service.yaml
│   ├── vllm-hpa.yaml
│   └── kustomization.yaml
├── overlays/
│   ├── dev/
│   │   ├── kustomization.yaml
│   │   ├── replica-patch.yaml
│   │   └── resource-patch.yaml
│   ├── staging/
│   │   ├── kustomization.yaml
│   │   └── replica-patch.yaml
│   └── prod/
│       ├── kustomization.yaml
│       ├── replica-patch.yaml
│       ├── hpa-patch.yaml
│       └── pdb-patch.yaml
```

```yaml
# base/kustomization.yaml
apiVersion: kustomize.config.k8s.io/v1beta1
kind: Kustomization
resources:
  - vllm-deployment.yaml
  - vllm-service.yaml
  - vllm-hpa.yaml

commonLabels:
  app.kubernetes.io/name: vllm
  app.kubernetes.io/part-of: llm-platform
---
# overlays/prod/kustomization.yaml
apiVersion: kustomize.config.k8s.io/v1beta1
kind: Kustomization
namespace: vllm-prod

resources:
  - ../../base

patches:
  - path: replica-patch.yaml
    target:
      kind: Deployment
      name: vllm

  - path: hpa-patch.yaml
    target:
      kind: HorizontalPodAutoscaler
      name: vllm

images:
  - name: vllm/vllm-openai
    newTag: v0.6.0

configMapGenerator:
  - name: vllm-config
    behavior: merge
    literals:
      - MODEL=meta-llama/Llama-3.1-70B
      - TENSOR_PARALLEL_SIZE=8
      - GPU_MEMORY_UTILIZATION=0.9

replicas:
  - name: vllm
    count: 4
```

### 5.2 多环境管理（Helm）

```yaml
# llm-chart/values.yaml (base)
image:
  repository: vllm/vllm-openai
  tag: v0.6.0
  pullPolicy: IfNotPresent

replicaCount: 1

resources:
  limits:
    nvidia.com/gpu: 8
    memory: 800Gi
  requests:
    nvidia.com/gpu: 8
    memory: 800Gi

vllm:
  model: meta-llama/Llama-3.1-70B
  tensorParallelSize: 8
  gpuMemoryUtilization: 0.9
  maxModelLen: 32768

service:
  type: ClusterIP
  port: 8000

autoscaling:
  enabled: false
---
# values-prod.yaml
replicaCount: 8

resources:
  limits:
    nvidia.com/gpu: 8
    memory: 1200Gi

vllm:
  model: meta-llama/Llama-3.1-70B
  tensorParallelSize: 8
  gpuMemoryUtilization: 0.92
  maxModelLen: 65536
  enablePrefixCaching: true

autoscaling:
  enabled: true
  minReplicas: 8
  maxReplicas: 32
  targetCPUUtilization: 70
  targetMemoryUtilization: 80

podDisruptionBudget:
  enabled: true
  minAvailable: 6

nodeSelector:
  gpu-type: H100-80GB
  topology.kubernetes.io/zone: cn-east-1a
```

### 5.3 ArgoCD App of Apps 模式

```yaml
# 应用的应用 (一套 root app 管理 N 个子 app)
apiVersion: argoproj.io/v1alpha1
kind: Application
metadata:
  name: llm-platform-prod
  namespace: argocd
spec:
  project: llm-platform
  source:
    repoURL: https://github.com/org/llm-manifests
    targetRevision: main
    path: prod/apps  # 此目录下有多个子 Application
  destination:
    server: https://kubernetes.default.svc
    namespace: argocd
  syncPolicy:
    automated:
      prune: true
      selfHeal: true
```

```
prod/apps/
├── vllm-app.yaml
├── tgi-app.yaml
├── router-app.yaml
├── monitoring-app.yaml
└── ingress-app.yaml
```

### 5.4 ApplicationSet 多集群

```yaml
apiVersion: argoproj.io/v1alpha1
kind: ApplicationSet
metadata:
  name: vllm-multi-cluster
spec:
  generators:
    - list:
        elements:
          - cluster: prod-cn-east
            url: https://prod-cn-east-api:6443
          - cluster: prod-cn-north
            url: https://prod-cn-north-api:6443
          - cluster: prod-us-west
            url: https://prod-us-west-api:6443
  template:
    metadata:
      name: 'vllm-{{cluster}}'
    spec:
      project: llm-platform
      source:
        repoURL: https://github.com/org/llm-manifests
        targetRevision: main
        path: 'overlays/{{cluster}}'
      destination:
        server: '{{url}}'
        namespace: vllm-prod
      syncPolicy:
        automated:
          prune: true
          selfHeal: true
```

---

## 六、底层原理

### 6.1 Reconcile Loop（协调循环）

```
ArgoCD Application Controller 工作流:

  每 3s 触发一次:
    │
    ▼
  1. 从 Git 拉取 latest manifest (Repo Server 缓存)
    │
    ▼
  2. 渲染 (Helm template / kustomize build)
    │
    ▼
  3. 比对 Git 期望状态 vs 集群实际状态
    │
    ├── 一致: 不操作
    │
    └── 不一致:
         │
         ├── SyncPolicy.Automated = true: 自动 apply
         │   ├── prune: 删除 Git 中已移除
         │   └── selfHeal: 修复手动修改
         │
         └── SyncPolicy.Automated = false: 等待手动 Sync
```

### 6.2 漂移检测

```
漂移场景:
  1. kubectl edit 直改资源
  2. kubectl scale 手动扩缩
  3. HPA 修改副本数 (合法, 应排除)
  4. 准入 webhook 注入字段 (合法, 应排除)

ArgoCD 漂移检测:
  - 默认每 3s 比对 live vs desired
  - 检测到差异:
    - selfHeal=true: 自动 apply 覆盖
    - selfHeal=false: 仅 UI 标记 OutOfSync

排除字段 (避免误报):
  ignoreDifferences:
    - group: apps
      kind: Deployment
      jsonPointers:
        - /spec/replicas  # HPA 管理
    - group: ""
      kind: Service
      jsonPointers:
        - /spec/clusterIP  # K8s 自动分配
```

### 6.3 Sync 时机与策略

```
Sync Hook:
  PreSync:  执行迁移、备份、检查
  Sync:     应用主资源
  PostSync: 健康检查、SLI 验证
  SyncFail: 失败时通知/回滚

Prune 策略:
  foreground: 先删子资源再删父 (推荐)
  background: 删父后异步删子
  orphan: 仅删父, 子资源孤儿化

健康检查:
  - 内置支持 Deployment/StatefulSet/Service/Ingress
  - 自定义 Lua 脚本检查 CRD 健康
```

### 6.4 GitOps 流水线全貌

```
开发流程:
  1. 工程师改 manifests (PR)
  2. CI 校验: kubeval / kubeconform / conftest
  3. PR Review
  4. Merge to main
  5. ArgoCD/Flux 检测到变更
  6. Sync 到 staging
  7. 自动化测试 + canary
  8. 手动 promote 到 prod (PR 到 prod 目录)
  9. ArgoCD Sync prod
 10. 渐进式发布 (Argo Rollouts, 15 章)

回滚:
  git revert + push
  ArgoCD 自动 sync 回滚版本
```

---

## 七、代码与配置示例

### 7.1 LLM 推理平台完整 GitOps 结构

```
llm-platform/
├── .github/
│   └── workflows/
│       ├── manifest-validate.yaml   # kubeconform + conftest
│       └── image-promote.yaml       # 镜像晋升 dev→staging→prod
├── apps/
│   ├── base/
│   │   ├── vllm/
│   │   │   ├── deployment.yaml
│   │   │   ├── service.yaml
│   │   │   ├── configmap.yaml
│   │   │   ├── hpa.yaml
│   │   │   ├── pdb.yaml
│   │   │   └── kustomization.yaml
│   │   ├── router/
│   │   └── monitor/
│   ├── overlays/
│   │   ├── dev/
│   │   ├── staging/
│   │   └── prod/
│   └── argocd/
│       ├── app-of-apps.yaml
│       └── applicationset.yaml
├── policies/
│   ├── opa/
│   │   ├── require-resources.rego
│   │   ├── require-labels.rego
│   │   └── no-latest-tag.rego
│   └── kyverno/
└── scripts/
    └── promote.sh
```

### 7.2 PreSync 数据库迁移

```yaml
apiVersion: batch/v1
kind: Job
metadata:
  name: db-migrate
  annotations:
    argocd.argoproj.io/hook: PreSync
    argocd.argoproj.io/hook-delete-policy: BeforeHookCreation
spec:
  template:
    spec:
      restartPolicy: OnFailure
      containers:
        - name: migrate
          image: registry/org/llm-api:v1.5.0
          command: ["python", "-m", "app.migrate"]
          env:
            - name: DATABASE_URL
              valueFrom:
                secretKeyRef:
                  name: db-credentials
                  key: url
```

### 7.3 conftest 策略校验

```rego
# policies/opa/require-resources.rego
package main

deny[msg] {
  resource := input[_]
  resource.kind == "Deployment"
  not resource.spec.template.spec.containers[_].resources.limits.cpu
  msg := sprintf("Deployment %s 缺少 CPU limit", [resource.metadata.name])
}

deny[msg] {
  resource := input[_]
  resource.kind == "Deployment"
  not resource.spec.template.spec.containers[_].resources.limits.memory
  msg := sprintf("Deployment %s 缺少 memory limit", [resource.metadata.name])
}

deny[msg] {
  resource := input[_]
  resource.kind == "Deployment"
  not resource.spec.template.spec.affinity
  msg := sprintf("Deployment %s 缺少 affinity (跨可用区)", [resource.metadata.name])
}
```

```yaml
# .github/workflows/manifest-validate.yaml
name: Manifest Validation
on:
  pull_request:
    paths:
      - 'apps/**'
jobs:
  validate:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Kubeconform
        run: |
          docker run --rm -v $(pwd):/work garethr/kubeconform:latest \
            -skip ReplicationController \
            -summary apps/overlays/prod
      - name: Conftest
        run: |
          docker run --rm -v $(pwd):/work openpolicyagent/conftest:v0.45.0 \
            test -p policies/opa apps/overlays/prod
```

### 7.4 镜像晋升流水线

```python
# scripts/promote.py
import sys
import yaml
from pathlib import Path

def promote(component: str, version: str, env: str):
    """把指定组件的镜像版本晋升到指定环境."""
    values_file = Path(f"apps/overlays/{env}/{component}/values.yaml")
    with open(values_file) as f:
        docs = list(yaml.safe_load_all(f))

    for doc in docs:
        if doc and doc.get('image'):
            doc['image']['tag'] = version

    with open(values_file, 'w') as f:
        yaml.safe_dump_all(docs, f, default_flow_style=False, sort_keys=False)

    print(f"晋升 {component} 到 {env}: {version}")

if __name__ == "__main__":
    promote(sys.argv[1], sys.argv[2], sys.argv[3])
```

---

## 八、Secret 治理

### 8.1 Secret 管理方案对比

| 方案 | 加密 | 轮换 | 审计 | 复杂度 |
|------|------|------|------|--------|
| 原生 Secret | 无 | 手动 | 无 | 低 |
| Sealed Secrets | 非对称 | 手动 | Git | 中 |
| SOPS + age/GPG | 对称/非对称 | 手动 | Git | 中 |
| External Secrets Operator | 外部 KMS | 自动 | Vault | 高 |
| HashiCorp Vault | 动态 | 自动 | 完整 | 高 |

### 8.2 Sealed Secrets

```bash
# 1. 安装
helm install sealed-secrets sealed-secrets/sealed-secrets

# 2. 加密
echo -n 'my-secret-key' | kubectl create secret generic api-key \
  --dry-run=client --from-file=key=/dev/stdin -o yaml | \
  kubeseal --controller-namespace=kube-system -o yaml > sealed-secret.yaml

# 3. 提交到 Git
git add sealed-secret.yaml
git commit -m "feat: add sealed secret for api-key"
```

```yaml
# sealed-secret.yaml (可安全提交 Git)
apiVersion: bitnami.com/v1alpha1
kind: SealedSecret
metadata:
  name: api-key
  namespace: vllm-prod
spec:
  encryptedData:
    key: AgBh...encrypted-blob...
  template:
    metadata:
      name: api-key
      namespace: vllm-prod
```

### 8.3 External Secrets Operator + Vault

```yaml
# 1. SecretStore (连接 Vault)
apiVersion: external-secrets.io/v1beta1
kind: SecretStore
metadata:
  name: vault-backend
  namespace: vllm-prod
spec:
  provider:
    vault:
      server: "https://vault.internal:8200"
      path: "kv"
      version: "v2"
      auth:
        kubernetes:
          mountPath: "kubernetes"
          role: "vllm-prod"
---
# 2. ExternalSecret (声明需要的密钥)
apiVersion: external-secrets.io/v1beta1
kind: ExternalSecret
metadata:
  name: vllm-secrets
  namespace: vllm-prod
spec:
  refreshInterval: 1h
  secretStoreRef:
    name: vault-backend
    kind: SecretStore
  target:
    name: vllm-secrets
    creationPolicy: Owner
  data:
    - secretKey: hf-token
      remoteRef:
        key: llm-platform/hf-token
        property: token
    - secretKey: api-key
      remoteRef:
        key: llm-platform/openai-key
        property: key
```

### 8.4 SOPS + age

```bash
# 1. 生成 age 密钥
age-keygen -o keys.txt
# public key: age1...

# 2. .sops.yaml 配置
cat > .sops.yaml <<EOF
creation_rules:
  - path_regex: secrets/.*\.yaml$
    age: age1...your-public-key
EOF

# 3. 加密
sops --encrypt --in-place secrets/vllm-prod.yaml

# 4. 提交 Git
git add secrets/vllm-prod.yaml
```

---

## 九、常见陷阱与调优

### 9.1 陷阱 1：Sync 失败雪崩

**症状**：一个资源失败, 整个 App 标记 Degraded。

**修复**：
- `syncOptions: - ApplyOutOfSyncOnly=true` 仅同步差异
- `syncOptions: - FailOnSharedResource=false` 共享资源不阻断
- 拆分 App, 降低单 App 复杂度

### 9.2 陷阱 2：selfHeal 与 HPA 冲突

**症状**：HPA 修改 replicas, ArgoCD selfHeal 又改回。

**修复**：
```yaml
ignoreDifferences:
  - group: apps
    kind: Deployment
    jsonPointers:
      - /spec/replicas
```

### 9.3 陷阱 3：CRD 升级顺序

**症状**：CRD 还没更新, CR 先 Sync, 失败。

**修复**：
- `syncOptions: - ServerSideApply=true`
- `syncOptions: - PrunePropagationPolicy=foreground`
- 拆分 CRD 与 CR 到不同 App, CRD App 先 Sync

### 9.4 陷阱 4：Secret 泄露

**症状**：明文 Secret 提交到 Git, 公开仓库泄露。

**修复**：
- 必用 Sealed Secrets / SOPS / ESO
- pre-commit hook 检测 Secret
- GitGuardian / TruffleHog 扫描历史

### 9.5 陷阱 5：大规模集群 Sync 风暴

**症状**：3000+ 资源的 App, 一次 Sync 5 分钟。

**修复**：
- 按命名空间/团队拆分 App
- `syncOptions: - ApplyOutOfSyncOnly=true`
- ArgoCD Redis 持久化

### 9.6 调优 Checklist

- [ ] Application 按业务域拆分, 单 App 资源数 < 500
- [ ] 使用 ApplicationSet 管理多集群
- [ ] 配置 ignoreDifferences 避免 HPA 等误报
- [ ] Secret 全部用 ESO/Sealed Secrets
- [ ] CI 集成 kubeconform + conftest
- [ ] PreSync/PostSync hook 处理迁移与验证
- [ ] 监控 ArgoCD 自身指标 (sync_duration, reconcile_duration)
- [ ] 配置 SLO: P99 sync 时间 < 30s

---

## 十、工业案例与基准数据

### 10.1 案例 1：字节跳动 LLM 推理平台 GitOps

**背景**：豆包大模型推理平台, 多集群, 多模型, 多地域。

**方案**：
- ArgoCD 中心化管理 (1 个 ArgoCD 管 20+ 集群)
- ApplicationSet 按集群+模型矩阵生成 App
- Kustomize 多环境 overlay
- 自研镜像晋升流水线 (dev→staging→canary→prod)
- ESO + 内部 KMS 管理 Secret

**规模**：
- 2000+ Application
- 50K+ K8s 资源
- 日均 200+ 次 Sync
- P99 Sync 耗时: 25s

### 10.2 案例 2：阿里巴巴 PAI-EAS GitOps

**背景**：PAI-EAS 推理服务发布平台。

**方案**：
- 自研 GitOps 控制器 (基于 Flux 二开)
- Helm Chart 多环境 values
- 集成 OPA Gatekeeper 准入校验
- 与 PAI 内部模型仓库联动

**效果**：
- 发布频率: 100+/天
- 失败回滚: 平均 8s (git revert + sync)
- 漂移率: < 0.1%

### 10.3 案例 3：OpenAI 内部发布平台

**背景**：OpenAI 推理集群发布 (公开资料有限, 推测)。

**推测方案**：
- 类似 ArgoCD 的自研系统
- 强类型 manifest 校验 (TypeScript + JSON Schema)
- 与内部 Feature Flag 系统集成 (16 章)
- 多阶段发布: 1% → 5% → 25% → 100% (15 章)

### 10.4 案例 4：Red Hat OpenShift GitOps

**背景**：Red Hat 商业化 ArgoCD (OpenShift GitOps)。

**方案**：
- ArgoCD 作为 OpenShift 默认 GitOps 引擎
- 集成 OpenShift OAuth/RBAC
- 与 OpenShift Pipelines (Tekton) 联动
- 提供 OperatorHub 一键安装

**适用**：传统企业 K8s 上云。

### 10.5 Sync 性能基准

| App 资源数 | Sync 耗时 (P50) | Sync 耗时 (P99) |
|-----------|----------------|----------------|
| 50 | 2s | 5s |
| 500 | 8s | 20s |
| 2000 | 25s | 60s |
| 5000 | 90s | 180s |

**结论**：单 App 资源数建议 < 500, 超过则拆分。

---

## 十一、与其他方案的关系

### 11.1 GitOps vs 传统 CI/CD

| 维度 | GitOps (Pull) | 传统 CI/CD (Push) |
|------|--------------|-------------------|
| 触发 | Agent 拉取 | CI 推送 |
| 凭证 | Agent 持 Git 凭证 | CI 持集群 kubeconfig |
| 多集群 | 每集群独立 Agent | CI 需多套 kubeconfig |
| 漂移检测 | 内置 | 无 |
| 回滚 | git revert | 重新跑 pipeline |
| 网络要求 | 集群出站到 Git | CI 入站到集群 |
| 适用 | K8s 原生 | 混合云/传统部署 |

### 11.2 ArgoCD vs Flux

| 场景 | 推荐 |
|------|------|
| 需要 UI 与精细 RBAC | ArgoCD |
| 多集群中心化管控 | ArgoCD (ApplicationSet) |
| GitOps 原教旨 (纯 CLI) | Flux |
| 边缘集群联邦自治 | Flux |
| 与 Argo Rollouts 渐进式发布 | ArgoCD |
| 与 Flagger 渐进式发布 | Flux |
| 中小团队快速上手 | Flux |
| 大型企业多团队协作 | ArgoCD |

### 11.3 Kustomize vs Helm

| 维度 | Kustomize | Helm |
|------|-----------|------|
| 学习曲线 | 低 (YAML) | 中 (模板) |
| 灵活性 | 模板 + patch | 模板语法 |
| 包管理 | 无 | Chart 仓库 |
| 多环境 | overlay 结构自然 | values 文件 |
| 模板错误 | 少 (无模板) | 模板渲染错误常见 |
| 生态 | K8s 原生 (kubectl -k) | 庞大 Chart 生态 |
| 适用 | 内部应用 | 第三方应用 (Bitnami/Prometheus) |

---

## 十二、面试速答

**Q1: 什么是 GitOps? 与传统 CI/CD 区别?**

A: GitOps 以 Git 为唯一可信源, 声明式描述系统状态, 集群内 Agent 持续 reconcile。区别于传统 CI/CD 的 Push 模型: 1) 触发方式 (Pull vs Push); 2) 凭证 (Agent 持 Git 凭证 vs CI 持集群凭证); 3) 漂移检测 (内置 vs 无); 4) 回滚 (git revert vs 重跑 pipeline)。

**Q2: ArgoCD 与 Flux 怎么选?**

A: ArgoCD 适合需要 UI/RBAC/多集群中心化管控的大企业; Flux 适合 GitOps 原教旨/边缘集群联邦自治/中小团队。ArgoCD 配 Argo Rollouts, Flux 配 Flagger, 是两套主流渐进式发布组合。

**Q3: GitOps 怎么管理 Secret?**

A: 三种方案: 1) Sealed Secrets (非对称加密, 提交 Git); 2) SOPS + age/GPG (文件级加密); 3) External Secrets Operator + Vault/KMS (动态拉取, 不入 Git)。大型生产推荐 ESO + Vault, 中小型用 Sealed Secrets 或 SOPS。

**Q4: 漂移检测怎么做? HPA 与 selfHeal 冲突怎么办?**

A: ArgoCD 默认 3s 比对 live vs desired, 不一致则 OutOfSync, selfHeal=true 自动覆盖。HPA 等合法漂移用 `ignoreDifferences` 排除 `spec.replicas` 字段。

**Q5: ArgoCD 单 App 资源数为什么建议 < 500?**

A: Sync 性能随资源数线性下降: 500 资源 P99 ~20s, 2000 资源 P99 ~60s, 5000 资源 P99 ~3min。拆分 App 还能隔离故障 (一个 App 失败不影响其他), 提升 reconcile 并发度。

---

## 十三、综合面试题

### 题 1（中级）：设计 LLM 推理平台 GitOps 流水线

**答题要点**：

1. **结构**：
   - `apps/base/vllm` (Kustomize base)
   - `apps/overlays/{dev,staging,prod}`
   - `apps/argocd/app-of-apps.yaml`

2. **环境晋升**：
   - dev: PR merge 自动 Sync
   - staging: PR + 自动化测试通过后 Sync
   - prod: PR + 人工审批 + Argo Rollouts 渐进式

3. **多集群**：ApplicationSet 按集群+模型矩阵生成 App

4. **Secret**：ESO + Vault, namespace 级别 RBAC

5. **校验**：
   - CI: kubeconform + conftest (OPA)
   - ArgoCD: PreSync hook 跑迁移
   - PostSync: 健康检查 + SLI 验证

6. **回滚**：`git revert` + push, ArgoCD 自动 Sync 回滚版本, 30s 内生效

7. **监控**：ArgoCD Prometheus 指标 (sync_duration, controller.reconcile)

### 题 2（高级）：GitOps 在万卡推理集群的工程挑战

**答题要点**：

1. **规模挑战**：
   - 2000+ Application, 50K+ 资源
   - 单 ArgoCD 实例瓶颈: Redis 内存, controller 并发

2. **拆分策略**：
   - 按业务域拆分 (LLM/Embedding/Reranker/VLM)
   - 按集群拆分 (ApplicationSet)
   - 按环境拆分 (dev/staging/prod App)

3. **性能优化**:
   - `ApplyOutOfSyncOnly=true` 仅同步差异
   - `ServerSideApply=true` 减少 API server 压力
   - ArgoCD 横向扩展 (multiple controller shards)

4. **可靠性**:
   - ArgoCD HA (多副本 + Redis Sentinel)
   - Git 仓库多源备份 (GitHub + GitLab mirror)
   - 离线模式 (集群断网时 Agent 缓存)

5. **安全**:
   - ArgoCD SSO + RBAC (按项目隔离)
   - ESO + Vault namespace 隔离
   - OPA Gatekeeper 准入校验

6. **观测**:
   - Prometheus + Grafana 看 sync 成功率/耗时
   - 漂移率 SLO < 0.1%
   - 失败告警接 PagerDuty

---

## 十四、故障复盘

### 14.1 案例 1：selfHeal 误删 HPA

**背景**：2024 年某公司 ArgoCD selfHeal 把 HPA 修改的 replicas 改回, 导致高峰期容量不足。

**根因**：未配置 `ignoreDifferences`, selfHeal 把 HPA 的合法修改当作漂移纠正。

**修复**：
```yaml
ignoreDifferences:
  - group: apps
    kind: Deployment
    jsonPointers:
      - /spec/replicas
```

**防范**：所有受 HPA 管理的 Deployment 必须配置 `ignoreDifferences`。

### 14.2 案例 2：CRD 升级导致 Sync 雪崩

**背景**：2025 年某公司升级 Argo Rollouts CRD, 旧版本 CRD 不识别新字段, 所有 Rollout Sync 失败。

**根因**：CRD 与 CR 在同一 App, CRD 升级失败导致后续 CR 全部失败。

**修复**：拆分 CRD 到独立 App, 先 Sync CRD App, 再 Sync CR App。`syncOptions: - Replace=true` 强制替换。

**防范**：CRD 单独 App 管理, 升级前先 dry-run。

### 14.3 案例 3：Secret 泄露到公开仓库

**背景**：2024 年某公司工程师把 OpenAI API Key 明文提交到公开 GitHub 仓库, 1 小时内被爬虫扫到, 损失 $50K。

**根因**：未用 Sealed Secrets/ESO, 缺少 pre-commit hook。

**修复**：
1. 立即吊销 Key
2. 全员培训 Sealed Secrets
3. 配置 pre-commit hook (detect-secrets)
4. 接入 GitGuardian 历史扫描

**防范**：所有 Secret 必须加密, CI 强制扫描, 仓库历史定期审计。

### 14.4 案例 4：ArgoCD Redis OOM

**背景**：2024 年某公司 ArgoCD Redis 持续 OOM, 导致 Sync 卡顿。

**根因**：2000+ Application 的 cache 全在 Redis, 内存不足。

**修复**：
- Redis 内存从 4G → 16G
- `ARGOCD_RECONCILIATION_TIMEOUT` 调大, 减少 reconcile 频率
- 拆分 ArgoCD 实例 (按业务域分 shard)

**防范**：大型集群规划 ArgoCD 容量, 单实例建议 < 1000 App。

### 14.5 案例 5：Git 仓库故障导致全局 Sync 停滞

**背景**：2025 年某公司 GitHub 故障 1 小时, ArgoCD 无法拉取 manifest, 全局 Sync 停滞。

**根因**：单源 Git 依赖, 无降级方案。

**修复**：
- 配置 GitLab mirror, 失败自动切换
- ArgoCD Repo Server 缓存最新 manifest, Git 故障时用缓存
- 关键环境配置人工 Sync 通道 (kubectl apply 备份)

**防范**：Git 多源 (GitHub + GitLab + 自建), ArgoCD Repo Server 持久化缓存。

---

## 十五、参考与延伸

### 15.1 工具

- ArgoCD — https://argo-cd.readthedocs.io/
- Flux — https://fluxcd.io/
- Argo Rollouts — https://argo-rollouts.readthedocs.io/
- Flagger — https://flagger.app/
- Sealed Secrets — https://github.com/bitnami-labs/sealed-secrets
- External Secrets Operator — https://external-secrets.io/
- SOPS — https://github.com/getsops/sops
- Kustomize — https://kustomize.io/
- Helm — https://helm.sh/
- conftest — https://www.conftest.dev/
- OpenGitOps — https://opengitops.dev/

### 15.2 跨模块链接

- [15-渐进式发布策略](./15-渐进式发布策略.md) —— Argo Rollouts/Flagger 实战
- [16-特征开关与实验平台](./16-特征开关与实验平台.md) —— 解耦发布与上线
- [11-冷启动优化与Scale-to-Zero](./11-冷启动优化与Scale-to-Zero.md) —— GitOps 部署 Serverless
- [22-混沌工程与稳定性验证](./22-混沌工程与稳定性验证.md) —— 漂移即故障演练
