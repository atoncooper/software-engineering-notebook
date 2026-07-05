# 16. CI/CD 与 Docker

> 章节定位: 生产实战篇 · 第二章
> 前置章节: [15-Dockerfile生产模板](./15-Dockerfile生产模板.md)
> 后续章节: [17-生产实践](./17-生产实践.md)

---

## 16.1 思维导图

```
                CI/CD with Docker
                      │
        ┌─────────────┼─────────────┐
        │             │             │
      CI 构建       CD 部署       GitOps
        │             │             │
   ┌────┴────┐   ┌────┴────┐   ┌────┴────┐
   │         │   │         │   │         │
 Dockerfile 镜像构建   滚动更新     ArgoCD
 BuildKit  多平台     蓝绿         Flux
 远程缓存   签名       金丝雀       Helm
 扫描                回滚         Kustomize
        │             │             │
        └─────────────┼─────────────┘
                      │
                      ▼
              流水线工具
                      │
   ┌──────────────────┼──────────────────┐
   │                  │                  │
GitHub Actions    GitLab CI    Jenkins / Tekton
```

**CI/CD 流水线 7 阶段**:

| 阶段 | 输入 | 输出 | 工具 |
|------|------|------|------|
| 1. 代码检查 | PR | Lint 结果 | hadolint, eslint |
| 2. 单元测试 | 源码 | 测试报告 | pytest, junit |
| 3. 构建 | 源码 | 镜像 | docker build |
| 4. 扫描 | 镜像 | CVE/SBOM | trivy, syft |
| 5. 签名 | 镜像 | 签名 | cosign |
| 6. 推送 | 镜像 | registry | docker push |
| 7. 部署 | 镜像+清单 | 生产环境 | argocd, kubectl |

---

## 16.2 章节简介

CI/CD(持续集成/持续部署)是现代软件工程的核心实践。Docker 作为标准化交付物,使 CI/CD 流水线变得可重现、可追溯、可回滚。

本章从工业实践出发,系统讲解:
1. **CI 阶段**: 镜像构建、缓存策略、多平台构建、安全扫描
2. **CD 阶段**: 滚动更新、蓝绿部署、金丝雀发布、自动回滚
3. **GitOps**: 声明式部署(ArgoCD/Flux),Git 作为唯一真相源
4. **流水线工具**: GitHub Actions / GitLab CI / Jenkins / Tekton 对比

**本章工业焦点**:
- 阿里 Aone 流水线日均 10万次构建
- 字节跳动 CI/CD 全链路优化(构建 25min→3min)
- Google Bazel + Docker 大规模构建
- Netflix Spinnaker 金丝雀部署
- GitOps 在大厂的标准化(ArgoCD 80% 占比)

---

## 16.3 核心概念

### 16.3.1 CI vs CD vs Continuous Deployment

```
CI (Continuous Integration)
└─ 代码合并到主干前自动测试+构建
   目标: 早发现问题

CD (Continuous Delivery)
└─ 持续交付: 任意时刻可发布(需人工点按钮)

CD (Continuous Deployment)
└─ 持续部署: 自动部署到生产(无人工)
   目标: 快速交付价值
```

**流水线全貌**:

```
开发 → PR → CI → 制品 → CD → 生产
 │     │     │     │      │     │
commit  自动   镜像   仓库   自动   部署
       测试   构建         部署
       构建                ↓
       扫描              监控
       签名              ↓
                        自动回滚(异常)
```

### 16.3.2 Docker 在 CI/CD 中的角色

```
┌────────────────────────────────────────────┐
│ Docker 解决的问题                          │
├────────────────────────────────────────────┤
│ 1. 环境一致性: 本地=CI=生产               │
│ 2. 依赖隔离: 不同项目不冲突               │
│ 3. 可重现构建: 同输入同输出              │
│ 4. 标准化交付: 镜像 = 制品                │
│ 5. 不可变部署: 替换而非修改               │
│ 6. 快速回滚: 切换 image tag              │
└────────────────────────────────────────────┘
```

### 16.3.3 部署策略对比

| 策略 | 描述 | 切换时间 | 资源开销 | 回滚速度 | 风险 |
|------|------|---------|---------|---------|------|
| 重建 | 停旧起新 | 慢 | 1x | 慢 | 中断 |
| 滚动 | 逐步替换 | 中 | 1x-1.5x | 中 | 中 |
| 蓝绿 | 双环境切换 | 快 | 2x | 快 | 低 |
| 金丝雀 | 小流量验证 | 慢 | 1.1x-2x | 快 | 极低 |
| 影子 | 镜像流量 | - | 2x | - | 最低 |

**滚动更新**(默认):
```
v1 v1 v1 v1 v1 v1
   ↓
v1 v1 v1 v1 v1 v2
   ↓
v1 v1 v1 v1 v2 v2
   ↓
...
v2 v2 v2 v2 v2 v2
```

**蓝绿部署**:
```
蓝(v1) ──100%流量──► 用户
绿(v2) ──待命──────

切换:
蓝(v1) ──待命──────
绿(v2) ──100%流量──► 用户

回滚: 切回蓝
```

**金丝雀部署**:
```
v1: ──95%──► 用户
v2: ──5%───► 用户(观察)

逐步:
v1: 80% / v2: 20%
v1: 50% / v2: 50%
v1: 0%  / v2: 100%
```

### 16.3.4 GitOps 核心原则

**4 原则**:
1. **声明式**: 系统状态用声明式描述(K8s YAML/Helm)
2. **版本控制**: Git 是唯一真相源
3. **自动拉取**: 部署工具自动同步(Pull,而非 Push)
4. **持续协调**: 实际状态不断向期望状态收敛

**Push vs Pull 模型**:

```
Push 模型(传统 CI/CD):
CI ──kubectl apply──► K8s
问题:
- CI 需 K8s 凭证(安全风险)
- K8s 状态变更不被记录
- 多集群难管理

Pull 模型(GitOps):
Git ──► ArgoCD/Flux(pod) ──watch──► K8s
优势:
- K8s 内部署,无需外部凭证
- 任何变更走 Git PR,可审计
- 自动同步,易回滚
```

### 16.3.5 制品管理

```
源码(Git) ──CI──► 镜像(Registry) ──CD──► 部署清单(Git) ──► K8s

两个 Git 仓库:
1. 应用源码仓库: myapp-src
2. 部署清单仓库: myapp-deploy (GitOps)
```

**好处**:
- 应用代码变更与部署解耦
- 部署清单可独立 review/审批
- 不同环境(dev/staging/prod)分支管理

---

## 16.4 底层原理

### 16.4.1 镜像构建缓存策略

```
┌─────────────────────────────────────────┐
│ 缓存层次                                │
├─────────────────────────────────────────┤
│ 1. 本地层缓存(同一机器)                │
│    - 镜像 layer 缓存                    │
│    - BuildKit cache mount(/var/cache)   │
├─────────────────────────────────────────┤
│ 2. 远程缓存(跨机器)                    │
│    - registry 缓存(--cache-from)        │
│    - 共享 cache mount(NFS)              │
├─────────────────────────────────────────┤
│ 3. 中间镜像(命名 tag)                  │
│    - myapp:builder-base                 │
│    - myapp:runtime-base                 │
└─────────────────────────────────────────┘
```

**远程缓存使用**:

```bash
# 1. 推送缓存到 registry
docker buildx build \
  --cache-to type=registry,ref=myorg/cache,mode=max \
  -t myapp:v1 \
  --push .

# 2. 下次构建拉取缓存
docker buildx build \
  --cache-from type=registry,ref=myorg/cache \
  -t myapp:v2 \
  --push .
```

**mode=min vs mode=max**:
- min: 仅最终镜像层(默认)
- max: 含中间阶段(多阶段构建用)

### 16.4.2 多平台构建

```bash
# 单次构建多平台
docker buildx build \
  --platform linux/amd64,linux/arm64,linux/arm/v7 \
  -t myapp:v1 \
  --push .
```

**底层: QEMU 模拟**:

```
Host: x86_64
  │
  ▼ buildx + QEMU
  ├─ linux/amd64  → 原生编译
  ├─ linux/arm64  → QEMU 模拟 arm64 指令
  └─ linux/arm/v7 → QEMU 模拟 armv7
```

**性能对比**:

| 方式 | amd64 | arm64 | 备注 |
|------|-------|-------|------|
| QEMU 模拟 | 1x | 10x 慢 | 通用但慢 |
| 跨节点构建 | 1x | 1x | 需 arm64 节点 |
| 原生构建 | 1x | 1x | 需对应架构机器 |

**生产推荐**: 跨节点构建(arm64 节点 + amd64 节点),性能与原生一致。

### 16.4.3 镜像签名链

```
源码 commit (Git)
    │
    │  CI 构建
    ▼
镜像 (Registry)
    │
    │  Cosign 签名
    ▼
签名 (Registry, 同 tag 不同后缀)
    │
    │  SBOM 生成
    ▼
SBOM (Registry)
    │
    │  K8s 准入控制(Kyverno)
    ▼
仅允许已签名 + SBOM 通过的镜像部署
```

**Cosign 签名存储**:
```
镜像: myapp:v1
签名: myapp:sha256-abc...sig
SBOM: myapp:sha256-abc...att
```

### 16.4.4 K8s 滚动更新原理

```yaml
spec:
  strategy:
    type: RollingUpdate
    rollingUpdate:
      maxSurge: 25%         # 最多超出期望副本 25%
      maxUnavailable: 25%   # 最多不可用 25%
```

**执行流程**:

```
初始: 4 副本 v1
├─ RS-v1: 4 副本

maxSurge=25%, maxUnavailable=25%
允许: 总数 4-5, 可用 3-5

步骤:
1. 启动 1 个 v2,RS-v2: 1
   总数 5,可用 5(4 v1 + 1 v2 readiness 通过)

2. 退役 1 个 v1,RS-v1: 3
   总数 4,可用 4(3 v1 + 1 v2)

3. 启动 1 个 v2,RS-v2: 2
   总数 5,可用 5(3 v1 + 2 v2)

...直到 RS-v1: 0, RS-v2: 4
```

**关键参数调优**:
- `maxUnavailable=0`: 永不减少可用,但需 maxSurge > 0
- `maxSurge=0`: 永不超出,但需 maxUnavailable > 0
- 生产推荐: maxUnavailable=0, maxSurge=20%(始终保持可用)

### 16.4.5 K8s 探针与部署

```yaml
livenessProbe:       # 存活探针(失败则重启)
  httpGet:
    path: /health
    port: 8080

readinessProbe:      # 就绪探针(失败则不入流量)
  httpGet:
    path: /ready
    port: 8080

startupProbe:        # 启动探针(慢启动应用)
  httpGet:
    path: /health
    port: 8080
  failureThreshold: 30
  periodSeconds: 10
```

**部署与探针配合**:

```
Pod 启动
  │
  ▼
startupProbe(最长 5min)
  │ 通过
  ▼
readinessProbe(每 10s)
  │ 通过 → 加入 Service Endpoints(接收流量)
  ▼
livenessProbe(每 10s)
  │ 失败 → 重启容器
```

**滚动更新中**:
- 新 Pod readinessProbe 通过才会加入
- 旧 Pod 收到 SIGTERM,preStop 等 readiness 失败再退

---

## 16.5 代码实现

### 16.5.1 GitHub Actions 流水线

`.github/workflows/ci.yml`:

```yaml
name: CI

on:
  push:
    branches: [main, develop]
    tags: ["v*"]
  pull_request:
    branches: [main]

env:
  REGISTRY: ghcr.io
  IMAGE_NAME: ${{ github.repository }}

jobs:
  # ===== 1. 代码检查 =====
  lint:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: hadolint/hadolint-action@v3.1.0
        with:
          dockerfile: Dockerfile
      - uses: actions/setup-node@v4
        with:
          node-version: 20
      - run: npm ci
      - run: npm run lint

  # ===== 2. 单元测试 =====
  test:
    runs-on: ubuntu-latest
    needs: lint
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-node@v4
        with:
          node-version: 20
      - run: npm ci
      - run: npm test -- --coverage
      - uses: codecov/codecov-action@v3

  # ===== 3. 构建镜像 =====
  build:
    runs-on: ubuntu-latest
    needs: test
    permissions:
      contents: read
      packages: write
    steps:
      - uses: actions/checkout@v4

      # 设置 BuildKit
      - uses: docker/setup-buildx-action@v3

      # 登录 registry
      - uses: docker/login-action@v3
        with:
          registry: ${{ env.REGISTRY }}
          username: ${{ github.actor }}
          password: ${{ secrets.GITHUB_TOKEN }}

      # 元数据(自动生成 tag)
      - id: meta
        uses: docker/metadata-action@v5
        with:
          images: ${{ env.REGISTRY }}/${{ env.IMAGE_NAME }}
          tags: |
            type=ref,event=branch
            type=ref,event=pr
            type=semver,pattern={{version}}
            type=semver,pattern={{major}}.{{minor}}
            type=sha,prefix=sha-

      # 构建并推送(带远程缓存)
      - uses: docker/build-push-action@v5
        with:
          context: .
          push: true
          tags: ${{ steps.meta.outputs.tags }}
          labels: ${{ steps.meta.outputs.labels }}
          cache-from: type=registry,ref=${{ env.REGISTRY }}/${{ env.IMAGE_NAME }}:cache
          cache-to: type=registry,ref=${{ env.REGISTRY }}/${{ env.IMAGE_NAME }}:cache,mode=max
          platforms: linux/amd64,linux/arm64
          build-args: |
            VERSION=${{ steps.meta.outputs.version }}
            COMMIT=${{ github.sha }}
            BUILD_DATE=${{ github.event.head_commit.timestamp }}

  # ===== 4. 安全扫描 =====
  scan:
    runs-on: ubuntu-latest
    needs: build
    steps:
      - uses: actions/checkout@v4

      # Trivy 扫描 CVE
      - uses: aquasecurity/trivy-action@master
        with:
          image-ref: ${{ env.REGISTRY }}/${{ env.IMAGE_NAME }}:sha-${{ github.sha }}
          format: sarif
          output: trivy-results.sarif
          severity: HIGH,CRITICAL
          exit-code: 1   # HIGH/CRITICAL 失败

      - uses: github/codeql-action/upload-sarif@v3
        with:
          sarif_file: trivy-results.sarif

      # SBOM 生成
      - uses: anchore/sbom-action@v0
        with:
          image: ${{ env.REGISTRY }}/${{ env.IMAGE_NAME }}:sha-${{ github.sha }}
          format: spdx-json
          output-file: sbom.spdx.json

      # 上传 SBOM
      - uses: actions/upload-artifact@v4
        with:
          name: sbom
          path: sbom.spdx.json

  # ===== 5. 镜像签名 =====
  sign:
    runs-on: ubuntu-latest
    needs: scan
    permissions:
      packages: write
      id-token: write  # OIDC
    steps:
      - uses: sigstore/cosign-installer@v3

      # keyless 签名(用 OIDC)
      - run: |
          cosign sign --yes \
            ${{ env.REGISTRY }}/${{ env.IMAGE_NAME }}:sha-${{ github.sha }}

      # 附加 SBOM
      - run: |
          cosign attach sbom \
            --sbom sbom.spdx.json \
            ${{ env.REGISTRY }}/${{ env.IMAGE_NAME }}:sha-${{ github.sha }}

  # ===== 6. 部署到 staging =====
  deploy-staging:
    runs-on: ubuntu-latest
    needs: sign
    if: github.ref == 'refs/heads/develop'
    environment: staging
    steps:
      - uses: actions/checkout@v4

      # 更新部署清单仓库
      - run: |
          git clone https://github.com/${{ github.repository }}-deploy deploy
          cd deploy
          # 更新 staging 镜像 tag
          sed -i "s|image:.*|image: ${{ env.REGISTRY }}/${{ env.IMAGE_NAME }}:sha-${{ github.sha }}|" staging/app.yaml
          git config user.name "CI"
          git config user.email "ci@example.com"
          git commit -am "deploy: staging ${{ github.sha }}"
          git push

  # ===== 7. 部署到生产 =====
  deploy-prod:
    runs-on: ubuntu-latest
    needs: deploy-staging
    if: startsWith(github.ref, 'refs/tags/v')
    environment:
      name: production
      url: https://myapp.example.com
    steps:
      - uses: actions/checkout@v4
      - run: |
          git clone https://github.com/${{ github.repository }}-deploy deploy
          cd deploy
          sed -i "s|image:.*|image: ${{ env.REGISTRY }}/${{ env.IMAGE_NAME }}:${{ github.ref_name }}|" prod/app.yaml
          git config user.name "CI"
          git config user.email "ci@example.com"
          git commit -am "deploy: prod ${{ github.ref_name }}"
          git push
```

### 16.5.2 GitLab CI 流水线

`.gitlab-ci.yml`:

```yaml
stages:
  - lint
  - test
  - build
  - scan
  - sign
  - deploy

variables:
  REGISTRY: registry.gitlab.com
  IMAGE: $REGISTRY/$CI_PROJECT_PATH
  DOCKER_BUILDKIT: 1
  BUILDX_VERSION: 0.11.2

# ===== 1. Lint =====
dockerfile-lint:
  stage: lint
  image: hadolint/hadolint:2.12.0-alpine
  script:
    - hadolint Dockerfile
  rules:
    - if: $CI_PIPELINE_SOURCE == "merge_request_event"

# ===== 2. 测试 =====
test:
  stage: test
  image: node:20-alpine
  cache:
    key: ${CI_COMMIT_REF_SLUG}
    paths:
      - node_modules/
  script:
    - npm ci
    - npm test -- --coverage
  coverage: /All files[^|]*\|[^|]*\s+([\d\.]+)/
  artifacts:
    reports:
      coverage_report:
        coverage_format: cobertura
        path: coverage/cobertura-coverage.xml

# ===== 3. 构建 =====
build:
  stage: build
  image: docker:24.0.7
  services:
    - docker:24.0.7-dind
  before_script:
    - echo $CI_REGISTRY_PASSWORD | docker login -u $CI_REGISTRY_USER --password-stdin $REGISTRY
    - wget -qO /usr/local/bin/buildx https://github.com/docker/buildx/releases/download/v${BUILDX_VERSION}/buildx-v${BUILDX_VERSION}.linux-amd64
    - chmod +x /usr/local/bin/buildx
    - docker buildx create --use
  script:
    - docker buildx build
        --cache-from type=registry,ref=$IMAGE:cache
        --cache-to type=registry,ref=$IMAGE:cache,mode=max
        --platform linux/amd64,linux/arm64
        --tag $IMAGE:sha-$CI_COMMIT_SHORT_SHA
        --tag $IMAGE:$CI_COMMIT_REF_SLUG
        --build-arg VERSION=$CI_COMMIT_TAG
        --build-arg COMMIT=$CI_COMMIT_SHA
        --push
        .
  rules:
    - if: $CI_COMMIT_BRANCH == "main"
    - if: $CI_COMMIT_TAG

# ===== 4. 扫描 =====
scan:
  stage: scan
  image:
    name: aquasec/trivy:0.48.0
    entrypoint: [""]
  script:
    - trivy image --severity HIGH,CRITICAL --exit-code 1 $IMAGE:sha-$CI_COMMIT_SHORT_SHA
  needs: [build]

# ===== 5. 签名 =====
sign:
  stage: sign
  image:
    name: gcr.io/projectsigstore/cosign:latest
    entrypoint: [""]
  script:
    - cosign sign --yes $IMAGE:sha-$CI_COMMIT_SHORT_SHA
  needs: [scan]

# ===== 6. 部署 =====
.deploy:
  image:
    name: alpine/git:latest
    entrypoint: [""]
  before_script:
    - git config --global user.email "ci@example.com"
    - git config --global user.name "GitLab CI"

deploy-staging:
  extends: .deploy
  stage: deploy
  environment: staging
  script:
    - git clone https://oauth2:$DEPLOY_TOKEN@gitlab.com/myorg/app-deploy.git
    - cd app-deploy
    - sed -i "s|image:.*|image: $IMAGE:sha-$CI_COMMIT_SHORT_SHA|" staging/app.yaml
    - git commit -am "deploy: staging $CI_COMMIT_SHORT_SHA"
    - git push
  rules:
    - if: $CI_COMMIT_BRANCH == "develop"

deploy-prod:
  extends: .deploy
  stage: deploy
  environment: production
  script:
    - git clone https://oauth2:$DEPLOY_TOKEN@gitlab.com/myorg/app-deploy.git
    - cd app-deploy
    - sed -i "s|image:.*|image: $IMAGE:$CI_COMMIT_TAG|" prod/app.yaml
    - git commit -am "deploy: prod $CI_COMMIT_TAG"
    - git push
  rules:
    - if: $CI_COMMIT_TAG
```

### 16.5.3 ArgoCD 部署清单

```yaml
# argocd-app.yaml
apiVersion: argoproj.io/v1alpha1
kind: Application
metadata:
  name: myapp-prod
  namespace: argocd
spec:
  source:
    repoURL: https://github.com/myorg/app-deploy
    targetRevision: main
    path: prod
  destination:
    server: https://kubernetes.default.svc
    namespace: prod
  syncPolicy:
    automated:
      prune: true        # 自动清理删除的资源
      selfHeal: true     # 自动同步(防手动改动)
    syncOptions:
      - CreateNamespace=true
      - PruneLast=true
  # 健康检查(等待 rollout 完成)
  ignoreDifferences:
    - group: apps
      kind: Deployment
      jsonPointers:
        - /spec/replicas
```

### 16.5.4 Argo Rollouts 金丝雀部署

```yaml
# rollout.yaml
apiVersion: argoproj.io/v1alpha1
kind: Rollout
metadata:
  name: myapp
spec:
  replicas: 10
  strategy:
    canary:
      canaryService: myapp-canary
      stableService: myapp-stable
      trafficRouting:
        nginx:
          stableIngress: myapp-ingress
      steps:
      - setWeight: 5           # 5% 流量到新版本
      - pause: { duration: 5m }
      - analysis:              # 自动分析指标
          templates:
          - templateName: success-rate
      - setWeight: 20          # 5% → 20%
      - pause: { duration: 10m }
      - setWeight: 50
      - pause: { duration: 10m }
      - setWeight: 100
  selector:
    matchLabels:
      app: myapp
  template:
    metadata:
      labels:
        app: myapp
    spec:
      containers:
      - name: app
        image: myapp:v1
        ports:
        - containerPort: 8080

---
# 自动分析(失败则回滚)
apiVersion: argoproj.io/v1alpha1
kind: AnalysisTemplate
metadata:
  name: success-rate
spec:
  args:
  - name: service-name
  metrics:
  - name: success-rate
    interval: 1m
    successCondition: result[0] >= 0.95
    failureLimit: 3   # 连续 3 次失败则回滚
    provider:
      prometheus:
        address: http://prometheus:9090
        query: |
          sum(rate(http_requests_total{service="{{args.service-name}}",status!~"5.."}[2m]))
          /
          sum(rate(http_requests_total{service="{{args.service-name}}"}[2m]))
```

### 16.5.5 Helm Chart 部署模板

```yaml
# Chart.yaml
apiVersion: v2
name: myapp
description: A production Helm chart
type: application
version: 1.0.0
appVersion: "1.0.0"
dependencies:
  - name: redis
    version: 18.0.0
    repository: https://charts.bitnami.com/bitnami
    condition: redis.enabled
```

```yaml
# values.yaml
replicaCount: 3

image:
  repository: myapp
  tag: ""
  pullPolicy: IfNotPresent

resources:
  limits:
    cpu: 500m
    memory: 512Mi
  requests:
    cpu: 100m
    memory: 128Mi

autoscaling:
  enabled: true
  minReplicas: 3
  maxReplicas: 20
  targetCPUUtilizationPercentage: 70

# 部署策略
deploymentStrategy:
  type: RollingUpdate
  rollingUpdate:
    maxSurge: 25%
    maxUnavailable: 0

# 健康检查
probes:
  liveness:
    httpGet:
      path: /health
      port: 8080
    initialDelaySeconds: 30
    periodSeconds: 10
  readiness:
    httpGet:
      path: /ready
      port: 8080
    initialDelaySeconds: 5
    periodSeconds: 5
  startup:
    httpGet:
      path: /health
      port: 8080
    failureThreshold: 30
    periodSeconds: 10

# 安全上下文
podSecurityContext:
  runAsNonRoot: true
  runAsUser: 65532
  fsGroup: 65532

securityContext:
  readOnlyRootFilesystem: true
  allowPrivilegeEscalation: false
  capabilities:
    drop: [ALL]
```

---

## 16.6 配置示例

### 16.6.1 远程缓存配置

```bash
# BuildKit 远程缓存
docker buildx build \
  --cache-from type=registry,ref=myorg/cache,mode=max \
  --cache-to type=registry,ref=myorg/cache,mode=max \
  -t myapp:v1 \
  --push .

# 本地目录缓存(CI runner 共享)
docker buildx build \
  --cache-from type=local,src=/tmp/.buildx-cache \
  --cache-to type=local,dest=/tmp/.buildx-cache-new \
  -t myapp:v1 .

# S3 缓存
docker buildx build \
  --cache-from type=s3,ref=mybucket/cache \
  --cache-to type=s3,ref=mybucket/cache \
  -t myapp:v1 .
```

### 16.6.2 多平台构建配置

```bash
# 1. 创建 multi-platform builder
docker buildx create \
  --name multi-builder \
  --driver docker-container \
  --platform linux/amd64,linux/arm64 \
  --use

# 2. 启动 QEMU(模拟其他架构)
docker run --privileged --rm tonistiigi/binfmt --install all

# 3. 构建
docker buildx build \
  --platform linux/amd64,linux/arm64,linux/arm/v7 \
  -t myapp:v1 \
  --push .
```

### 16.6.3 Cosign + Kyverno 准入控制

```bash
# 1. 生成密钥对
cosign generate-key-pair

# 2. 签名镜像
COSIGN_PASSWORD=xxx cosign sign --key cosign.key myapp:v1

# 3. 验证签名
cosign verify --key cosign.pub myapp:v1
```

```yaml
# Kyverno 策略: 仅允许已签名镜像
apiVersion: kyverno.io/v1
kind: ClusterPolicy
metadata:
  name: require-signed-images
spec:
  validationFailureAction: Enforce
  rules:
  - name: verify-signature
    match:
      resources:
        kinds:
        - Pod
    verifyImages:
    - imageReferences:
      - "myorg/*"
      attestors:
      - entries:
        - keys:
            publicKeys: |
              -----BEGIN PUBLIC KEY-----
              ...
              -----END PUBLIC KEY-----
```

### 16.6.4 自动回滚配置

```yaml
# K8s 部署 + 失败自动回滚
apiVersion: apps/v1
kind: Deployment
metadata:
  name: myapp
  annotations:
    # ArgoCD rollback 注解
    argocd.argoproj.io/sync-options: PrunePropagationPolicy=foreground
spec:
  minReadySeconds: 30    # Pod ready 30s 后才认为可用
  progressDeadlineSeconds: 600  # 10min 内未完成则失败
  strategy:
    type: RollingUpdate
    rollingUpdate:
      maxSurge: 1
      maxUnavailable: 0
```

```bash
# kubectl 自动回滚(超时未就绪)
kubectl rollout status deployment/myapp --timeout=10m

# 失败则回滚
if ! kubectl rollout status deployment/myapp --timeout=10m; then
  kubectl rollout undo deployment/myapp
  exit 1
fi
```

### 16.6.5 环境隔离配置

```
myapp-deploy/
├── base/                  # 公共基础
│   ├── deployment.yaml
│   ├── service.yaml
│   └── kustomization.yaml
├── overlays/
│   ├── dev/
│   │   ├── kustomization.yaml
│   │   └── patches.yaml    # 1 副本,小资源
│   ├── staging/
│   │   └── ...
│   └── prod/
│       └── ...             # 3 副本,大资源,PDB
```

`base/deployment.yaml`:

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: myapp
spec:
  replicas: 1
  template:
    spec:
      containers:
      - name: app
        image: myapp:latest
        resources:
          requests:
            cpu: 100m
            memory: 128Mi
```

`overlays/prod/kustomization.yaml`:

```yaml
apiVersion: kustomize.config.k8s.io/v1beta1
kind: Kustomization
namespace: prod
resources:
- ../../base
patches:
- replicas: 3
  target:
    kind: Deployment
    name: myapp
- target:
    kind: Deployment
    name: myapp
  patch: |
    - op: replace
      path: /spec/template/spec/containers/0/resources/requests/cpu
      value: 500m
```

---

## 16.7 工业案例与基准数据

### 16.7.1 案例 1: 字节跳动 CI/CD 优化

**初始状态**:
- 单次构建: 25min
- 日构建量: 5万次
- CI 集群: 5000+ runner

**优化路径**:

| 阶段 | 优化项 | 构建时长 | 收益 |
|------|-------|---------|------|
| 初始 | 无缓存 | 25min | - |
| 1 | BuildKit + cache mount | 12min | 2x |
| 2 | 远程缓存(registry) | 6min | 4x |
| 3 | 多阶段并行构建 | 4min | 6x |
| 4 | 增量构建(Bazel) | 2min | 12x |
| 5 | 预编译基础镜像 | 90s | 17x |

**关键创新**:
1. **远程缓存命中率 95%+**: 跨 runner 共享,新 commit 仅构建变更层
2. **增量构建**: Bazel 严格依赖图,仅构建受影响目标
3. **预编译 base**: 通用依赖打包到 base image,业务层仅构建代码
4. **按需 runner**: 自动扩缩,高峰 5000,低谷 500

### 16.7.2 案例 2: Netflix Spinnaker 金丝雀

**场景**: Netflix 每天部署 4000+ 次,99% 通过金丝雀验证。

**架构**:

```
代码合并
   │
   ▼
CI 构建镜像
   │
   ▼
Spinnaker 编排
   ├─ Bake: 烘焙 AMI/镜像
   ├─ Deploy: 部署到集群
   ├─ Stage: 1% 流量验证
   ├─ Canary: 5% → 25% → 50%
   └─ Production: 100%
       │
       ▼
   Kayenta(自动分析)
       ├─ 指标对比(v1 vs v2)
       ├─ 异常检测(ML)
       └─ 自动回滚(异常)
```

**关键指标**:
- 部署成功率: 99.5%(金丝雀拦截 80% 问题)
- 平均部署时长: 12min
- 自动回滚率: 0.5%

### 16.7.3 案例 3: 阿里 Aone 流水线

**规模**:
- 日均构建: 10万次
- 应用数: 1万+
- 流水线模板: 200+

**特色**:
1. **流水线模板化**: 不同业务类型(Java/Go/前端)有标准模板
2. **环境隔离**: dev/staging/prod 物理隔离
3. **灰度发布**: 全部生产部署走金丝雀
4. **审计合规**: 所有变更可追溯到 PR + 人

### 16.7.4 性能基准对比

**构建时长对比**(同一 Go 项目,10000 行代码):

| 方案 | 首次 | 二次(缓存) | 远程缓存 |
|------|------|------------|---------|
| docker build | 8min | 4min | - |
| BuildKit | 6min | 90s | 30s |
| BuildKit + 多阶段 | 5min | 30s | 15s |
| Bazel + Docker | 4min | 10s | 5s |

**部署策略对比**(100 副本滚动更新):

| 策略 | 总耗时 | 流量影响 | 回滚时长 |
|------|-------|---------|---------|
| 滚动(默认) | 8min | 短暂混合 | 8min |
| 蓝绿 | 30s(切换) | 0 | 30s |
| 金丝雀(5%) | 30min | 5%-100% | 30s |
| A/B(按用户) | 不限 | 部分 | 30s |

**GitOps vs 传统 Push**:

| 维度 | Push (Jenkins) | Pull (ArgoCD) |
|------|---------------|---------------|
| 凭证管理 | CI 持 K8s 凭证 | K8s 内部署 |
| 状态同步 | 单向 | 双向(自动协调) |
| 审计 | CI 日志 | Git 历史 |
| 回滚 | 重新跑 CI | git revert |
| 多集群 | 复杂 | 简单(联邦) |

### 16.7.5 大厂 CI/CD 工具选型

| 厂商 | CI 工具 | CD 工具 | 编排 |
|------|--------|--------|------|
| 阿里 | Aone | Aone | 自研 |
| 字节 | 自研 + Bazel | 自研 + ArgoCD | ArgoCD |
| 腾讯 | Coding | Coding | 自研 |
| Netflix | Jenkins | Spinnaker | Spinnaker |
| Google | Bazel + Buildbuddy | Bazel + 内部 | 内部 |
| Uber | BuildKite | 自研 | Peloton |
| Microsoft | Azure DevOps | Azure DevOps | AKS |

---

## 16.8 故障复盘

### 16.8.1 故障 1: 镜像 tag 重复导致部署混乱

**背景**: 2024-02,某公司生产部署了错误版本,排查发现 tag 复用。

**现象**:
- 部署 myapp:v1.2.3
- 不同环境行为不一致
- registry 中 v1.2.3 既有新镜像又有老镜像

**根因**:
```bash
# 错误: 同一 tag 覆盖
docker build -t myapp:v1.2.3 .   # 第一次构建
docker build -t myapp:v1.2.3 .   # 第二次构建(覆盖)
```

虽然 tag 都是 v1.2.3,但 digest 不同。部分节点拉到旧版,部分拉到新版。

**修复过程**:
1. **临时**: 强制删除并重新推送
2. **彻底**: 改用不可变 tag
   ```bash
   # 用 git commit hash 作为 tag(唯一)
   docker build -t myapp:sha-abc1234 .
   # 语义版本仅作 alias
   docker tag myapp:sha-abc1234 myapp:v1.2.3
   ```
3. **registry 强制不可变**:
   ```yaml
   # Harbor 配置
   immutable_tag: "v*,sha-*"
   ```

**预防措施**:
- **生产用 immutable tag**(sha-xxx)
- **registry 启用不可变规则**(防覆盖)
- **部署清单记录 digest**(不仅 tag)
- **Cosign 签名基于 digest**(防篡改)

### 16.8.2 故障 2: 流水线缓存污染

**背景**: 2024-03,某公司 CI 缓存被污染,导致所有构建产物含恶意代码。

**现象**:
- 多个不相关项目构建后含相同后门
- 源码无问题,但镜像有后门
- 调查发现是缓存层被植入

**根因**:
- BuildKit cache mount 共享给所有项目
- 攻击者通过一个项目植入恶意 .so 到缓存
- 其他项目构建时挂载该缓存,链接了恶意库

**修复过程**:
1. **紧急清理**: 删除所有缓存,重新构建
2. **缓存隔离**: 不同项目用独立 cache mount
   ```dockerfile
   RUN --mount=type=cache,target=/root/.cache,uid=1000,gid=1000,sharing=locked
   ```
3. **签名验证**: 所有依赖签名验证(防篡改)
4. **构建审计**: 记录每次构建的缓存来源

**预防措施**:
- **缓存隔离**(P0): 项目间不共享
- **依赖签名**: pip/npm/go mod 验证签名
- **SBOM 比对**: 与上次构建对比,异常变化告警
- **可信构建节点**: runner 不混用敏感/非敏感项目

### 16.8.3 故障 3: 滚动更新中断流量

**背景**: 2024-04,某公司滚动更新期间 5% 请求失败,持续 2min。

**现象**:
- 部署期间部分请求 502
- 应用日志显示优雅退出失败
- K8s events 显示 Pod 被强杀

**根因**:
```yaml
# K8s 配置缺失
spec:
  terminationGracePeriodSeconds: 30  # 默认 30s
  # ❌ 没有 preStop hook
  # ❌ 没有 readiness 探针延迟
```

Pod 收到 SIGTERM → 立即从 Endpoints 移除 → 但仍有正在处理的请求 → 30s 后强杀 → 请求失败。

**修复过程**:
1. **加 preStop hook**: 给应用留时间处理完请求
   ```yaml
   lifecycle:
     preStop:
       exec:
         command: ["sh", "-c", "sleep 10"]
   ```
2. **应用优雅退出**:
   ```python
   def handle_sigterm(signum, frame):
       server.stop_receiving_new_requests()
       server.wait_for_pending_requests(timeout=20)
       sys.exit(0)
   ```
3. **readiness 探针**:
   ```yaml
   readinessProbe:
     httpGet:
       path: /ready
       port: 8080
     periodSeconds: 5
   ```

**完整优雅退出流程**:
```
1. K8s 发送 SIGTERM
2. Pod 标记为 Terminating
3. preStop hook 执行(sleep 10s)
4. Endpoints 移除该 Pod(不再接新流量)
5. 应用处理完存量请求
6. 应用主动退出(SIGTERM 后 20s 内)
7. K8s 发送 SIGKILL(超时)
```

**预防措施**:
- **必须有 preStop hook**(P0)
- **应用实现优雅退出**(SIGTERM handler)
- **terminationGracePeriodSeconds 足够长**(>应用最大请求时长)
- **readinessProbe 配合**(快速移出流量)

### 16.8.4 故障 4: 镜像扫描误报阻断 CI

**背景**: 2024-05,某公司所有 CI 构建失败,Trivy 报告 HIGH CVE。

**现象**:
- 应用本身无问题
- 但 base image(ubuntu 22.04) 含 5 个 HIGH CVE
- CI 配置 `exit-code: 1` 阻断所有构建

**根因**:
- Ubuntu 22.04 最新补丁未及时更新
- Trivy 数据库已包含这些 CVE
- CI 严格策略导致全量阻断

**修复过程**:
1. **临时**: 临时降级策略(仅 CRITICAL 阻断)
2. **彻底**: 升级 base image 到最新补丁版本
3. **建立白名单**: 已知不影响业务的 CVE 加白
   ```yaml
   trivy:
     ignore-unfixed: true
     ignore-policy: ignore.rego
   ```
4. ** CVE 响应流程**:
   - CRITICAL: 24h 内修复
   - HIGH: 7 天内修复
   - MEDIUM: 30 天内修复

**预防措施**:
- **CVE 分级响应**(P0): 不是一刀切
- **白名单管理**: 已确认不影响的 CVE
- **base image 定期更新**: 每月升级
- **Trivy 数据库定时同步**: 每日更新

### 16.8.5 故障 5: ArgoCD 同步失败导致生产不一致

**背景**: 2024-06,某公司生产环境与 Git 不一致,ArgoCD 未检测到。

**现象**:
- Git 中 myapp 副本数 = 3
- 生产实际副本数 = 5(有人手动 kubectl scale)
- ArgoCD 显示 Synced(实际不一致)

**根因**:
- ArgoCD 配置 `ignoreDifferences` 忽略了 replicas
- 没有开启 selfHeal
- 缺乏漂移检测告警

**修复过程**:
1. **清理 ignoreDifferences**: 不忽略关键字段
2. **开启 selfHeal**: 自动同步
   ```yaml
   syncPolicy:
     automated:
       prune: true
       selfHeal: true
   ```
3. **漂移检测告警**:
   ```yaml
   - alert: ArgoCDDriftDetected
     expr: |
       argocd_app_info{sync_status!="Synced"} > 0
     for: 5m
     labels:
       severity: warning
   ```
4. **禁止手动 kubectl**: RBAC 限制直接操作生产

**预防措施**:
- **GitOps 严格化**(P0): Git 是唯一真相源
- **selfHeal 开启**: 自动纠正漂移
- **漂移告警**: 任何不一致立即告警
- **RBAC 限制**: 禁止手动 kubectl 改生产
- **审计日志**: 记录所有 K8s 操作

---

## 16.9 最佳实践

### 16.9.1 CI/CD 流水线设计原则

**1. 快速失败**
- Lint 在前(秒级)
- 单元测试在前(分钟级)
- 集成测试在后(数分钟)
- 安全扫描可与部署并行

**2. 失败隔离**
- 单个 job 失败不影响其他独立 job
- 必要时重试机制(瞬时故障)

**3. 可重现**
- 同一 commit 同一镜像 digest
- 锁版本(基础镜像 / 依赖 / 工具)

**4. 可观测**
- 每步有日志
- 关键指标(构建时长/成功率)
- 失败自动通知

**5. 安全**
- 凭证用 secret,不硬编码
- 最小权限(CI 仅有需要的作用域)
- 镜像签名 + 准入控制

### 16.9.2 部署策略选型

```
应用类型 → 推荐策略

无状态微服务 → 滚动更新(默认)
  ↓ 高可用需求
  → 蓝绿(零中断)

有状态应用 → 金丝雀(逐步验证)
  ↓ 数据兼容性敏感
  → 双写双读 + 金丝雀

关键业务 → 金丝雀 + 自动回滚
  ↓ 极高要求
  → 影子部署(先镜像流量)

数据库 → 不用 K8s 滚动,用工具(Flyway/Liquibase)
```

### 16.9.3 GitOps 实践

**5 原则**:
1. **Git 是唯一真相源**: 任何环境变更走 PR
2. **声明式**: 全部用 YAML,不用命令式
3. **自动同步**: ArgoCD/Flux 持续协调
4. **环境隔离**: 不同环境不同目录/分支
5. **可审计**: Git history 即部署历史

**仓库结构**:

```
myapp-deploy/
├── base/                  # 公共基础
├── overlays/
│   ├── dev/               # 开发
│   ├── staging/           # 预发
│   └── prod/              # 生产
├── .github/workflows/     # PR 校验
└── README.md              # 部署规范
```

### 16.9.4 镜像生命周期管理

```
构建 → 推送 → 部署 → 老化 → 清理

策略:
- dev/staging 镜像: 7 天后清理
- prod 镜像: 保留 90 天(支持回滚)
- release 镜像: 永久保留(版本归档)

工具:
- Harbor 自动清理策略
- registry garbage collect
- 镜像 tag 自动化(SHA + 语义版本)
```

### 16.9.5 回滚策略

**3 级回滚**:

```
Level 1: 应用层回滚(秒级)
  - kubectl rollout undo
  - ArgoCD 同步到上一个 commit

Level 2: 镜像回滚(分钟级)
  - 切换 image tag
  - 重新部署

Level 3: 数据回滚(分钟~小时级)
  - 数据库备份恢复
  - 需谨慎(可能丢数据)
```

**回滚原则**:
- **优先应用层回滚**(不动数据)
- **数据回滚需评估**: 可能丢数据
- **回滚后必须复盘**: 找根因,防再犯

---

## 16.10 常见陷阱

### 16.10.1 陷阱 1: 构建上下文过大

**问题**:
```bash
$ docker build .
[+] Building 180s ... (50/50)
```

构建上下文 500MB,传输慢。

**解决**:
- 配置 .dockerignore(参考 [15 章](./15-Dockerfile生产模板.md))
- 用 `.dockerignore` 排除 node_modules / .git / dist 等

### 16.10.2 陷阱 2: CI 与本地构建不一致

**问题**: 本地构建成功,CI 失败。

**原因**:
- 本地有缓存,CI 全新构建
- 本地依赖版本与 CI 不一致
- CI runner 架构不同(arm64 vs amd64)

**解决**:
- CI 用锁文件(package-lock.json / Cargo.lock)
- 定期清理本地缓存,验证全新构建
- CI runner 与生产架构一致

### 16.10.3 陷阱 3: secret 进镜像

**问题**:
```dockerfile
ARG API_KEY=xxx   # ❌ 进 image history
RUN curl -H "Authorization: $API_KEY" ...
```

**解决**:
```dockerfile
# 用 secret mount
RUN --mount=type=secret,id=api_key \
    curl -H "Authorization: $(cat /run/secrets/api_key)" ...
```

```bash
docker build --secret id=api_key,env=API_KEY .
```

### 16.10.4 陷阱 4: 部署顺序错误

**问题**: 先部署新版本,数据库 schema 还没更新。

**解决**:
- **数据库先行**: 先迁移 schema(向后兼容)
- **代码兼容**: 新代码兼容老 schema
- **滚动部署**: 旧版本仍可用老 schema
- **清理旧 schema**: 全量升级后清理

### 16.10.5 陷阱 5: K8s manifest 不版本化

**问题**:
```bash
kubectl apply -f deployment.yaml   # ❌ 无版本控制
```

**解决**:
- 所有 manifest 进 Git
- 用 Kustomize/Helm 管理
- ArgoCD 自动同步

### 16.10.6 陷阱 6: 健康检查太严

**问题**:
```yaml
livenessProbe:
  httpGet:
    path: /health
  periodSeconds: 5
  failureThreshold: 1   # ❌ 一次失败就重启
```

瞬时抖动导致 Pod 无限重启。

**解决**:
```yaml
livenessProbe:
  httpGet:
    path: /health
  periodSeconds: 10
  failureThreshold: 3   # 3 次失败才重启(30s)
  timeoutSeconds: 3
```

---

## 16.11 面试题

### Q1: CI 和 CD 区别?你的项目怎么做的?

**答**:
- **CI(持续集成)**: 代码合并前自动测试+构建,目标早发现问题
- **CD(持续交付/部署)**: 自动部署到环境,目标快速交付

**项目实践**:
- CI: GitHub Actions,PR 触发 lint+test+build+scan
- CD: ArgoCD GitOps,main 分支自动同步 staging,tag 自动同步 prod
- 部署策略: 金丝雀(5%→25%→50%→100%)
- 回滚: ArgoCD 自动回滚(指标异常)

### Q2: Docker 在 CI/CD 中解决什么问题?

**答**:
1. **环境一致性**: 本地=CI=生产,无"在我机器上能跑"
2. **依赖隔离**: 不同项目不冲突
3. **可重现构建**: 同输入同输出
4. **标准化交付**: 镜像即制品
5. **不可变部署**: 替换而非修改,易回滚
6. **快速回滚**: 切换 tag 即可

### Q3: GitOps 与传统 CI/CD 区别?

**答**:
| 维度 | 传统 Push | GitOps Pull |
|------|---------|------------|
| 模型 | CI push 到 K8s | ArgoCD pull Git |
| 凭证 | CI 持 K8s 凭证 | K8s 内部署 |
| 真相源 | CI 配置 + K8s 状态 | Git |
| 审计 | CI 日志 | Git history |
| 回滚 | 重新跑 CI | git revert |
| 多集群 | 复杂 | 简单(联邦) |

GitOps 优势: 安全(无外部凭证)、可审计、易回滚、多集群友好。

### Q4: 怎么优化镜像构建速度?

**答**:
1. **多阶段构建**: 分离构建与运行
2. **BuildKit cache mount**: 跨构建共享缓存
3. **远程缓存(registry)**: 跨机器共享
4. **依赖文件前置**: 缓存友好
5. **.dockerignore**: 减少上下文
6. **预编译基础镜像**: 通用依赖打包
7. **多阶段并行**: 独立阶段并行构建
8. **增量构建**: Bazel 严格依赖图

实测: 25min → 90s,17 倍提升。

### Q5: 蓝绿、金丝雀、滚动更新区别?

**答**:
| 策略 | 切换 | 资源 | 回滚 | 风险 |
|------|------|-----|------|------|
| 滚动 | 逐步 | 1x | 慢 | 中 |
| 蓝绿 | 切换 | 2x | 快 | 低 |
| 金丝雀 | 渐进 | 1.1-2x | 快 | 极低 |

- **滚动**: 默认,适合无状态服务
- **蓝绿**: 零中断,适合关键业务
- **金丝雀**: 风险最低,适合重大变更

### Q6: 怎么实现镜像签名验证?

**答**:
1. **签名**: Cosign 签名镜像
   ```bash
   cosign sign --key cosign.key myapp:v1
   ```
2. **验证**: K8s 准入控制
   ```yaml
   # Kyverno 策略
   verifyImages:
   - imageReferences: ["myorg/*"]
     attestors:
     - entries:
       - keys:
           publicKeys: |
             -----BEGIN PUBLIC KEY-----
             ...
   ```
3. **SBOM**: 软件物料清单,证明依赖
4. **SLSA**: 供应链等级框架

### Q7: K8s 滚动更新中怎么避免流量中断?

**答**:
1. **maxUnavailable=0**: 永不减少可用副本
2. **readinessProbe**: 新 Pod ready 才加入
3. **preStop hook**: 给旧 Pod 时间处理存量请求
   ```yaml
   lifecycle:
     preStop:
       exec:
         command: ["sh", "-c", "sleep 10"]
   ```
4. **应用优雅退出**: 处理 SIGTERM
5. **terminationGracePeriodSeconds**: 留足退出时间

### Q8: 怎么实现自动回滚?

**答**:
1. **K8s 原生**:
   ```bash
   kubectl rollout status deployment/myapp --timeout=10m || \
   kubectl rollout undo deployment/myapp
   ```
2. **ArgoCD**:
   - sync 失败自动回滚
   - 配合 AnalysisTemplate 指标分析
3. **Argo Rollouts**:
   - 金丝雀自动分析
   - 失败自动回滚
4. **Prometheus**:
   - 告警触发 webhook
   - 自动执行回滚脚本

### Q9: 多平台构建怎么实现?

**答**:
```bash
# 1. 创建 multi-platform builder
docker buildx create --name multi --use

# 2. 启用 QEMU
docker run --privileged --rm tonistiigi/binfmt --install all

# 3. 构建多平台
docker buildx build \
  --platform linux/amd64,linux/arm64 \
  -t myapp:v1 \
  --push .
```

**性能优化**:
- 用原生节点(amd64 + arm64 runner),避免 QEMU 模拟
- BuildKit 并行构建不同平台

### Q10: 流水线缓存怎么管理?

**答**:
3 层缓存:
1. **本地层缓存**: BuildKit layer cache
2. **cache mount**: `--mount=type=cache,target=...`
3. **远程缓存**: `--cache-from/to type=registry`

**注意**:
- 缓存隔离(不同项目不共享)
- 缓存清理(避免膨胀)
- 缓存安全(防污染)

---

## 16.12 总结

### 16.12.1 核心要点

1. **Docker 是 CI/CD 标准交付物**: 环境一致、可重现、易回滚
2. **流水线 7 阶段**: lint → test → build → scan → sign → push → deploy
3. **构建优化**: 多阶段 + BuildKit + 远程缓存,25min→90s
4. **部署策略**: 滚动(默认)/蓝绿(零中断)/金丝雀(最低风险)
5. **GitOps**: Git 唯一真相源,ArgoCD 自动同步
6. **供应链安全**: SBOM + Cosign 签名 + Kyverno 准入
7. **自动回滚**: 指标异常自动回滚,MTTR < 5min

### 16.12.2 工业实践要点

1. **字节**: Bazel + 远程缓存,17x 加速
2. **Netflix**: Spinnaker + Kayenta,99.5% 部署成功率
3. **阿里**: Aone 模板化,日均 10万构建
4. **Google**: Bazel 严格依赖图,增量构建
5. **GitOps 标准化**: ArgoCD 占大厂 80% 份额

### 16.12.3 选型决策

```
CI 工具:
├─ 开源 / GitHub 项目 → GitHub Actions
├─ 自建 GitLab → GitLab CI
├─ 老牌企业 → Jenkins
└─ K8s 原生 → Tekton

CD 工具:
├─ GitOps(推荐) → ArgoCD / Flux
├─ 复杂编排 → Spinnaker
├─ 简单部署 → kubectl + Helm
└─ 多云 → ArgoCD 联邦

部署策略:
├─ 无状态服务 → 滚动
├─ 关键业务 → 蓝绿
├─ 重大变更 → 金丝雀
└─ 数据库 → 不用 K8s 滚动
```

### 16.12.4 与其他章节联系

- **[03-镜像原理与Dockerfile](./03-镜像原理与Dockerfile.md)**: 镜像构建基础
- **[13-镜像仓库与分发](./13-镜像仓库与分发.md)**: registry 与签名
- **[14-监控与日志](./14-监控与日志.md)**: 部署后监控告警
- **[15-Dockerfile生产模板](./15-Dockerfile生产模板.md)**: 模板在 CI 中应用
- **[17-生产实践](./17-生产实践.md)**: 综合生产实践

---

## 16.13 参考资料

### 官方文档
- [GitHub Actions](https://docs.github.com/actions)
- [GitLab CI/CD](https://docs.gitlab.com/ee/ci/)
- [ArgoCD](https://argo-cd.readthedocs.io/)
- [Argo Rollouts](https://argo-rollouts.readthedocs.io/)
- [Flux](https://fluxcd.io/)

### 工具与项目
- [BuildKit](https://github.com/moby/buildkit)
- [buildx](https://github.com/docker/buildx)
- [Cosign](https://github.com/sigstore/cosign)
- [Kyverno](https://kyverno.io/)
- [OPA Gatekeeper](https://github.com/open-policy-agent/gatekeeper)
- [Trivy](https://github.com/aquasec/trivy)
- [Syft](https://github.com/anchore/syft)
- [Spinnaker](https://spinnaker.io/)
- [Tekton](https://tekton.dev/)
- [Kustomize](https://kustomize.io/)
- [Helm](https://helm.sh/)

### 工业实践
- [Netflix: Spinnaker](https://netflixtechblog.com/global-continuous-delivery-with-spinnaker-2e68c2df7d81)
- [Netflix: Automated Canary Analysis](https://netflixtechblog.com/automated-canary-analysis-at-netflix-with-kayenta-3260bc7acc69)
- [字节跳动: Bazel 实践](https://bytedance.feishu.cn/docs/)
- [Google: Bazel](https://bazel.build/)
- [Uber: CI/CD at Scale](https://www.uber.com/blog/cicd-at-uber-scale/)
- [阿里 Aone 流水线](https://developer.aliyun.com/article/782016)

### 标准与规范
- [SLSA Framework](https://slsa.dev/)
- [SPDX SBOM](https://spdx.dev/)
- [NIST Supply Chain](https://csrc.nist.gov/projects/supply-chain-risk-management)
- [GitOps Principles](https://opengitops.dev/)

### 学习资源
- [ArgoCD Examples](https://github.com/argoproj/argocd-example-apps)
- [GitOps Handbook](https://github.com/open-gitops/documents)
- [CNCF CI/CD Landscape](https://landscape.cncf.io/card-mode?category=continuous-integration-delivery)

---

> 下一章: [17-生产实践](./17-生产实践.md) - 综合生产实践与运维经验
