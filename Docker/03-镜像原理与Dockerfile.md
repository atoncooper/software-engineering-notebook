# 03 - 镜像原理与 Dockerfile

> Docker 的核心概念是镜像。理解分层、UnionFS、构建缓存,才能写出工业级的 Dockerfile。

---

## 1. 问题定义与边界

### 1.1 本章解决什么

- 把"镜像"这个概念讲透:为什么分层、怎么分层、分层带来什么好处
- 把 `Dockerfile` 每条指令的副作用说清:哪条会新建层、哪条会改变元数据、哪条会被缓存
- 把 **多阶段构建** 讲到能工业落地:为什么必须用、怎么写、坑在哪
- 把 **镜像瘦身** 讲到可量化:从 1.2 GB → 80 MB 的路径
- 把 **构建缓存** 讲到能调优:CI 里怎么保住缓存

### 1.2 本章不解决什么

- 不讲底层 UnionFS 内核实现(见 [10-底层原理-UnionFS](./10-底层原理-UnionFS.md))
- 不讲容器运行时参数(见 [04-容器运行与生命周期](./04-容器运行与生命周期.md))
- 不讲镜像签名与安全(见 [12-安全与隔离](./12-安全与隔离.md) / [24-工业实战-供应链安全](./24-工业实战-供应链安全.md))
- 不讲各语言的具体 Dockerfile 模板(见 [15-Dockerfile实战模板](./15-Dockerfile实战模板.md))

> **关键认知**:镜像是 Docker 的"可执行文件"。一个 1 GB 的镜像和一个 50 MB 的镜像,功能可以完全一样——区别在工程师对分层的理解。

---

## 2. 直觉解释

### 2.1 镜像分层类比:洋葱

```
   完整视图              实际分层(从下到上叠加)
┌────────────┐         ┌────────────────────┐ ← 容器层(R/W)
│  /app      │         │ CMD ["python", ...]│   docker run 时加
│  /usr/bin  │         ├────────────────────┤
│  /etc      │         │ COPY app.py /app   │   ← 第 5 层
│  /lib      │         ├────────────────────┤
│  ...       │         │ RUN pip install -r │   ← 第 4 层
└────────────┘         ├────────────────────┤
                       │ FROM python:3.12   │   ← 第 1 层(基础)
                       └────────────────────┘
                              ↓ overlay2
                       统一文件系统视图
```

**类比**:镜像像一颗洋葱,每条 Dockerfile 指令加一层。运行时把这些层叠加,从外面看是一个完整文件系统。

### 2.2 为什么分层?——共享与复用

```
镜像 A:nginx + app1       镜像 B:nginx + app2
┌──────────────┐          ┌──────────────┐
│  app1        │          │  app2        │  ← 不同
├──────────────┤          ├──────────────┤
│  nginx       │          │  nginx       │  ← 共享(磁盘只存一份)
├──────────────┤          ├──────────────┤
│  debian      │          │  debian      │  ← 共享
└──────────────┘          └──────────────┘
       磁盘占用:debian + nginx + app1 + app2
       而非 2 × (debian + nginx + app)
```

1000 个基于 `python:3.12-slim` 的镜像,磁盘上 `python:3.12-slim` 只存一份,节省数 GB × 1000。

### 2.3 写时复制(CoW)

容器启动时,在最上面加一个可读写层。读文件时从下层找;**写文件时,先把文件从下层复制到容器层,再修改**。

```
   镜像(只读)                容器(可读写)
┌──────────────┐          ┌──────────────┐
│  /etc/nginx  │          │ /etc/nginx   │ ← 修改时复制上来
│  (原始)      │          │ (副本)       │
└──────────────┘          └──────────────┘
       ↓ 只读                    ↑ 写时复制(CoW)
```

> **关键推论**:大文件修改(如日志写 1 GB)会触发整个文件复制到容器层,磁盘占用翻倍。日志应写卷或 stdout。

---

## 3. 核心概念与架构

### 3.1 镜像在磁盘上的形态

```
/var/lib/docker/overlay2/
├── <layer-hash-1>/        # 镜像层 1
│   ├── diff/              # 该层文件
│   ├── work/
│   └── committed
├── <layer-hash-2>/        # 镜像层 2
│   ├── diff/
│   └── ...
└── <container-hash>/      # 容器层
    ├── diff/
    ├── work/
    ├── lowerdir           # 指向下层(链式)
    ├── merged/            # overlay 挂载点(统一视图)
    └── ...
```

### 3.2 overlay2 工作原理

```
                 ┌─────────────┐
                 │  应用视角    │
                 │  /usr/bin/  │
                 │  /app       │
                 │  /etc/...   │
                 └──────┬──────┘
                        │  overlay mount
              ┌─────────┴─────────┐
              │                    │
              ▼                    ▼
   ┌──────────────────┐  ┌──────────────────┐
   │  upperdir (R/W)  │  │  lowerdir (R/O)  │
   │  容器层           │  │  镜像层(可多个)  │
   │  /var/lib/docker/│  │  /var/lib/docker/│
   │  overlay2/<id>/  │  │  overlay2/<id>/  │
   │  diff/           │  │  diff/           │
   └──────────────────┘  └──────────────────┘
                                    │
                                    ▼
                        ┌──────────────────┐
                        │  base image       │
                        │  diff/            │
                        └──────────────────┘
```

- **lowerdir**:只读,镜像各层(从下到上叠加)
- **upperdir**:可读写,容器层
- **workdir**:overlay 内部工作目录
- **merged**:统一视图,容器内看到的文件系统

> 详细内核机制见 [10-底层原理-UnionFS](./10-底层原理-UnionFS.md)。

### 3.3 镜像元数据:image manifest 与 config

一个镜像不只是文件层,还有元数据(存在 registry 里):

```json
// manifest(清单:有哪些层)
{
  "schemaVersion": 2,
  "mediaType": "application/vnd.docker.distribution.manifest.v2+json",
  "config": {
    "mediaType": "application/vnd.docker.container.image.v1+json",
    "digest": "sha256:a1b2c3...",
    "size": 7023
  },
  "layers": [
    {"digest": "sha256:layer1...", "size": 31337184},
    {"digest": "sha256:layer2...", "size": 8255831},
    {"digest": "sha256:layer3...", "size": 261}
  ]
}
```

```json
// config(配置:环境变量、CMD、入口点、分层历史)
{
  "architecture": "amd64",
  "os": "linux",
  "config": {
    "Env": ["PATH=/usr/local/bin:/usr/bin:/bin", "PYTHON_VERSION=3.12.0"],
    "Cmd": ["python3"],
    "Entrypoint": ["docker-entrypoint.sh"],
    "WorkingDir": "/app"
  },
  "history": [
    {"created": "2024-01-01T00:00:00Z", "created_by": "ADD file:... in /"},
    {"created": "2024-01-01T00:00:01Z", "created_by": "RUN apt-get update"}
  ],
  "rootfs": {
    "type": "layers",
    "diff_ids": ["sha256:...", "sha256:..."]
  }
}
```

> **关键**:同一个 image 可以有不同 tag(`nginx:1.25` / `nginx:latest`),tag 只是 manifest 的别名。**digest(sha256) 才是镜像的唯一身份**。

### 3.4 Dockerfile 指令全集

| 指令 | 作用 | 是否新建层 | 改变元数据 |
|------|------|-----------|-----------|
| `FROM` | 基础镜像 | 是(继承) | - |
| `LABEL` | 元数据标签 | 否 | 是 |
| `MAINTAINER` | 维护者(已废弃,用 LABEL) | 否 | 是 |
| `ENV` | 环境变量 | 否 | 是 |
| `ARG` | 构建参数(仅构建期) | 否 | 否 |
| `WORKDIR` | 工作目录 | 否 | 是 |
| `USER` | 切换用户 | 否 | 是 |
| `COPY` | 拷贝文件进来 | 是 | - |
| `ADD` | 拷贝 + 解压 + URL(慎用) | 是 | - |
| `RUN` | 执行命令 | 是 | - |
| `CMD` | 默认命令(可被 run 覆盖) | 否 | 是 |
| `ENTRYPOINT` | 入口点(不易覆盖) | 否 | 是 |
| `EXPOSE` | 声明端口(仅文档) | 否 | 是 |
| `VOLUME` | 声明卷点 | 否 | 是 |
| `HEALTHCHECK` | 健康检查命令 | 否 | 是 |
| `STOPSIGNAL` | 停止信号 | 否 | 是 |
| `SHELL` | 默认 shell | 否 | 是 |
| `ONBUILD` | 触发器(子镜像构建时执行) | 否 | 是 |
| `HEALTHCHECK` | 健康检查 | 否 | 是 |

---

## 4. 操作流程与命令

### 4.1 一个典型的 Dockerfile

```dockerfile
# syntax=docker/dockerfile:1.6

FROM python:3.12-slim AS base

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PIP_NO_CACHE_DIR=1 \
    PIP_DISABLE_PIP_VERSION_CHECK=1

WORKDIR /app

# 依赖单独一层(利用缓存)
COPY requirements.txt .
RUN pip install -r requirements.txt

# 应用代码
COPY . .

# 安全:非 root
RUN useradd -m -u 10001 app && chown -R app:app /app
USER app

HEALTHCHECK --interval=30s --timeout=3s --retries=3 \
  CMD python -c "import urllib.request; urllib.request.urlopen('http://localhost:8000/health')"

EXPOSE 8000
CMD ["uvicorn", "main:app", "--host", "0.0.0.0", "--port", "8000"]
```

### 4.2 构建命令

```bash
# 基础构建
docker build -t myapp:v1 .

# 带构建参数
docker build --build-arg VERSION=1.2.3 -t myapp:v1.2.3 .

# 多架构(buildx)
docker buildx build --platform linux/amd64,linux/arm64 -t myapp:v1 --push .

# 不使用缓存
docker build --no-cache -t myapp:v1 .

# 指定 Dockerfile 路径
docker build -f docker/Dockerfile.prod -t myapp:v1 .

# 进度信息
docker build --progress=plain -t myapp:v1 .
```

### 4.3 查看镜像信息

```bash
# 列出镜像
docker image ls

# 含 SHA256
docker image ls --digests

# 镜像分层历史
docker history myapp:v1
# IMAGE          CREATED       CREATED BY                                      SIZE
# a1b2c3d4e5f6   2 minutes ago CMD ["uvicorn" "main:app" ...]                   0B
# b2c3d4e5f6a1   2 minutes ago USER app                                        0B
# ...

# 镜像元数据
docker image inspect myapp:v1

# 镜像大小
docker image ls myapp:v1
```

---

## 5. 底层原理(简略)

### 5.1 构建缓存的判定逻辑

```
当前指令 + 上层所有指令的 hash ──> cache key
                                  ↓
                          cache 命中?
                         /          \
                       是            否
                       ↓             ↓
                  用缓存层        重新执行
                                  并新建层
                       ↓
                  后续指令全部
                  必然 cache miss
```

**关键规则**:
- `COPY` / `ADD`:根据源文件内容 hash 判断(内容变就 miss)
- `RUN`:根据命令字符串判断(命令变就 miss)
- `FROM`:根据基础镜像 digest 判断
- 一旦 miss,后续全部 miss(因为 hash 链断了)

### 5.2 BuildKit 的优化

BuildKit(Docker 18.09+ 引入,23.0+ 默认)比旧版 builder 强:

- **并行构建**:多阶段构建中,无依赖的阶段并行
- **缓存后端**:可推送到 registry / 本地目录,跨机器共享
- **懒加载**:只下载需要的层
- **更好的输出**:progress plain / auto / tty
- **前端独立**:`# syntax=docker/dockerfile:1.6` 可用新语法

```bash
# 启用 BuildKit
DOCKER_BUILDKIT=1 docker build -t myapp:v1 .

# 缓存导出到本地
docker build --cache-to=type=local,dest=/tmp/cache -t myapp:v1 .

# 缓存从 registry 加载
docker build --cache-from=type=registry,ref=myrepo/myapp:cache -t myapp:v1 .
```

---

## 6. 代码与配置示例

### 6.1 多阶段构建:Go 项目从 1.2 GB → 20 MB

```dockerfile
# 阶段 1:构建
FROM golang:1.22-alpine AS builder

WORKDIR /src
COPY go.mod go.sum ./
RUN go mod download

COPY . .
RUN CGO_ENABLED=0 GOOS=linux go build \
    -ldflags="-s -w -X main.version=$(git rev-parse --short HEAD)" \
    -o /app/server ./cmd/server

# 阶段 2:运行
FROM gcr.io/distroless/static-debian12:nonroot

COPY --from=builder /app/server /server
COPY --from=builder /src/configs /configs

USER nonroot:nonroot
EXPOSE 8080
ENTRYPOINT ["/server"]
```

**效果**:
| 阶段 | 镜像大小 |
|------|----------|
| 单阶段(golang:1.22 + 源码) | 1.2 GB |
| 多阶段 + alpine | 25 MB |
| 多阶段 + distroless | 20 MB |
| 多阶段 + scratch + 静态二进制 | 15 MB |

### 6.2 Python 多阶段:利用缓存层

```dockerfile
FROM python:3.12-slim AS builder

WORKDIR /app
RUN pip install --user uv

COPY pyproject.toml uv.lock ./
RUN uv pip install --system --no-cache -r pyproject.toml

FROM python:3.12-slim

COPY --from=builder /usr/local/lib/python3.12/site-packages /usr/local/lib/python3.12/site-packages
COPY --from=builder /usr/local/bin /usr/local/bin

WORKDIR /app
COPY . .

RUN useradd -m -u 10001 app && chown -R app:app /app
USER app

CMD ["uvicorn", "main:app", "--host", "0.0.0.0", "--port", "8000"]
```

### 6.3 Java:分层 JAR 优化启动

```dockerfile
FROM eclipse-temurin:17-jre-jammy

WORKDIR /app

# 分层 JAR(Spring Boot 2.3+)
COPY target/app.jar app.jar
RUN java -Djarmode=layertools -jar app.jar extract

COPY dependencies/ ./
COPY spring-boot-loader/ ./
COPY snapshot-dependencies/ ./
COPY application/ ./

USER 10001:10001
ENTRYPOINT ["java", "org.springframework.boot.loader.launch.JarLauncher"]
```

> **关键**:Spring Boot 分层后,依赖层不变就不重新下载,CI 缓存命中率从 30% → 90%。

---

## 7. 常见陷阱与调优

### 7.1 陷阱:`latest` 基础镜像

```dockerfile
FROM python:latest          # ❌ 不可重现
FROM python:3.12-slim       # ✓ 锁定版本
FROM python:3.12.3-slim@sha256:abc123...  # ✓ 锁定 digest(最严格)
```

### 7.2 陷阱:`RUN apt-get` 不清理

```dockerfile
# ❌ 会留 apt 缓存,层变大 50 MB
RUN apt-get update && apt-get install -y curl

# ✓ 一条命令装 + 清理
RUN apt-get update \
    && apt-get install -y --no-install-recommends curl \
    && rm -rf /var/lib/apt/lists/*
```

### 7.3 陷阱:频繁变化的层放前面

```dockerfile
# ❌ 代码天天变,导致后面 pip install 缓存失效
COPY . /app
RUN pip install -r requirements.txt   # 每次都重装

# ✓ requirements.txt 很少变,放前面
COPY requirements.txt .
RUN pip install -r requirements.txt    # 缓存命中
COPY . /app                            # 代码变不影响上层
```

### 7.4 陷阱:`ADD` 而非 `COPY`

```dockerfile
ADD http://example.com/file.tar.gz /tmp/   # ❌ 看不出来源
ADD file.tar.gz /tmp/                       # ❌ 自动解压,行为不直观
COPY file.tar.gz /tmp/                      # ✓ 只是拷贝
RUN tar xzf /tmp/file.tar.gz -C /tmp/       # 显式解压
```

> **规则**:能用 COPY 就不用 ADD。ADD 仅在需要自动解压 tar 时考虑。

### 7.5 陷阱:CMD 与 ENTRYPOINT 误用

```dockerfile
# ❌ shell 形式:PID 1 是 /bin/sh,不接收 SIGTERM
CMD python main.py
# 等价于 CMD ["/bin/sh", "-c", "python main.py"]

# ✓ exec 形式:PID 1 是 python,接收信号
CMD ["python", "main.py"]
```

详见 [04-容器运行与生命周期](./04-容器运行与生命周期.md) 的"PID 1 与信号处理"。

### 7.6 陷阱:把 secret 写进 Dockerfile

```dockerfile
# ❌ 密码会留在 image history 里,泄漏
ENV DB_PASSWORD=p@ssw0rd
RUN curl -u admin:secret https://internal/api

# ✓ 用 --secret(BuildKit)
RUN --mount=type=secret,id=db_password \
    curl -u admin:$(cat /run/secrets/db_password) https://internal/api

# ✓ 运行时注入
docker run -e DB_PASSWORD=xxx myapp
```

> 详见 [24-工业实战-供应链安全](./24-工业实战-供应链安全.md)。

### 7.7 陷阱:镜像层数过多

```dockerfile
# ❌ 7 层
RUN apt-get update
RUN apt-get install -y curl
RUN apt-get install -y git
RUN rm -rf /var/lib/apt/lists/*
RUN useradd app
RUN mkdir /app
RUN chown app:app /app

# ✓ 2 层
RUN apt-get update \
    && apt-get install -y --no-install-recommends curl git \
    && rm -rf /var/lib/apt/lists/* \
    && useradd app \
    && mkdir /app \
    && chown app:app /app
```

> **平衡**:层数过多影响启动(每层一次 mount)。但不是越少越好,合理分层便于缓存。建议 10-20 层。

### 7.8 陷阱:没有 `.dockerignore`

```
# .dockerignore
.git
.gitignore
node_modules
__pycache__
*.pyc
.env
.venv
.vscode
.idea
Dockerfile
docker-compose*.yml
README.md
tests/
*.md
```

**没 .dockerignore 的后果**:
- `COPY . .` 把 `.git`(可能几百 MB)、`node_modules` 都拷进去
- 镜像膨胀
- secret(`.env`)可能泄漏
- 构建上下文过大,build 慢

### 7.9 陷阱:root 用户运行

```dockerfile
# ❌ 默认 root,容器逃逸后即 root
# (不写 USER)

# ✓ 显式非 root
RUN useradd -m -u 10001 app
USER 10001:10001
```

> **工业基线**:所有生产镜像必须非 root。K8s 的 PSA restricted 策略强制。

---

## 8. 工业案例与基准数据

### 8.1 镜像大小与启动时间的关系

**测试条件**:同一 Python FastAPI 应用,不同基础镜像。

| 基础镜像 | 镜像大小 | 冷启动 | 内存占用 |
|----------|----------|--------|----------|
| `python:3.12` | 1.0 GB | 1.2 s | 45 MB |
| `python:3.12-slim` | 150 MB | 1.1 s | 45 MB |
| `python:3.12-alpine` | 55 MB | 1.8 s | 38 MB |
| 多阶段 + slim | 90 MB | 1.1 s | 45 MB |
| 多阶段 + distroless | 70 MB | 1.0 s | 42 MB |

**结论**:
- alpine 镜像小但启动慢(musl libc 不兼容,Python wheel 需重编译)
- **distroless + 多阶段** 是工业最优
- 不要无脑选 alpine,要看语言生态

### 8.2 阿里 / 字节的镜像优化实践

**阿里(公开演讲)**:
- 基础镜像统一 `alinux` + `tini`(30 MB)
- 多阶段构建 + BuildKit
- 镜像分层缓存:CI 共享缓存到 NAS
- 平均镜像大小从 800 MB → 120 MB
- 镜像拉取时间从 30s → 3s

**字节**:
- 自研 `basel` 远程缓存,跨 CI 节点共享
- 镜像按需加载(Nydus),启动 30s → 5s
- 镜像扫描(Trivy)+ 签名(Cosign)嵌入 CI

### 8.3 BuildKit 缓存的工业实践

**场景**:1000 个微服务,每天 10000 次构建,缓存命中率影响 CI 成本。

**方案**:
```bash
# CI 脚本
docker buildx build \
  --cache-from=type=registry,ref=corp.com/cache/myapp:cache \
  --cache-to=type=registry,ref=corp.com/cache/myapp:cache,mode=max \
  -t myapp:${CI_COMMIT_SHA} \
  --push .
```

**效果**(某厂实测):
| 方案 | CI 平均耗时 | 缓存命中率 | 月 CI 成本 |
|------|-------------|-----------|-----------|
| 无缓存 | 8 min | 0% | $5000 |
| 本地缓存 | 5 min | 60% | $3500 |
| Registry 缓存 | 3 min | 85% | $2200 |
| Registry + NAS | 2 min | 92% | $1800 |

### 8.4 多架构镜像的工业实践

**背景**:Apple Silicon(arm64)与 x86_64 共存,K8s 集群异构。

```bash
# 创建 builder
docker buildx create --name multiarch --use

# 构建并推送多架构镜像
docker buildx build \
  --platform linux/amd64,linux/arm64 \
  -t myrepo/myapp:v1 \
  --push .
```

**坑**:
- arm64 上跑 x86 镜像默认用 QEMU 模拟,慢 10 倍
- 应该用原生 arm64 构建机,或 GitHub Actions 的 multi-arch runner
- C/C++ 项目要注意交叉编译工具链

---

## 9. 与其他方案的关系

### 9.1 Dockerfile vs Buildah / Kaniko

| 维度 | Dockerfile | Buildah | Kaniko |
|------|-----------|---------|--------|
| 工具 | docker build | Buildah(Red Hat) | Kaniko(Google) |
| 是否需要 daemon | 是(dockerd) | 否 | 否 |
| 镜像兼容 | OCI | OCI | OCI |
| K8s 友好 | 一般 | 好 | 极好(无特权) |
| 适用场景 | 本地开发 | CI/CD | K8s 集群内构建 |

### 9.2 Dockerfile vs Nix / Bazel

| 维度 | Dockerfile | Nix | Bazel |
|------|-----------|-----|-------|
| 设计目标 | 容器镜像构建 | 函数式包管理 | 多语言构建系统 |
| 可重现 | 一般(依赖网络) | 极强(纯函数) | 强(锁版本) |
| 学习曲线 | 低 | 高 | 高 |
| 镜像大小 | 取决于写法 | 极小 | 取决于配置 |
| 适用 | 通用 | NixOS 生态 | Google / Stripe 等大厂 |

> **趋势**:大厂用 Bazel/Nix 做构建,Dockerfile 只做最后打包。

---

## 10. 面试速答

| 问题 | 一句话答案 |
|------|-----------|
| 镜像为什么分层? | 共享底层节省存储、构建缓存加速、按需下载层。 |
| overlay2 中 upperdir / lowerdir 是什么? | lowerdir 是只读镜像层,upperdir 是可读写容器层;写时复制到 upper。 |
| Dockerfile 哪些指令新建层? | RUN / COPY / ADD 新建层;ENV / LABEL / WORKDIR / CMD 等只改元数据。 |
| 多阶段构建解决什么? | 把构建工具(编译器、源码)与运行时分离,最终镜像只含运行所需,体积大降。 |
| 为什么 `COPY requirements.txt` 要放 `COPY .` 前面? | requirements.txt 很少变,放前面让 pip install 缓存命中;代码变不重装依赖。 |
| `CMD` 与 `ENTRYPOINT` 区别? | CMD 可被 `docker run` 参数覆盖,ENTRYPOINT 不易覆盖;组合使用 ENTRYPOINT 定命令、CMD 定默认参数。 |
| 镜像 digest 与 tag 区别? | tag 是可变别名(latest 会变),digest 是内容 hash,不可变,唯一标识。 |
| `.dockerignore` 干什么? | 排除文件进入构建上下文,减小镜像、加速构建、避免泄漏 secret。 |
| BuildKit 比旧 builder 强在哪? | 并行构建、跨机器缓存、懒加载、新语法支持。 |
| alpine 镜像为什么小但有时慢? | 用 musl libc,部分 wheel 不兼容需编译;Python/Node 等生态兼容性差。 |

---

## 11. 综合面试题

### 题 1(原理)
**问**:解释镜像分层的实现原理,以及为什么 overlay2 比 aufs 优。

**答题要点**:
- 分层通过 UnionFS 实现,多个只读层叠加 + 一个可读写层
- overlay2 是 Linux 内核原生(3.18+),aufs 是 out-of-tree 补丁
- overlay2 性能更好(少一次 lookup)、维护成本低
- overlay2 支持多层(默认 128),aufs 限制更多
- Docker 18.06+ 默认 overlay2

### 题 2(实战)
**问**:一个 1.2 GB 的 Go 镜像,如何优化到 20 MB?

**答题要点**:
- 多阶段构建:builder 阶段用 golang,运行阶段用 distroless/scratch
- `CGO_ENABLED=0` 静态链接,可跑在 scratch
- `-ldflags="-s -w"` 去掉调试信息
- 不带源码、不带编译器、不带 shell
- distroless 没有 shell,需 `COPY` 静态配置文件
- 用 `upx` 压缩二进制(可选,代价:启动慢)

### 题 3(缓存)
**问**:CI 里 `docker build` 缓存经常失效,如何排查?

**答题要点**:
- 看是哪一层 miss:`docker build --progress=plain` 看 `CACHED` 标记
- FROM 变了?(基础镜像 latest → 改成 digest 锁定)
- COPY 源文件变了?(`.dockerignore` 排除 `.git` 等)
- ARG 变了?(每次 CI 注入不同 ARG 会导致后续 miss)
- 网络层(apt-get / pip)无 cache key,建议用 vendor 锁文件
- BuildKit `--cache-from` 配置是否正确
- CI runner 是否每次新建(本地缓存丢失)

### 题 4(安全)
**问**:如何避免把 secret 写进镜像?

**答题要点**:
- Dockerfile 不写明文密码
- BuildKit `--mount=type=secret`:运行时挂载,不留在层里
- `docker history` 检查是否有 secret
- Trivy / dockle 扫描镜像
- 运行时通过 K8s Secret / Vault 注入
- CI 不在 Dockerfile 里 build args 传密码

### 题 5(故障)
**问**:`docker build` 报 `Cannot connect to the Docker daemon`,但 `docker ps` 正常。

**答题要点**:
- 看是不是用了 buildx:`docker buildx ls`,builder 可能配置错误
- `DOCKER_HOST` 环境变量是否被改
- `docker context ls` 切错了 context
- BuildKit daemon 启动失败:`DOCKER_BUILDKIT=0` 退回旧 builder 验证
- 权限问题:用户不在 docker 组

### 题 6(工业)
**问**:1000 个微服务如何统一镜像规范?

**答题要点**:
- 提供官方基础镜像(公司统一 debian + tini + ca-certs)
- Dockerfile lint(dockerfile_lint / hadolint)在 CI 强制
- 镜像扫描(Trivy)门槛:CRITICAL 不通过
- 镜像签名(Cosign)准入控制
- 镜像大小门槛:< 500 MB,否则 PR 不通过
- 多阶段构建强制
- 非 root 强制
- 提供 5-10 个语言模板(Go/Python/Java/Node/Rust)
- 文档:Confluence 上的镜像规范,新项目从模板初始化

### 题 7(深度)
**问**:为什么 `RUN cd /app && pip install` 与 `WORKDIR /app` + `RUN pip install` 效果不同?

**答题要点**:
- 每个 RUN 是新 shell,cd 不影响下一个 RUN
- WORKDIR 改变镜像元数据,所有后续指令工作目录都是 /app
- RUN cd 是临时的,只在当前 RUN 生效
- 推荐用 WORKDIR,清晰且可缓存

### 题 8(性能)
**问**:镜像层数对启动时间有何影响?

**答题要点**:
- 每层一次 overlay mount,层数多启动慢
- 但 overlay2 已经优化,几十层影响不大(< 100 ms)
- 真正影响启动的是镜像大小(拉取时间)与启动进程本身
- 极端情况(几百层)会显著变慢
- 工业建议 10-20 层,合理利用缓存

### 题 9(架构)
**问**:为什么 distroless 比 alpine 更适合生产?

**答题要点**:
- distroless 不含 shell,攻击面小
- 用 glibc,兼容性好(Python wheel / Java JDK)
- alpine 用 musl,部分库不兼容需重编译
- distroless 由 Google 维护,与 K8s 生态契合
- 缺点:无 shell,调试困难(需 `:debug` 变种)
- 工业选择:distroless > slim > alpine > full

### 题 10(综合)
**问**:设计一个镜像构建流水线,要求可重现、可缓存、安全。

**答题要点**:
- 源码:Git,锁 commit SHA
- 基础镜像:锁 digest(`nginx@sha256:...`)
- 依赖:锁文件(requirements.txt / go.sum / Cargo.lock)
- 构建:BuildKit + `--cache-from=type=registry`
- 多阶段:builder + runner
- Lint:hadolint + dockerfile_lint
- 扫描:Trivy + Grype
- 签名:Cosign + Rekor 透明日志
- 标签:语义版本 + git SHA + 构建时间
- 推送:私有 Harbor + 多架构
- 准入:K8s Admission Controller 验证签名与扫描结果
- 审计:每次构建记录到 SBOM

---

## 12. 故障复盘

### 案例 1:`.dockerignore` 缺失导致镜像含 secret

**现象**:某开发者把 AWS access key 放在 `.env` 里,`COPY . .` 把 `.env` 拷进镜像,镜像推到 Docker Hub 公开仓库。一周后被爬虫扫到,AWS 账单 $50000。

**根因**:
- 没有 `.dockerignore`
- `COPY . .` 没有针对性
- 没有 secret 扫描

**修复**:
```
# .dockerignore
.env
.env.*
*.pem
*.key
secrets/
```

```bash
# CI 加 secret 扫描
trivy fs --secret-config trivy-secret.yaml .
```

**防范**:
- 强制 `.dockerignore` 模板
- CI 跑 trivy / gitleaks
- AWS key 用临时凭证(STS),不长期有效
- 镜像扫描门槛:含 secret 不通过

### 案例 2:基础镜像 `latest` 导致构建不可重现

**现象**:某团队 CI 跑了一年没问题,某天突然所有镜像构建失败,排查发现是 `python:latest` 升级到 3.13,某依赖不兼容。

**根因**:
- `FROM python:latest` 没锁版本
- Python 3.13 移除了某些 API

**修复**:
```dockerfile
FROM python:3.12-slim@sha256:abc123...   # 锁版本 + digest
```

**防范**:
- hadolint 规则:`DL3006 Don't use :latest`
- CI 检查 Dockerfile FROM 必须是 digest
- 基础镜像定期升级(每月),不靠 latest 自动

### 案例 3:`RUN apt-get install` 不清理导致镜像膨胀

**现象**:某团队镜像从 200 MB 涨到 600 MB,排查发现 apt 缓存层 400 MB。

**根因**:
```dockerfile
# 错误写法
RUN apt-get update
RUN apt-get install -y curl git vim
# 没有 rm -rf /var/lib/apt/lists/*
```

**修复**:
```dockerfile
RUN apt-get update \
    && apt-get install -y --no-install-recommends curl git vim \
    && rm -rf /var/lib/apt/lists/*
```

**防范**:
- hadolint 规则:`DL3009 Delete the apt-get lists after installing`
- 镜像大小门槛:CI 检查镜像增量
- 用 distroless 避免装包

### 案例 4:多阶段构建漏 COPY 配置文件

**现象**:某 Go 项目多阶段构建后,容器启动报 `config.yaml not found`,但本地构建正常。

**根因**:
```dockerfile
FROM golang:1.22 AS builder
COPY . .
RUN go build -o /app/server ./cmd/server

FROM scratch
COPY --from=builder /app/server /server
# 漏了: COPY --from=builder /src/configs /configs
CMD ["/server"]
```

**修复**:
```dockerfile
FROM scratch
COPY --from=builder /app/server /server
COPY --from=builder /src/configs /configs
```

**防范**:
- Dockerfile lint 检查 COPY 完整性
- 本地构建 + 运行验证后再推
- CI 跑镜像 smoke test(启动 + 健康检查)

### 案例 5:BuildKit 缓存配置错误导致 CI 慢

**现象**:某公司 CI 把缓存推到 registry,但每次都重建,缓存命中率 0%。

**根因**:
```bash
# 错误:cache-to 没加 mode=max,只缓存最终层
docker buildx build \
  --cache-from=type=registry,ref=cache:v1 \
  --cache-to=type=registry,ref=cache:v1 \
  -t app:v1 .
```

**修复**:
```bash
# mode=max 缓存所有中间层
docker buildx build \
  --cache-from=type=registry,ref=cache:v1 \
  --cache-to=type=registry,ref=cache:v1,mode=max \
  -t app:v1 .
```

**防范**:
- 文档化 buildx 缓存最佳实践
- CI 监控缓存命中率,< 50% 报警
- 缓存层用单独 repo,避免被 GC

---

## 13. 参考与延伸

### 官方文档

- Dockerfile reference — https://docs.docker.com/engine/reference/builder/
- BuildKit — https://docs.docker.com/build/buildkit/
- buildx — https://docs.docker.com/build/buildx/
- Best practices — https://docs.docker.com/develop/develop-images/dockerfile_best-practices/

### 工具

- hadolint — Dockerfile linter
- dive — 镜像分层分析
- Trivy — 镜像扫描
- Cosign — 镜像签名
- syft — SBOM 生成
- slim — 镜像自动瘦身

### 大厂博客

- Google Distroless — https://github.com/GoogleContainerTools/distroless
- Stripe Bazel + Docker — 工程博客
- 字节镜像优化实践 — InfoQ 演讲

### 相关模块

- [02-安装与CLI基础](./02-安装与CLI基础.md) — 上一章
- [04-容器运行与生命周期](./04-容器运行与生命周期.md) — 下一章
- [10-底层原理-UnionFS](./10-底层原理-UnionFS.md) — 分层存储内核机制
- [15-Dockerfile实战模板](./15-Dockerfile实战模板.md) — 各语言生产级模板
- [16-Docker与CI-CD](./16-Docker与CI-CD.md) — CI 流水线中的构建
- [24-工业实战-供应链安全](./24-工业实战-供应链安全.md) — 镜像签名与扫描

---

> **下一章**:[04-容器运行与生命周期](./04-容器运行与生命周期.md)
