# 02 - 安装与 CLI 基础

> 把 Docker 装到机器上、把 CLI 用熟、把 daemon 配好——所有后续章节的前提。

---

## 1. 问题定义与边界

### 1.1 本章解决什么

- 在 **Linux / macOS / Windows** 三类平台上把 Docker 装起来
- 理解 **Docker Engine / Docker Desktop / Docker CE / Docker EE** 的区别,选对发行版
- 把 **CLI** 常用命令用熟,建立"镜像—容器—卷—网络"四类对象的心智模型
- 把 **daemon**(`dockerd`) 配置好,避免生产环境的常见坑

### 1.2 本章不解决什么

- 不讲 K8s 节点上的 containerd 安装(见 [11-OCI规范与运行时](./11-OCI规范与运行时.md))
- 不讲 Docker Desktop 的 K8s 集成(见 [18-Docker Swarm入门](./18-Docker-Swarm入门.md))
- 不讲镜像构建细节(见 [03-镜像原理与Dockerfile](./03-镜像原理与Dockerfile.md))
- 不讲 rootless 模式的安全机制(见 [12-安全与隔离](./12-安全与隔离.md))

> **关键认知**:Docker 在 2017 年拆分为 **CE(社区版,免费)** 与 **EE(企业版,付费)**,2019 年后 EE 改名 **Docker EE / Mirantis Container Runtime**。开源项目与个人学习几乎只用 CE。

---

## 2. 直觉解释

### 2.1 三个层次的"安装对象"

```
┌──────────────────────────────────────┐
│  Docker Desktop (macOS/Windows)      │  ← 图形化 + Linux VM + CLI
│  - 含 GUI、K8s、Compose、BuildKit    │
├──────────────────────────────────────┤
│  Docker Engine (Linux)               │  ← dockerd + containerd + runc + CLI
│  - 服务端 + 客户端,无 GUI            │
├──────────────────────────────────────┤
│  Docker CLI (跨平台)                 │  ← 命令行客户端
│  - 可连接远程 daemon                  │
└──────────────────────────────────────┘
```

**类比**:
- **Docker Engine** = 引擎本体(Linux 原生)
- **Docker Desktop** = "引擎 + GUI + Linux VM"(给 Mac/Win 用)
- **Docker CLI** = 方向盘(可远程操控引擎)

### 2.2 三类典型安装场景

| 场景 | 选择 | 原因 |
|------|------|------|
| 个人开发(Mac/Win) | Docker Desktop | 一键装好,含 K8s/Compose |
| Linux 服务器生产 | Docker Engine CE | 轻量、稳定、可控 |
| 企业内网(无外网) | Docker Engine CE + 私有镜像源 | 离线安装 + 内网仓库 |

---

## 3. 核心概念与架构

### 3.1 Docker Engine 的组成

```
┌────────────────────────────────────────────────┐
│              Docker Engine                     │
│                                                │
│  ┌──────────┐  ┌────────────┐  ┌──────────┐  │
│  │ dockerd  │  │ containerd │  │  runc    │  │
│  │ (daemon) │→ │ (高级运行时)│→ │(低级运行时)│  │
│  └────┬─────┘  └─────┬──────┘  └────┬─────┘  │
│       │              │              │         │
│       │              ▼              ▼         │
│       │      ┌──────────────┐  Linux Kernel  │
│       │      │containerd-shim│  namespace     │
│       │      │  (每容器一个) │  cgroup        │
│       │      └──────────────┘  overlayfs     │
│       │                                       │
│       ▼                                       │
│  ┌──────────┐  ┌─────────┐  ┌──────────┐    │
│  │ network  │  │ volume  │  │  build   │    │
│  │ plugin   │  │ plugin  │  │ (BuildKit)│   │
│  └──────────┘  └─────────┘  └──────────┘    │
└────────────────────────────────────────────────┘
              ↑
              │ REST API (UNIX socket / TCP)
              │
        ┌─────────┐
        │ docker  │  ← CLI(独立二进制)
        │  CLI    │
        └─────────┘
```

### 3.2 三类对象:镜像 / 容器 / 卷 / 网络

```
   镜像(Image)            容器(Container)
   ┌────────┐              ┌────────┐
   │ 模板    │ ──run──>    │ 实例    │
   └────────┘              └────────┘
       ↑                       │
       │                       │ commit
       │                       ▼
       └─────── new image ─────┘

   卷(Volume)              网络(Network)
   ┌────────┐              ┌────────┐
   │ 持久化  │              │ 通信    │
   │ 数据    │              │ 拓扑    │
   └────────┘              └────────┘
       ↑                       ↑
       └── 挂载到容器           └── 容器接入
```

### 3.3 命令族全景(2017 整理后的 v1.13+ 语法)

```
docker
├── 管理类(顶层)
│   ├── docker image    # 镜像管理
│   ├── docker container# 容器管理
│   ├── docker volume   # 卷管理
│   ├── docker network  # 网络管理
│   ├── docker system   # 系统级(磁盘/清理/信息)
│   ├── docker builder  # 构建(BuildKit)
│   ├── docker context  # 多 daemon 切换
│   ├── docker plugin   # 插件
│   ├── docker secret   # Swarm secret
│   ├── docker service  # Swarm service
│   ├── docker stack    # Swarm stack
│   ├── docker swarm    # Swarm 模式
│   └── docker trust    # 镜像签名
│
└── 快捷类(向后兼容)
    ├── docker pull      = docker image pull
    ├── docker run       = docker container run
    ├── docker ps        = docker container ls
    ├── docker images    = docker image ls
    ├── docker exec      = docker container exec
    └── docker logs      = docker container logs
```

> **记忆法**:`docker <对象> <动作>`,如 `docker image ls`、`docker container run`。快捷形式只是别名。

---

## 4. 操作流程与命令

### 4.1 Linux(Ubuntu/Debian)安装

```bash
# 1. 卸载旧版(避免冲突)
sudo apt remove docker docker-engine docker.io containerd runc

# 2. 添加 Docker 官方 GPG key 与仓库
sudo apt update
sudo apt install -y ca-certificates curl gnupg

sudo install -m 0755 -d /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | \
  sudo gpg --dearmor -o /etc/apt/keyrings/docker.gpg
sudo chmod a+r /etc/apt/keyrings/docker.gpg

echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] \
  https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable" | \
  sudo tee /etc/apt/sources.list.d/docker.list > /dev/null

# 3. 安装
sudo apt update
sudo apt install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin

# 4. 验证
sudo docker run hello-world

# 5. 让非 root 用户可用(免 sudo)
sudo usermod -aG docker $USER
newgrp docker   # 立即生效,或重新登录
```

### 4.2 CentOS/RHEL 安装

```bash
# 卸载旧版
sudo yum remove -y docker docker-client docker-client-latest \
  docker-common docker-latest docker-latest-logind docker-logind docker-engine

# 添加仓库
sudo yum install -y yum-utils
sudo yum-config-manager --add-repo \
  https://download.docker.com/linux/centos/docker-ce.repo

# 安装
sudo yum install -y docker-ce docker-ce-cli containerd.io \
  docker-buildx-plugin docker-compose-plugin

# 启动并设置开机自启
sudo systemctl enable --now docker

# 验证
sudo docker run hello-world
```

### 4.3 macOS 安装

**推荐方式:Docker Desktop**

```bash
# 方式 1:官网下载 dmg(https://www.docker.com/products/docker-desktop)
# 双击安装,启动 Docker Desktop GUI

# 方式 2:Homebrew
brew install --cask docker
open /Applications/Docker.app
```

**架构选择**:
- Apple Silicon(M1/M2/M3)→ Apple Silicon 版(arm64)
- Intel Mac → Intel 版(x86_64)

**底层机制**:macOS 没有 Linux 内核,Docker Desktop 在 Hypervisor.framework 上跑一个轻量 LinuxKit VM,容器实际运行在 VM 内。

```
┌────────────────────────────────────┐
│  macOS                             │
│  ┌──────────────────────────────┐  │
│  │ Docker Desktop GUI           │  │
│  └──────────┬───────────────────┘  │
│  ┌──────────▼───────────────────┐  │
│  │ Docker CLI                   │  │
│  └──────────┬───────────────────┘  │
│  ┌──────────▼───────────────────┐  │
│  │ LinuxKit VM (Hypervisor.fw)  │  │
│  │  ┌────────────────────────┐  │  │
│  │  │ dockerd + containerd   │  │  │
│  │  │ Linux kernel           │  │  │
│  │  └────────────────────────┘  │  │
│  └──────────────────────────────┘  │
└────────────────────────────────────┘
```

### 4.4 Windows 安装

**前置要求**:WSL 2 或 Hyper-V(Windows 10/11 Pro+)

```powershell
# 方式 1:启用 WSL 2
wsl --install
# 重启电脑后,从官网下载 Docker Desktop Installer.exe

# 方式 2:winget
winget install Docker.DockerDesktop

# 方式 3:Chocolatey
choco install docker-desktop
```

**架构选择**:
- WSL 2 backend(推荐)— 性能更好,文件系统兼容
- Hyper-V backend — 旧版,兼容性差

**底层机制**:

```
┌──────────────────────────────────────┐
│  Windows                             │
│  ┌────────────────────────────────┐  │
│  │ Docker Desktop GUI             │  │
│  └──────────┬─────────────────────┘  │
│  ┌──────────▼─────────────────────┐  │
│  │ Docker CLI (Windows 原生)      │  │
│  └──────────┬─────────────────────┘  │
│  ┌──────────▼─────────────────────┐  │
│  │ WSL 2 (lightweight Hyper-V)    │  │
│  │  ┌──────────────────────────┐  │  │
│  │  │ docker-desktop WSL distro│  │  │
│  │  │  dockerd + containerd    │  │  │
│  │  │  真正的 Linux 内核       │  │  │
│  │  └──────────────────────────┘  │  │
│  └────────────────────────────────┘  │
└──────────────────────────────────────┘
```

> **关键**:Windows 上的容器也跑在 Linux 内核里(WSL 2 的内核)。Windows 容器(原生)是另一回事,极少用。

### 4.5 离线安装(内网/隔离环境)

```bash
# 1. 在有外网的机器上下载 deb/rpm 包
# https://download.docker.com/linux/ubuntu/dists/jammy/pool/stable/amd64/

# 2. 拷贝到目标机器
scp containerd.io_1.6.*.deb docker-ce_24.*.deb docker-ce-cli_24.*.deb \
    user@target:/tmp/

# 3. 安装
sudo dpkg -i /tmp/*.deb

# 4. 配置私有仓库
sudo tee /etc/docker/daemon.json <<EOF
{
  "registry-mirrors": ["https://harbor.internal.corp.com"],
  "insecure-registries": ["harbor.internal.corp.com"]
}
EOF

sudo systemctl restart docker
```

### 4.6 验证安装

```bash
# 1. 版本
docker version
# Client: Docker Engine - Community
#  Cloud integration: v1.0.35-desktop+001
#  Version:           24.0.7
# Server: Docker Desktop 4.26.1
#  Engine:
#   Version:          24.0.7

# 2. 系统信息
docker info
# ... 输出包括内核版本、容器数、镜像数、存储驱动、cgroup 版本等

# 3. 跑测试镜像
docker run hello-world

# 4. 跑一个真实服务
docker run -d -p 8080:80 --name web nginx:alpine
curl http://localhost:8080

# 5. 清理
docker rm -f web
```

### 4.7 CLI 常用命令清单

#### 镜像命令

```bash
docker pull nginx:1.25              # 拉取镜像
docker images                       # 列出本地镜像
docker image ls --digests           # 含 SHA256
docker image inspect nginx:1.25     # 查看镜像元数据
docker image rm nginx:1.25          # 删除镜像
docker image prune -a               # 删除所有未使用镜像
docker tag nginx:1.25 myrepo/nginx:v1  # 打标签
docker save -o nginx.tar nginx:1.25 # 导出为文件
docker load -i nginx.tar            # 从文件导入
docker history nginx:1.25           # 查看镜像分层历史
```

#### 容器命令

```bash
docker run -d --name web -p 8080:80 nginx:1.25   # 后台运行
docker ps                                       # 运行中的容器
docker ps -a                                    # 包括已停止的
docker logs -f web                              # 看日志
docker exec -it web sh                          # 进入容器
docker stats                                    # 资源占用
docker top web                                  # 容器内进程
docker stop web                                 # 优雅停止
docker kill web                                 # 强制停止
docker start web                                # 重新启动
docker restart web                              # 重启
docker rm web                                   # 删除(需先停止)
docker rm -f web                                # 强制删除
docker cp web:/etc/nginx/nginx.conf ./          # 拷贝文件出来
docker cp ./app.py web:/app/                    # 拷贝文件进去
docker commit web myapp:v1                      # 提交为新镜像(不推荐)
```

#### 卷与网络

```bash
# 卷
docker volume create mydata
docker volume ls
docker volume inspect mydata
docker volume rm mydata
docker volume prune

# 网络
docker network create mynet
docker network ls
docker network inspect mynet
docker network connect mynet web
docker network disconnect mynet web
docker network rm mynet
```

#### 系统级

```bash
docker system df                # 磁盘占用
docker system prune -a          # 清理所有未使用
docker system events            # 实时事件流
docker info                     # 系统信息
docker context ls               # 多 daemon 切换
docker context use remote       # 切换到远程 daemon
```

---

## 5. 底层原理(简略)

### 5.1 `docker run` 在内核里发生了什么

> 完整版见 [04-容器运行与生命周期](./04-容器运行与生命周期.md) 与 [08-底层原理-namespaces](./08-底层原理-namespaces.md)。

```
docker run -d --name web -p 8080:80 nginx:1.25
         │
         ▼
1. CLI → dockerd(通过 /var/run/docker.sock)
2. dockerd 检查镜像:
   ├─ 本地有 → 跳过
   └─ 本地无 → 从 registry 拉取,存到 /var/lib/docker/overlay2/
3. dockerd 创建容器元数据(/var/lib/docker/containers/<id>/)
4. dockerd 调用 containerd.CreateContainer()
5. containerd 准备 rootfs:
   - mount overlayfs(lowerdir = 镜像层,upperdir = 容器层)
6. containerd 调用 runc.create()
7. runc:
   - clone(CLONE_NEWPID | CLONE_NEWNET | CLONE_NEWNS | ...)
   - 设置 cgroup(/sys/fs/cgroup/.../docker/<id>)
   - pivot_root 到新 rootfs
   - exec nginx(成为 PID 1)
8. runc 退出,containerd-shim 接管为容器父进程
9. dockerd 配置端口转发(iptables DNAT 8080→容器:80)
10. 容器对外提供服务
```

### 5.2 daemon 配置文件 `daemon.json`

```json
{
  "registry-mirrors": ["https://mirror.ccs.tencentyun.com"],
  "insecure-registries": ["harbor.corp.com"],
  "data-root": "/data/docker",
  "storage-driver": "overlay2",
  "exec-opts": ["native.cgroupdriver=systemd"],
  "log-driver": "json-file",
  "log-opts": {
    "max-size": "100m",
    "max-file": "5"
  },
  "live-restore": true,
  "max-concurrent-downloads": 10,
  "default-ulimits": {
    "nofile": {"Hard": 65535, "Soft": 65535}
  }
}
```

修改后 `sudo systemctl restart docker`(开启 `live-restore` 时可不重启)。

---

## 6. 代码与配置示例

### 6.1 生产级 `daemon.json`

```json
{
  "data-root": "/data/docker",
  "storage-driver": "overlay2",
  "exec-opts": ["native.cgroupdriver=systemd"],

  "registry-mirrors": [
    "https://harbor.internal.corp.com"
  ],

  "log-driver": "json-file",
  "log-opts": {
    "max-size": "100m",
    "max-file": "10"
  },

  "live-restore": true,
  "userland-proxy": false,
  "iptables": true,
  "ip-forward": true,
  "ip-masq": true,
  "bridge": "docker0",

  "default-runtime": "runc",
  "runtimes": {
    "nvidia": {
      "path": "nvidia-container-runtime",
      "runtimeArgs": []
    }
  },

  "max-concurrent-downloads": 20,
  "max-concurrent-uploads": 10,

  "default-ulimits": {
    "nofile": {"Hard": 65535, "Soft": 65535},
    "nproc":  {"Hard": 65535, "Soft": 65535}
  },

  "metrics-addr": "0.0.0.0:9323",
  "experimental": true
}
```

### 6.2 多 daemon 切换(context)

```bash
# 创建一个远程 context
docker context create remote \
  --docker "host=ssh://user@10.0.0.10" \
  --description "Production node 1"

# 列出
docker context ls
# NAME       DESCRIPTION                          DOCKER ENDPOINT
# default *  Current DOCKER_HOST based config    npipe:////./pipe/docker_engine
# remote     Production node 1                    ssh://user@10.0.0.10

# 切换
docker context use remote
docker ps   # 现在列出的是远程机器上的容器

# 切回
docker context use default
```

> **工业场景**:运维同时管理多个集群,用 context 切换比改环境变量方便。

### 6.3 systemd 服务单元

```ini
# /usr/lib/systemd/system/docker.service
[Unit]
Description=Docker Application Container Engine
Documentation=https://docs.docker.com
After=network-online.target docker.socket firewalld.service containerd.service
Wants=network-online.target containerd.service
Requires=docker.socket

[Service]
Type=notify
ExecStart=/usr/bin/dockerd -H fd:// --containerd=/run/containerd/containerd.sock
ExecReload=/bin/kill -s HUP $MAINPID
TimeoutSec=0
RestartSec=2
Restart=always
StartLimitBurst=3
StartLimitInterval=60s
LimitNOFILE=infinity
LimitNPROC=infinity
LimitCORE=infinity
TasksMax=infinity

[Install]
WantedBy=multi-user.target
```

---

## 7. 常见陷阱与调优

### 7.1 陷阱:`sudo docker` 而非 `docker`

**问题**:每次都要 `sudo`,或权限错误 `Cannot connect to the Docker daemon`。

**原因**:dockerd 默认监听 `/var/run/docker.sock`,属主 root:docker,普通用户无权限。

**修复**:
```bash
sudo usermod -aG docker $USER
newgrp docker   # 或重新登录
```

> **安全提醒**:把用户加入 docker 组等同于给 root 权限(可挂载宿主机根目录)。生产环境慎用,见 [12-安全与隔离](./12-安全与隔离.md)。

### 7.2 陷阱:磁盘占用无限增长

**问题**:`/var/lib/docker` 占满磁盘。

**原因**:
- json-file 日志驱动无大小限制
- 死容器与悬空镜像(dangling)未清理
- 构建缓存累积

**修复**:
```json
// daemon.json
"log-driver": "json-file",
"log-opts": {
  "max-size": "100m",
  "max-file": "10"
}
```

```bash
# 定时清理( crontab )
0 3 * * * docker system prune -a --volumes --filter "until=168h"
```

> **工业实践**:阿里 / 字节在构建机上每天清理,生产节点用日志驱动 `journald` 或 `fluentd`,不写本地文件。

### 7.3 陷阱:`live-restore` 没开

**问题**:升级 dockerd 后所有容器重启,业务受影响。

**修复**:
```json
{ "live-restore": true }
```

开启后,dockerd 重启不影响容器(由 containerd-shim 接管)。

### 7.4 陷阱:cgroup driver 不匹配

**问题**:K8s 节点上 kubelet 与 dockerd 用不同 cgroup driver,导致 kubelet 拿不到容器指标。

**修复**:
```json
{ "exec-opts": ["native.cgroupdriver=systemd"] }
```

K8s 1.22+ 默认 systemd cgroup driver,dockerd 必须一致。

### 7.5 陷阱:macOS 文件挂载慢

**问题**:在 Mac 上 `docker run -v $(pwd):/app` 后,文件 IO 极慢,Webpack 热更新延迟 30 秒。

**原因**:macOS 的文件需要从宿主机通过 VirtioFS / gRPC FUSE 转发到 LinuxKit VM,延迟大。

**修复**:
- 升级 Docker Desktop 4.6+(默认 VirtioFS,比旧的 osxfs 快 10 倍)
- 关闭"Use gRPC FUSE",改用 VirtioFS
- 开启 `delegated` 或 `cached`(已废弃,新版本自动优化)
- 终极方案:把代码放在容器内(不用挂载),用 `docker cp` 同步

### 7.6 陷阱:Windows 路径挂载

**问题**:`docker run -v C:\Users\me\app:/app` 报错或路径错乱。

**修复**:
```bash
# 必须用 /c/Users/me/app 形式
docker run -v /c/Users/me/app:/app myimage

# 或 PowerShell
docker run -v ${PWD}:/app myimage
```

### 7.7 陷阱:防火墙冲突

**问题**:开了 firewalld 后,容器之间不通,或外部访问不到容器。

**原因**:firewalld 与 docker 的 iptables 规则冲突。

**修复**:
```bash
# 方案 1:停用 firewalld(简单粗暴)
sudo systemctl stop firewalld

# 方案 2:配置 firewalld 信任 docker0
sudo firewall-cmd --permanent --zone=trusted --add-interface=docker0
sudo firewall-cmd --reload
```

---

## 8. 工业案例与基准数据

### 8.1 大厂安装标准化:Ansible / Salt

**场景**:1000 台服务器需要装 Docker,配置统一。

**典型方案**(Ansible 示例):

```yaml
# roles/docker/tasks/main.yml
- name: Remove old Docker versions
  apt:
    name: "{{ item }}"
    state: absent
  loop:
    - docker
    - docker-engine
    - docker.io

- name: Add Docker GPG key
  apt_key:
    url: https://download.docker.com/linux/ubuntu/gpg
    state: present

- name: Add Docker repository
  apt_repository:
    repo: "deb [arch=amd64] https://download.docker.com/linux/ubuntu {{ ansible_distribution_release }} stable"
    state: present

- name: Install Docker CE
  apt:
    name:
      - docker-ce=5:24.0.*
      - docker-ce-cli=5:24.0.*
      - containerd.io
      - docker-buildx-plugin
      - docker-compose-plugin
    state: present
    update_cache: yes

- name: Configure Docker daemon
  copy:
    dest: /etc/docker/daemon.json
    content: |
      {{ lookup('template', 'daemon.json.j2') | indent(2) }}
  notify: Restart Docker

- name: Ensure Docker is enabled and running
  systemd:
    name: docker
    state: started
    enabled: yes
```

### 8.2 阿里云 ACK 节点 Docker 配置(公开资料)

```json
{
  "data-root": "/var/lib/docker",
  "storage-driver": "overlay2",
  "exec-opts": ["native.cgroupdriver=systemd"],
  "registry-mirrors": ["https://registry.cn-hangzhou.aliyuncs.com"],
  "log-driver": "json-file",
  "log-opts": {
    "max-size": "100m",
    "max-file": "10"
  },
  "max-concurrent-downloads": 20,
  "live-restore": true,
  "default-ulimits": {
    "nofile": {"Hard": 655350, "Soft": 655350}
  }
}
```

> **关键差异**:阿里默认 `nofile` 上限 65 万(应对海量连接),`max-concurrent-downloads` 翻倍(加速大集群拉镜像)。

### 8.3 Docker Desktop vs Engine 性能对比

| 维度 | Docker Desktop (Mac) | Docker Engine (Linux) |
|------|----------------------|------------------------|
| 容器启动延迟 | +50-100 ms(VM 跨界) | 原生 |
| 文件挂载 IO | 慢 5-10 倍(VirtioFS) | 原生 |
| 网络延迟 | +1-3 ms(VM NAT) | 原生 |
| CPU 开销 | +5%(VM 间接) | 0% |
| 适用场景 | 开发 | 生产 |

> **结论**:开发用 Desktop,生产必须用 Engine。Mac 上的性能数据不能直接外推到生产。

### 8.4 离线安装的工业实践

**场景**:金融、政企内网无外网,但仍要装 Docker。

**典型流程**:
1. 在外网构建机:`apt download docker-ce docker-ce-cli containerd.io` 下载 deb
2. 同步基础镜像到私有仓库:`docker pull` + `docker push` 到 Harbor
3. 拷贝 deb 与 `daemon.json` 模板到内网
4. Ansible 批量安装,配置 `insecure-registries` 指向内网 Harbor
5. 节点 `docker pull` 从内网 Harbor 拉镜像

**踩过的坑**:
- deb 包依赖 `container-selinux`,离线环境要一并下载
- `daemon.json` 里 `data-root` 必须指向大盘,否则一周爆
- 内网 Harbor 用自签证书,必须配 `insecure-registries` 或导入 CA

---

## 9. 与其他方案的关系

### 9.1 Docker Engine vs containerd(裸用)

| 维度 | Docker Engine | containerd(裸) |
|------|---------------|-------------------|
| 镜像构建 | docker build / BuildKit | nerdctl build |
| 网络管理 | docker network | CNI 插件 |
| 卷管理 | docker volume | CSI 插件 |
| CLI | docker | nerdctl / ctr |
| 体积 | ~150 MB | ~50 MB |
| K8s 兼容 | 通过 dockershim(已废弃) | 原生 CRI |
| 适用场景 | 通用开发 / 单机生产 | K8s 节点 / 极致精简 |

> 详见 [11-OCI规范与运行时](./11-OCI规范与运行时.md)。

### 9.2 Docker Desktop vs Rancher Desktop / Podman Desktop

| 维度 | Docker Desktop | Rancher Desktop | Podman Desktop |
|------|----------------|-----------------|----------------|
| 内核 | containerd + LinuxKit | containerd + WSL/k3s | podman + WSL |
| K8s 内置 | 可选启用 | 默认 k3s | 无 |
| 商业授权 | 大企业收费(250+ 员工) | 完全开源 | 完全开源 |
| Compose | 原生 | 原生 | podman-compose |
| 镜像兼容 | OCI | OCI | OCI |

> **背景**:Docker Desktop 2021 年起对 250+ 员工的企业收费,推动了大厂转向 Rancher Desktop / Podman Desktop。

### 9.3 Linux 容器 vs Windows 容器

| 维度 | Linux 容器 | Windows 容器 |
|------|-----------|--------------|
| 内核 | Linux | Windows Server 内核 |
| 镜像 | ubuntu / alpine 等 | nanoserver / servercore |
| 隔离 | namespace + cgroup | Job Object + silo |
| 适用 | 99% 场景 | .NET Framework 旧应用 |
| 跨平台 | macOS/Win 上用 VM 跑 | 必须在 Windows 上 |

---

## 10. 面试速答

| 问题 | 一句话答案 |
|------|-----------|
| Docker CE 和 EE 区别? | CE 是社区版(免费开源),EE 是企业版(付费,改名 Mirantis Container Runtime);2017 拆分。 |
| Docker Desktop 和 Engine 区别? | Desktop 是 Mac/Win 的图形化套件,内含 Linux VM + GUI;Engine 是 Linux 上的纯命令行服务端。 |
| 为什么把用户加入 docker 组等同给 root? | 因为可以在容器里挂载宿主机 `/` 然后读写,等效 root。 |
| `daemon.json` 最关键配置有哪些? | `data-root`(盘位置)、`log-opts`(日志限制)、`live-restore`(升级不重启容器)、`registry-mirrors`(镜像源)。 |
| `live-restore` 解决什么? | dockerd 重启时容器不重启,由 containerd-shim 接管;升级 Docker 不影响业务。 |
| Mac 上为什么文件挂载慢? | 文件从 macOS 通过 VirtioFS 跨越 VM 边界到 LinuxKit 内核,延迟大;Linux 上原生无此问题。 |
| `docker context` 干什么用? | 切换多个 daemon(本地 / 远程 / 不同集群),无需改环境变量。 |
| K8s 节点上 dockerd 必须配什么? | `native.cgroupdriver=systemd`,与 kubelet 一致,否则指标拿不到。 |
| Docker Desktop 商业授权变了什么? | 2021 起对 250+ 员工或 1000 万+ 营收企业收费,推动 Rancher/Podman Desktop 兴起。 |
| `docker run` 与 `docker create` 区别? | `create` 只创建不启动,`run` = `create` + `start`。 |

---

## 11. 综合面试题

### 题 1(基础)
**问**:在 Ubuntu 上 `apt install docker.io` 装的 Docker 和官方仓库装的有何区别?

**答题要点**:
- `docker.io` 是 Ubuntu 官方源,版本旧、更新慢
- 官方 docker-ce 是 Docker 公司维护,版本新、Bug 修复及时
- 工业环境一律用官方源,锁版本 `docker-ce=5:24.0.*`
- 卸载时包名不同(docker.io vs docker-ce)

### 题 2(原理)
**问**:为什么把用户加入 docker 组等同给 root 权限?

**答题要点**:
- docker 组用户可读写 `/var/run/docker.sock`,即可控制 dockerd
- dockerd 以 root 运行,可创建任意容器
- 攻击方式:`docker run -v /:/host -it alpine chroot /host`
- 即获得宿主机 root shell
- 缓解:用 rootless mode、AppArmor、限制 docker 命令

### 题 3(配置)
**问**:线上节点磁盘使用率 95%,如何排查 Docker 占用?

**答题要点**:
- `docker system df` 看镜像 / 容器 / 卷 / 缓存占用
- `docker system df -v` 看每个对象的具体大小
- 检查 `/var/lib/docker/overlay2`(镜像层)、`/var/lib/docker/containers`(日志)、`/var/lib/docker/volumes`(卷)
- `du -sh /var/lib/docker/*` 找最大目录
- 清理:`docker system prune -a --volumes`(慎用,会删未使用的卷)
- 长期:`daemon.json` 加 `log-opts` 限制,定时清理脚本

### 题 4(故障)
**问**:升级 Docker 后所有容器重启了,业务受影响。如何避免?

**答题要点**:
- 启用 `live-restore: true`(在 daemon.json)
- 原理:容器父进程是 containerd-shim 而非 dockerd,dockerd 重启不影响 shim
- 升级流程:滚动升级,每次只升一个节点
- K8s 场景:cordon + drain + 升级 + uncordon
- 极端情况:升级 containerd / runc 仍需重启容器(因为 shim 依赖它们)

### 题 5(架构)
**问**:Docker Desktop 在 Mac 上是如何跑 Linux 容器的?

**答题要点**:
- macOS 无 Linux 内核,Desktop 用 Hypervisor.framework 跑 LinuxKit VM
- LinuxKit 是轻量 Linux,内含 dockerd + containerd + 内核
- CLI 在 macOS 原生,通过 socket 转发到 VM 内的 dockerd
- 文件挂载通过 VirtioFS 从 macOS 转发到 VM
- 网络通过 VPNKit 转发
- 因此有跨界开销,不适合生产

### 题 6(工业)
**问**:1000 台服务器批量装 Docker,如何保证一致与可回滚?

**答题要点**:
- 用 Ansible / Salt / Puppet 等配置管理工具
- 锁版本:`docker-ce=5:24.0.7-1~ubuntu.22.04~jammy`
- daemon.json 用模板渲染(j2),变量化 registry / data-root
- 灰度:先 1% 节点,观察 24 小时,再 10%,再全量
- 回滚:Ansible playbook 支持降级到上一版本
- 验证:安装后跑 hello-world + 节点健康检查
- 监控:节点加入集群后,看容器启动成功率

### 题 7(优化)
**问**:`docker pull` 在大集群(500+ 节点)同时拉镜像很慢,如何优化?

**答题要点**:
- 短期:`max-concurrent-downloads` 调高(默认 3 → 20)
- 用 P2P 分发(Dragonfly / Kraken),带宽降 80%
- 镜像预热(提前 pull 到所有节点)
- 按需加载(Stargz / Nydus),不下载整个镜像
- 私有仓库多副本 + CDN
- 详见 [23-工业实战-镜像分发与CDN](./23-工业实战-镜像分发与CDN.md)

### 题 8(安全)
**问**:生产环境为什么不应该让所有用户都 `sudo docker`?

**答题要点**:
- docker 组 = root(见题 2)
- 应该:用 RBAC 限制谁能用 docker
- 或:rootless mode,每用户独立 daemon
- 或:用 K8s + namespace 隔离,不给 docker 直接权限
- 审计:开启 dockerd audit log,记录所有 API 调用

### 题 9(跨平台)
**问**:Windows 上跑 Linux 容器,底层是什么?

**答题要点**:
- Windows 11 / Server 2019+ 用 WSL 2
- WSL 2 是轻量 Hyper-V VM,跑真 Linux 内核
- Docker Desktop 把 dockerd 装在 docker-desktop WSL distro 里
- 容器实际跑在 WSL 2 内核中,不在 Windows 内核
- Windows 容器(原生)是另一回事,用 Job Object + silo,极少用

### 题 10(综合)
**问**:设计一个 Docker 安装与配置基线规范,适用于 500 节点集群。

**答题要点**:
- 版本:Docker CE 24.x LTS,锁补丁版本
- 安装:Ansible 统一部署,从内网源安装
- daemon.json:
  - data-root 指向独立大盘(XFS / ext4)
  - log-opts 限制(100m × 10)
  - live-restore: true
  - cgroupdriver: systemd
  - registry-mirrors 指向内网 Harbor
  - max-concurrent-downloads: 20
  - nofile 上限 65 万
- 内核:调高 inotify / conntrack / PID 上限
- 监控:Prometheus + node-exporter + cAdvisor
- 日志:fluentd 采集 docker logs 到 Loki
- 安全:不允许 docker 组,用 K8s RBAC
- 升级:每月一次,滚动,先 drain
- 审计:开启 auditd 记录 docker.sock 调用

---

## 12. 故障复盘

### 案例 1:daemon.json 语法错误导致节点失联

**现象**:某运维修改 100 台节点的 `daemon.json`(漏了一个逗号),`systemctl restart docker` 后 dockerd 起不来,K8s 节点 NotReady。

**根因**:
- JSON 不允许尾逗号,运维加了一行配置后忘记删上一个逗号
- dockerd 启动失败,systemd 重试 5 次后放弃
- 节点上所有容器虽在跑(live-restore 救了一命),但无法管理

**修复**:
```bash
# 用 jq 验证
jq . /etc/docker/daemon.json

# 或用 dockerd --validate
dockerd --validate --config-file /etc/docker/daemon.json
```

**防范**:
- Ansible 模板渲染后用 `jq .` 校验
- CI 中加 JSON lint
- `systemctl restart docker` 后 `systemctl is-active docker` 验证
- 灰度:一次只改一个节点,观察后再全量

### 案例 2:`data-root` 不在独立盘导致磁盘爆满

**现象**:某公司节点 `/var/lib/docker` 在系统盘(50 GB),运行两周后磁盘 100%,节点不可用。

**根因**:
- 默认 `data-root=/var/lib/docker`,与系统盘共用
- 镜像 + 容器 + 卷 + 日志累积,50 GB 一周就满

**修复**:
- 挂载独立数据盘(`/dev/vdb` → `/data`)
- `daemon.json` 改 `data-root: /data/docker`
- 迁移:`rsync -aP /var/lib/docker/ /data/docker/`
- 重启 dockerd

**防范**:
- 节点初始化时强制挂载数据盘
- Ansible playbook 检查 `data-root` 不在系统盘
- 监控磁盘使用率,80% 报警
- 定时 `docker system prune`

### 案例 3:`max-concurrent-downloads` 默认值导致大集群拉镜像超时

**现象**:某公司 500 节点集群同时拉一个 2 GB 镜像,部分节点超时失败,Pod 起不来。

**根因**:
- `max-concurrent-downloads` 默认 3,同时下载层数有限
- 500 节点同时打 Harbor,Harbor 带宽打满
- 部分节点拉取超时(默认 5 分钟)

**修复**:
- 短期:Harbor 加带宽、加副本
- 中期:部署 Dragonfly P2P 分发
- 长期:镜像预热 + 按需加载(Nydus)
- 节点侧:`max-concurrent-downloads: 20`

**防范**:
- 大集群必须有 P2P 分发方案
- 镜像分层尽量复用基础镜像(减少层数)
- 监控 Harbor 带宽与延迟

### 案例 4:Windows 路径挂载导致开发环境诡异 Bug

**现象**:某开发者在 Windows 上 `docker run -v C:\Users\me\app:/app node:20 npm install`,容器内 `/app` 出现神秘的 `Microsoft.VCLibs` 文件,`npm install` 失败。

**根因**:
- Windows 路径分隔符 `\` 在 Linux 容器内被解释为转义
- Docker Desktop 把 `C:\Users\me\app` 错误转换,挂载到了别的目录
- 容器内 `/app` 实际是 Windows 系统目录

**修复**:
```bash
# 用正斜杠
docker run -v /c/Users/me/app:/app node:20 npm install

# 或 PowerShell 用 ${PWD}
docker run -v ${PWD}:/app node:20 npm install
```

**防范**:
- 团队统一用 WSL 2 终端(路径是 Linux 风格)
- 文档里强调 Windows 路径注意事项
- CI 用 Linux,避免 Windows 路径问题

---

## 13. 参考与延伸

### 官方文档

- Install Docker Engine — https://docs.docker.com/engine/install/
- Install Docker Desktop — https://docs.docker.com/desktop/
- daemon.json reference — https://docs.docker.com/engine/reference/commandline/dockerd/#daemon-configuration-file
- CLI reference — https://docs.docker.com/engine/reference/commandline/cli/

### 大厂实践

- Alibaba ACK 节点配置最佳实践 — https://help.aliyun.com/zh/ack/
- Google Container-Optimized OS — https://cloud.google.com/container-optimized-os/docs
- AWS Bottlerocket — https://bottlerocket.dev/

### 相关模块

- [01-基础与核心概念](./01-基础与核心概念.md) — 上一章
- [03-镜像原理与Dockerfile](./03-镜像原理与Dockerfile.md) — 下一章
- [04-容器运行与生命周期](./04-容器运行与生命周期.md) — run 全参数
- [12-安全与隔离](./12-安全与隔离.md) — rootless 与权限
- [23-工业实战-镜像分发与CDN](./23-工业实战-镜像分发与CDN.md) — 大集群拉镜像优化
- [infra开发](../infra开发/) — 节点初始化与配置管理

---

> **下一章**:[03-镜像原理与Dockerfile](./03-镜像原理与Dockerfile.md)
