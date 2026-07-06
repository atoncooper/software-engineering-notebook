# 18 - WebAssembly 部署

> WebAssembly (WASM) 是继容器之后的下一代部署单元: 毫秒级冷启动、跨平台、内存安全、近原生性能。WASI 标准让它跳出浏览器成为通用运行时。本章梳理 WASM 运行时谱系、WASI 演进、组件模型、Spin/Wasm-edge 框架, 以及在边缘函数/AI Agent 工具/Plugin 系统/Serverless 的工业实践。

---

## 一、思维导图

```
                  WebAssembly 部署
                        │
        ┌───────────────┼───────────────┐
        ▼               ▼               ▼
   ┌─────────┐     ┌─────────┐     ┌─────────┐
   │ 运行时  │     │ WASI    │     │ 应用    │
   │ Wasmtime│     │ Preview1│     │ Spin    │
   │ Wasm-   │     │ Preview2│     │ Wasmer  │
   │ edge    │     │ 组件    │     │ Extism  │
   │ WAMR    │     │ 模型    │     │ Plugin  │
   └─────────┘ └─────────┘     └─────────┘
```

---

## 二、问题定义与边界

### 2.1 解决什么

- **毫秒级冷启动**: Serverless/边缘函数
- **跨平台**: 一次编译, 到处运行 (Linux/Win/macOS/嵌入式)
- **内存安全**: 沙箱隔离, 无内存安全漏洞
- **Plugin 系统**: 安全加载第三方代码
- **AI Agent 工具**: 沙箱化 LLM 生成的代码

### 2.2 不解决什么

- 不覆盖 MicroVM (17 章, 强隔离)
- 不覆盖容器基础 (Docker 模块)
- 不深入 Rust/Go 语言特性

---

## 三、直觉解释

### 3.1 为什么需要 WASM

```
容器 (Docker):
  - 启动: 50ms-1s
  - 内存: 1-110MB
  - 隔离: namespaces+cgroups (弱) 或 MicroVM (强但慢)
  - 跨平台: 仅 Linux 容器
  - 镜像大小: MB-GB

MicroVM (Firecracker):
  - 启动: 125ms
  - 内存: 5-110MB
  - 隔离: 强 (硬件虚拟化)
  - 跨平台: Linux + KVM
  - 镜像: kernel + rootfs (50MB+)

WASM:
  - 启动: < 1ms
  - 内存: < 1MB
  - 隔离: 内存安全 (沙箱)
  - 跨平台: 任意 OS, 任意 CPU
  - 模块大小: KB-MB
```

### 3.2 WASM vs 容器 vs MicroVM

| 维度 | WASM | 容器 (runc) | MicroVM |
|------|------|-----------|---------|
| 启动 | < 1ms | 50ms | 125ms-2s |
| 内存 | < 1MB | 1MB | 5-110MB |
| 隔离 | 内存安全 | 弱 (共享内核) | 强 (硬件) |
| 跨平台 | 是 (任意 OS/CPU) | 否 (Linux) | 否 (Linux+KVM) |
| 性能 | 接近原生 (JIT) | 原生 | 接近原生 |
| 兼容性 | WASM 生态 | 完整 Linux | 完整 Linux |
| 镜像大小 | KB-MB | MB-GB | 50MB+ |

### 3.3 适用场景

```
适合 WASM:
  - 边缘函数 (Cloudflare Workers, Fastly Compute)
  - Serverless 短任务 (轻量, 快启动)
  - Plugin 系统 (安全加载第三方)
  - AI Agent 工具 (LLM 生成代码沙箱)
  - 跨平台 CLI (Rust 编译 WASM)

不适合:
  - 长任务/重计算 (容器更适合)
  - 需要完整 Linux 生态 (依赖 .so 库)
  - GPU 直通 (WASM GPU 支持有限)
  - 大数据 IO (WASI 性能不如原生)
```

---

## 四、核心概念与架构

### 4.1 WASM 运行时架构

```
┌─────────────────────────────────────────────┐
│              Host Process                   │
│  ┌──────────────────────────────────────┐   │
│  │         WASM Runtime                 │   │
│  │  ┌────────────┐  ┌────────────────┐  │   │
│  │  │ Module     │  │ Instance       │  │   │
│  │  │ (字节码)   │  │ (运行实例)     │  │   │
│  │  └────────────┘  └────────────────┘  │   │
│  │  ┌────────────────────────────────┐  │   │
│  │  │ JIT/AOT Compiler               │  │   │
│  │  │ (Cranelift/LLVM)               │  │   │
│  │  └────────────────────────────────┘  │   │
│  │  ┌────────────────────────────────┐  │   │
│  │  │ WASI Implementation            │  │   │
│  │  │ - 文件 IO                      │  │   │
│  │  │ - 网络                         │  │   │
│  │  │ - 时钟                         │  │   │
│  │  │ - 随机数                       │  │   │
│  │  └────────────────────────────────┘  │   │
│  └──────────────────────────────────────┘   │
└─────────────────────────────────────────────┘
```

### 4.2 WASI 演进

```
WASI Preview 1 (wasi_snapshot_preview1):
  - 基础文件 IO (fd_read, fd_write, fd_open)
  - 简单网络 (sock_accept, sock_send, sock_recv)
  - 时钟 (clock_time_get)
  - 随机数 (random_get)
  - 限制: 文件路径需预开放, 网络仅 TCP accept

WASI Preview 2 (2023+):
  - 基于组件模型 (Component Model)
  - 接口类型 (Interface Types): 跨语言互操作
  - 异步支持 (async functions)
  - 网络自由 (无 accept 限制)
  - 模块间组合 (compose)

组件模型 (Component Model):
  - WASM Module → Component (导出接口)
  - Component 间通过接口类型通信
  - 不共享内存, 仅数据交换
  - 类似微服务, 但进程内

未来 (WASI 0.2+):
  - 完整异步
  - GPU/NN 加速 (wasi-nn, wasi-gpu)
  - 文件系统细粒度权限
  - 原生线程
```

### 4.3 WASI 接口示例

```wit
// WIT (WASM Interface Type) 定义接口
// example.wit
package example:api;

interface calculator {
    add: func(a: f64, b: f64) -> f64;
    multiply: func(a: f64, b: f64) -> f64;
}

interface database {
    record person {
        name: string,
        age: u32,
    }

    get-person: func(id: u32) -> option<person>;
    save-person: func(p: person) -> result<u32, string>;
}

world example-app {
    import database;
    export calculator;
}
```

```rust
// Rust 实现
use wasi::http::{types::*, outgoing_handler::OutgoingHandler};

// 实现 calculator 接口
#[wasmtypes::component]
impl calculator::Calculator for MyCalculator {
    fn add(&self, a: f64, b: f64) -> f64 {
        a + b
    }
    fn multiply(&self, a: f64, b: f64) -> f64 {
        a * b
    }
}
```

### 4.4 Spin 框架（Fermyon）

```toml
# spin.toml
spin_manifest_version = "2"
name = "llm-tool"
version = "1.0.0"

[[trigger.http]]
route = "/generate"
component = "llm-generate"

[component.llm-generate]
source = "target/wasm32-wasi/release/llm_generate.wasm"
allowed_outbound_hosts = ["https://api.openai.com"]
[component.llm-generate.build]
command = "cargo build --target wasm32-wasi --release"

[[trigger.http]]
route = "/embed"
component = "embed"

[component.embed]
source = "target/wasm32-wasi/release/embed.wasm"
key_value_stores = ["default"]
```

```rust
// src/main.rs (Rust + Spin SDK)
use spin_sdk::{
    http::{Request, Response},
    http_component,
    key_value::Store,
};

#[http_component]
fn generate(req: Request) -> Response {
    let body = req.body();
    let prompt = std::str::from_utf8(body).unwrap();

    // 1. 调用 LLM API
    let resp = spin_sdk::http::send(
        Request::post("https://api.openai.com/v1/chat/completions")
            .header("Authorization", "Bearer sk-xxx")
            .body(format!(r#"{{"model":"gpt-4","prompt":"{}"}}"#, prompt))
    ).unwrap();

    // 2. 缓存到 KV store
    let mut store = Store::open("default").unwrap();
    store.set("last_prompt", prompt.as_bytes()).unwrap();

    // 3. 返回
    Response::new(200)
        .body(resp.into_body())
}
```

### 4.5 Wasm-edge 应用

```yaml
# Wasm-edge + Docker 集成
# Dockerfile
FROM wasmedge/slim-runtime:latest
COPY app.wasm /app.wasm
CMD ["wasmedge", "/app.wasm"]
```

```bash
# 直接运行
wasmedge --env "OPENAI_KEY=sk-xxx" app.wasm

# 与 Docker 集成 (containerd + runwasi)
docker run --runtime=io.containerd.wasmtime.v1 my-app:latest
```

---

## 五、操作流程与配置

### 5.1 Rust → WASM 编译

```bash
# 1. 安装 wasm32-wasi target
rustup target add wasm32-wasi

# 2. 创建项目
cargo new wasm-app
cd wasm-app

# 3. 编译
cargo build --target wasm32-wasi --release

# 4. 运行
wasmedge target/wasm32-wasi/release/wasm_app.wasm
# 或
wasmtime target/wasm32-wasi/release/wasm_app.wasm
```

```rust
// Cargo.toml
[package]
name = "wasm-app"
version = "0.1.0"
edition = "2021"

[dependencies]
wasi = "0.13"
serde = { version = "1", features = ["derive"] }
serde_json = "1"

[lib]
crate-type = ["cdylib"]  # 编译为动态库 (WASM 模块)
```

### 5.2 部署到 Cloudflare Workers

```toml
# wrangler.toml
name = "llm-router"
main = "src/index.ts"
compatibility_date = "2024-01-01"

[vars]
DEFAULT_MODEL = "llama-3.1-70b"

[[kv_namespaces]]
binding = "CONFIG"
id = "your-kv-namespace-id"
```

```typescript
// src/index.ts
export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);

    if (url.pathname === "/generate") {
      const { prompt } = await request.json();
      const model = env.DEFAULT_MODEL;

      // 调用 LLM API
      const llmResp = await fetch("https://api.openai.com/v1/chat/completions", {
        method: "POST",
        headers: {
          "Authorization": `Bearer ${env.OPENAI_KEY}`,
          "Content-Type": "application/json"
        },
        body: JSON.stringify({ model, prompt })
      });

      return new Response(llmResp.body, { status: 200 });
    }

    return new Response("Not Found", { status: 404 });
  }
};
```

```bash
# 部署
npx wrangler deploy
```

### 5.3 部署到 Fastly Compute@Edge

```rust
// src/main.rs
use fastly::http::{Method, Request, Response};
use fastly::{Body, Error};

#[fastly::main]
fn main(mut req: Request) -> Result<Response, Error> {
    if req.get_method() == Method::POST && req.get_path() == "/generate" {
        let body = req.take_body();
        let prompt = body.into_string();

        // 调用 LLM
        let mut beresp = Request::post("https://api.openai.com/v1/chat/completions")
            .with_header("Authorization", "Bearer sk-xxx")
            .with_body(prompt)
            .send("openai_backend")?;

        return Ok(beresp);
    }

    Ok(Response::new(Body::from("Not Found"), 404))
}
```

```toml
# fastly.toml
name = "llm-router"
language = "rust"

[backends]
  [backends.openai_backend]
  url = "https://api.openai.com"
```

### 5.4 K8s + runwasi (containerd 集成)

```yaml
# containerd 配置
# /etc/containerd/config.toml
[plugins."io.containerd.grpc.v1.cri".containerd.runtimes.wasm]
  runtime_type = "io.containerd.wasmtime.v1"
```

```yaml
# Pod 使用 WASM
apiVersion: v1
kind: Pod
metadata:
  name: wasm-app
spec:
  runtimeClassName: wasm
  containers:
    - name: app
      image: registry/wasm-app:latest  # 镜像里只有 .wasm 文件
```

```dockerfile
# Dockerfile (WASM 镜像)
FROM scratch
COPY app.wasm /app.wasm
ENTRYPOINT ["app.wasm"]
```

### 5.5 Extism Plugin 系统

```python
# Python 应用加载 WASM Plugin
from extism import Plugin, Function

# 加载 WASM 插件
plugin = Plugin(
    wasm_file="plugin.wasm",
    config={
        "api_key": "sk-xxx",
        "model": "gpt-4"
    },
    functions=[
        Function("log", lambda args: print(args[0]))
    ],
    allowed_hosts=["api.openai.com"],
    allowed_paths={"/data": "./data"}
)

# 调用插件函数
result = plugin.call("process_prompt", b"Hello, world!")
print(result.decode())
```

```rust
// plugin.rs (Rust + Extism PDK)
use extism_pdk::*;

#[plugin_fn]
pub fn process_prompt(input: String) -> FnResult<String> {
    let config = config::get("api_key")?;
    let model = config::get("model")?;

    // 调用 LLM
    let resp = http::request::<String>(
        &HttpRequest::new("https://api.openai.com/v1/chat/completions")
            .with_header("Authorization", &format!("Bearer {}", config))
            .with_method("POST"),
        &format!(r#"{{"model":"{}","prompt":"{}"}}"#, model, input)
    )?;

    Ok(resp.body())
}
```

---

## 六、底层原理

### 6.1 WASM 字节码与执行

```
WASM 模块结构:
  - Magic: 0x00 0x61 0x73 0x6d ('\0asm')
  - Version: 0x01 0x00 0x00 0x00
  - Sections:
    - Type: 函数签名
    - Import: 外部依赖
    - Function: 函数索引
    - Table: 间接调用表
    - Memory: 线性内存
    - Global: 全局变量
    - Export: 导出函数
    - Code: 函数体 (字节码)
    - Data: 初始化数据

执行方式:
  - 解释执行: 慢, 启动快
  - JIT (Cranelift): 热点编译, 接近原生
  - AOT (预编译): 启动快, 部署慢

Wasmtime JIT 流程:
  1. 加载 WASM 模块 (解析字节码)
  2. 验证 (类型检查, 安全检查)
  3. 编译为本地机器码 (Cranelift)
  4. 实例化 (分配内存, 设置导入)
  5. 执行 (调用导出函数)
```

### 6.2 WASM 安全模型

```
WASM 沙箱:
  1. 线性内存 (Linear Memory):
     - 独立内存空间, 不能越界访问
     - 无法访问宿主内存
     - 内存访问由运行时检查
  
  2. 不可达指令:
     - 验证阶段确保没有非法跳转
     - 函数返回类型必须匹配
  
  3. 资源限制:
     - 内存上限 (memory.max)
     - 表大小上限
     - 执行燃料 (fuel metering)

  4. WASI 权限:
     - 文件访问需预开放 (preopens)
     - 网络访问需白名单 (allowed_hosts)
     - 环境变量需显式传入

安全保证:
  - 内存安全 (无 buffer overflow)
  - 控制流安全 (无 ROP)
  - 类型安全 (无 type confusion)
  - 资源限制 (无 DoS)
```

### 6.3 组件模型（Component Model）

```
传统 WASM Module:
  - 共享线性内存
  - 函数调用通过 import/export
  - 跨语言互操作困难 (ABI 不统一)

组件模型:
  - Component: 接口导出单元
  - 接口类型 (Interface Types): 高级类型 (string, record, variant)
  - 不共享内存, 仅数据交换
  - 跨语言组合 (Rust + JS + Python)

组件示例:
  Component A (Rust): 实现 LLM 路由
  Component B (JS): 实现 prompt 模板
  Component C (Python): 实现日志

  组合: Host 加载三个 Component, 通过 WIT 接口互调
  无需重写, 各语言独立编译, 运行时组合

价值:
  - 复用: 各语言生态库 (Rust 性能, JS 易用, Python ML)
  - 解耦: 接口稳定, 实现可替换
  - 安全: 不共享内存, 边界清晰
```

### 6.4 性能基准

```
启动时间 (Hello World):
  - WASM (Wasmtime): 0.5ms
  - WASM (Wasm-edge): 0.3ms
  - Container (runc): 50ms
  - MicroVM (Firecracker): 125ms
  - JVM (Java): 500ms

内存开销 (空载):
  - WASM: 0.1-0.5MB
  - Container: 1MB
  - MicroVM (Firecracker): 5-8MB
  - JVM: 50-100MB

计算性能 (相对原生):
  - WASM (JIT): 95-105% (部分场景更快, 因 SIMD)
  - Container: 100%
  - JVM: 90-100%
  - Python: 5-50%

文件 IO (相对原生):
  - WASM (WASI): 70-90%
  - Container: 100%
  - 跨平台开销小

网络吞吐:
  - WASM: 接近原生 (95%+)
  - Container: 原生
```

---

## 七、代码与配置示例

### 7.1 AI Agent 工具沙箱（WASM）

```rust
// agent_tool.rs (Rust 编译为 WASM)
use extism_pdk::*;
use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize)]
struct ToolInput {
    code: String,
    language: String,
}

#[derive(Serialize, Deserialize)]
struct ToolOutput {
    stdout: String,
    stderr: String,
    exit_code: i32,
}

#[plugin_fn]
pub fn execute_code(input: ToolInput) -> FnResult<ToolOutput> {
    // 1. 检查代码安全 (静态分析)
    if input.code.contains("system(") || input.code.contains("exec(") {
        return Ok(ToolOutput {
            stdout: String::new(),
            stderr: "Forbidden API".to_string(),
            exit_code: -1,
        });
    }

    // 2. 在 WASM 沙箱内执行 (无文件系统, 无网络)
    let result = match input.language.as_str() {
        "python" => execute_python(&input.code)?,
        "javascript" => execute_js(&input.code)?,
        "sql" => execute_sql(&input.code)?,
        _ => return Err(PluginError::Msg("Unsupported language".to_string())),
    };

    Ok(result)
}

fn execute_python(code: &str) -> FnResult<ToolOutput> {
    // 调用 Python 解释器 (作为 Component)
    let py_result: String = host_fn("python_exec", code)?;
    let parsed: ToolOutput = serde_json::from_str(&py_result)?;
    Ok(parsed)
}
```

```python
# agent_server.py (Python 宿主, 加载 WASM 工具)
from extism import Plugin
import json

class AgentToolSandbox:
    """加载 WASM 工具, 沙箱执行 LLM 生成的代码."""

    def __init__(self):
        self.plugin = Plugin(
            wasm_file="agent_tool.wasm",
            allowed_hosts=[],  # 无网络
            allowed_paths={},  # 无文件系统
            config={}
        )

    def execute(self, code: str, language: str) -> dict:
        """执行 LLM 生成的代码, 强隔离."""
        input_data = json.dumps({"code": code, "language": language})
        result = self.plugin.call("execute_code", input_data.encode())
        return json.loads(result.decode())

    def close(self):
        self.plugin.close()


# 在 Agent 中使用
sandbox = AgentToolSandbox()
result = sandbox.execute("print('hello')", "python")
# 即使 LLM 生成恶意代码, 也无法逃逸 WASM 沙箱
```

### 7.2 边缘 LLM Router（Cloudflare Workers）

```typescript
// src/index.ts (Cloudflare Workers)
export interface Env {
  OPENAI_KEY: string;
  ANTHROPIC_KEY: string;
  CONFIG: KVNamespace;
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);

    if (url.pathname !== "/v1/chat/completions") {
      return new Response("Not Found", { status: 404 });
    }

    const body = await request.json();
    const { messages, model } = body;

    // 1. 路由策略 (从 KV 读取)
    const routing = await env.CONFIG.get("routing_strategy") || "openai";
    let targetModel = model;
    let apiUrl: string;
    let apiKey: string;

    if (routing === "openai") {
      apiUrl = "https://api.openai.com/v1/chat/completions";
      apiKey = env.OPENAI_KEY;
      targetModel = model || "gpt-4";
    } else if (routing === "anthropic") {
      apiUrl = "https://api.anthropic.com/v1/messages";
      apiKey = env.ANTHROPIC_KEY;
      targetModel = model || "claude-3-5-sonnet";
    } else {
      return new Response("Invalid routing", { status: 400 });
    }

    // 2. 转发请求 (流式)
    const upstream = await fetch(apiUrl, {
      method: "POST",
      headers: {
        "Authorization": `Bearer ${apiKey}`,
        "Content-Type": "application/json"
      },
      body: JSON.stringify({ ...body, model: targetModel })
    });

    // 3. 流式返回
    return new Response(upstream.body, {
      status: upstream.status,
      headers: { "Content-Type": "text/event-stream" }
    });
  }
};
```

### 7.3 Plugin 系统（WASM 组件模型）

```wit
// plugin.wit (接口定义)
package llm-platform:plugin;

interface llm-tool {
    record tool-input {
        code: string,
        args: list<string>,
    }

    record tool-output {
        result: string,
        error: option<string>,
    }

    execute: func(input: tool-input) -> result<tool-output, string>;
    info: func() -> string;
}

world llm-plugin {
    export llm-tool;
}
```

```rust
// Rust 实现
wit_bindgen::generate!({"path": "plugin.wit"});

struct MyTool;

impl llm_tool::LlmTool for MyTool {
    fn execute(input: llm_tool::ToolInput) -> Result<llm_tool::ToolOutput, String> {
        // 实现
        Ok(llm_tool::ToolOutput {
            result: format!("Executed: {}", input.code),
            error: None,
        })
    }

    fn info() -> String {
        "My Tool v1.0".to_string()
    }
}

export_llm_tool!(MyTool);
```

```python
# Python 宿主加载多个 Plugin
from extism import Plugin

class PluginManager:
    def __init__(self):
        self.plugins = {}

    def load(self, name: str, wasm_path: str):
        self.plugins[name] = Plugin(wasm_file=wasm_path)

    def execute(self, plugin_name: str, code: str, args: list):
        plugin = self.plugins[plugin_name]
        input_data = {"code": code, "args": args}
        result = plugin.call("execute", json.dumps(input_data).encode())
        return json.loads(result.decode())

# 加载多个插件
manager = PluginManager()
manager.load("calculator", "plugins/calculator.wasm")
manager.load("web_search", "plugins/web_search.wasm")
manager.load("code_runner", "plugins/code_runner.wasm")

# Agent 调用
result = manager.execute("calculator", "1 + 2", [])
```

### 7.4 Spin 微服务

```bash
# 创建 Spin 应用
spin templates install --git https://github.com/fermyon/spin
spin new http-rust llm-service

cd llm-service
# 编辑 src/lib.rs

# 构建并运行
spin build
spin up
```

```rust
// src/lib.rs
use spin_sdk::http::{Request, Response, IntoBody, Method};
use spin_sdk::http_component;

#[http_component]
fn handle(req: Request) -> Response {
    match req.method() {
        Method::POST => handle_post(req),
        _ => Response::new(405, "Method Not Allowed".into()),
    }
}

fn handle_post(req: Request) -> Response {
    let body: serde_json::Value = serde_json::from_slice(req.body()).unwrap();
    let prompt = body["prompt"].as_str().unwrap();

    // 调用 LLM API
    let llm_req = Request::post("https://api.openai.com/v1/chat/completions")
        .header("Authorization", "Bearer sk-xxx")
        .header("Content-Type", "application/json")
        .body(serde_json::json!({
            "model": "gpt-4",
            "messages": [{"role": "user", "content": prompt}]
        }).to_string())
        .unwrap();

    let llm_resp = spin_sdk::http::send(llm_req).unwrap();
    Response::new(200, llm_resp.into_body())
}
```

---

## 八、常见陷阱与调优

### 8.1 陷阱 1：WASI 兼容性

**症状**：部分 Rust crate 依赖系统 API, 编译到 WASM 失败。

**修复**：
- 用 `cargo build --target wasm32-wasi` 测试
- 替换不兼容 crate (如 `reqwest` → `spin-sdk::http`)
- 用 `wasi` crate 替代 `std::os`

### 8.2 陷阱 2：网络性能

**症状**：WASM 网络吞吐不及原生 50%。

**修复**：
- WASI Preview 2 网络性能更好
- Wasm-edge 用 wasmedge_wasi_socket (优化版)
- 避免高频小请求, 用 batch

### 8.3 陷阱 3：冷启动仍慢

**症状**：WASM 模块大 (10MB+), 启动 100ms+。

**修复**：
- 模块裁剪: `wasm-opt -Oz` 减小体积
- 移除调试信息: `wasm-strip`
- AOT 预编译: Wasm-edge `wasmedgec` 编译为 .so
- 模块拆分: 按需加载

### 8.4 陷阱 4：缺少 GPU 支持

**症状**：WASM 跑 LLM 推理, GPU 用不上。

**修复**：
- wasi-nn 接口 (WASM 神经网络)
- Wasm-edge + GGML plugin (LLM 推理)
- 或 WASM 仅做编排, 推理走外部 API

### 8.5 陷阱 5：调试困难

**症状**：WASM 错误堆栈不明, 难定位。

**修复**：
- 保留 debug info: `cargo build` 不加 `--release`
- Wasmtime: `--debug-logging`
- 用 console.log/print 输出调试
- DWARF 调试信息 (Wasmtime 支持)

### 8.6 调优 Checklist

- [ ] 模块裁剪: wasm-opt -Oz 减小体积
- [ ] AOT 预编译: Wasm-edge wasmedgec
- [ ] WASI Preview 2 (性能 + 功能)
- [ ] 网络白名单严格 (allowed_hosts)
- [ ] 文件权限最小 (preopens)
- [ ] 燃料限制 (fuel metering) 防 DoS
- [ ] 模块拆分按需加载
- [ ] 与容器/MicroVM 配合: WASM 跑轻任务, 容器跑重任务

---

## 九、工业案例与基准数据

### 9.1 案例 1：Cloudflare Workers

**背景**：Cloudflare 边缘函数平台。

**方案**：
- WASM 运行时 (V8 isolate + WASM)
- 全球 300+ 边缘节点
- 启动: < 5ms
- 内存: < 128MB

**规模**：
- 日均请求: 100M+
- 函数数: 1M+
- 冷启动: P99 < 5ms

### 9.2 案例 2：Fastly Compute@Edge

**背景**：Fastly 边缘计算平台。

**方案**：
- Wasmtime 运行时
- 启动: < 1ms (号称全球最快)
- 100+ POP (Point of Presence)

**效果**：
- 冷启动几乎无感
- 支持 Rust/C/Go/JavaScript

### 9.3 案例 3：Fermyon Spin

**背景**：Fermyon Spin Serverless WASM 框架。

**方案**：
- Spin 框架 (开源)
- 本地开发 + K8s 部署
- 内置 KV/SQL/Redis 等组件

**效果**：
- 启动: < 1ms
- 内存: < 1MB per app
- 适合: 微服务, 边缘应用

### 9.4 案例 4：Shopify Functions

**背景**：Shopify 电商平台, 商家自定义逻辑。

**方案**：
- 商家用 Rust/JS 写函数, 编译 WASM
- 在 Shopify 沙箱内运行
- 不影响主站稳定性

**规模**：
- 商家数: 100K+
- 函数调用: B+/day
- 安全: WASM 沙箱零逃逸

### 9.5 案例 5：Extism Plugin 平台

**背景**：Extism 跨语言 Plugin 系统。

**方案**：
- 任意语言编写 Plugin (Rust/Go/JS/Python)
- 编译 WASM, 跨平台加载
- 任意宿主 (Python/Go/Ruby/JS) 调用

**适用**：SaaS 扩展、IDE 插件、CI/CD Runner。

### 9.6 性能基准

| 平台 | 启动 | 内存 | 吞吐 | 适合 |
|------|------|------|------|------|
| Cloudflare Workers | 5ms | 128MB | 100M req/day | 边缘函数 |
| Fastly Compute@Edge | 1ms | 128MB | 100M req/day | 边缘计算 |
| Spin (Fermyon) | 1ms | 1MB | 10K req/s/instance | 微服务 |
| Wasmtime | 0.5ms | 0.5MB | 50K req/s/instance | 通用运行时 |
| Wasm-edge | 0.3ms | 0.3MB | 50K req/s/instance | 边缘 + AI |

---

## 十、与其他方案的关系

### 10.1 WASM vs 容器

| 场景 | 推荐 |
|------|------|
| 边缘函数 (低延迟) | WASM |
| Serverless 短任务 | WASM (或 Firecracker) |
| 长任务/重计算 | 容器 |
| 完整 Linux 生态依赖 | 容器 |
| Plugin 系统 | WASM |
| AI Agent 代码沙箱 | WASM (轻) 或 Firecracker (强) |
| 多租户强隔离 | MicroVM (更强) |

### 10.2 WASM + 容器混合

```
混合架构:
  - API 网关: WASM (Cloudflare Workers, 快启动)
  - 业务逻辑: 容器 (K8s, 完整生态)
  - AI 推理: 容器 + GPU (K8s)
  - Agent 工具: WASM (沙箱)
  - Plugin: WASM (跨语言)

价值:
  - WASM 解决: 启动/隔离/跨平台
  - 容器解决: 生态/重计算/GPU
  - 各取所长
```

### 10.3 WASM + MicroVM

```
组合方案:
  - MicroVM (Firecracker): 进程级强隔离
  - VM 内运行 WASM: 函数级隔离
  - 单 VM 跑多 WASM 函数 (高密度)

适用: 极致安全 + 高密度 Serverless
例: Fastly 内部架构推测
```

---

## 十一、面试速答

**Q1: WASM 比容器好在哪里?**

A: 1) 启动快 (< 1ms vs 50ms); 2) 内存小 (< 1MB vs 1MB+); 3) 跨平台 (任意 OS/CPU); 4) 内存安全 (沙箱); 5) 模块小 (KB-MB)。但容器在生态/重计算/GPU 更强, 两者互补。

**Q2: WASI 是什么? 为什么需要?**

A: WASI (WebAssembly System Interface) 是 WASM 的系统接口标准, 让 WASM 跳出浏览器访问文件/网络/时钟等。Preview 1 基础接口, Preview 2 基于组件模型支持异步/接口类型/网络自由。WASI 让 WASM 成为通用运行时, 不依赖浏览器。

**Q3: WASM 适合什么场景?**

A: 1) 边缘函数 (Cloudflare Workers, 快启动); 2) Serverless 短任务 (轻量); 3) Plugin 系统 (安全加载第三方); 4) AI Agent 工具沙箱 (LLM 生成代码隔离); 5) 跨平台 CLI (Rust + WASM)。不适合: 长任务/重计算/完整 Linux 生态依赖/GPU 直通。

**Q4: 组件模型解决什么问题?**

A: 传统 WASM Module 共享线性内存, 跨语言互操作困难 (ABI 不统一)。组件模型 (Component Model): 1) Component 接口导出单元; 2) 接口类型 (string/record/variant) 跨语言; 3) 不共享内存, 仅数据交换; 4) 跨语言组合 (Rust + JS + Python)。让 WASM 真正成为"语言无关的组件平台"。

**Q5: AI Agent 代码沙箱用 WASM 还是 Firecracker?**

A: 视场景: 1) WASM: 轻量 (<1MB), 快启动 (<1ms), 但仅支持 WASM 生态语言 (Rust/Go/JS 编译 WASM); 2) Firecracker: 强隔离 (硬件虚拟化), 完整 Linux, 但重 (5MB+), 慢 (125ms)。简单代码用 WASM (Python/JS), 复杂代码 (系统调用/网络) 用 Firecracker。

---

## 十二、综合面试题

### 题 1（中级）：设计基于 WASM 的边缘 LLM Router

**答题要点**：

1. **架构**:
   - 边缘节点: Cloudflare Workers / Fastly Compute@Edge
   - WASM 函数: 路由 + 鉴权 + 限流
   - 后端: 多个 LLM API (OpenAI/Anthropic/自建)

2. **功能**:
   - 路由: 按用户/地域/模型选后端
   - 鉴权: API Key 校验
   - 限流: 滑动窗口 (KV store)
   - 缓存: 常见 prompt 缓存 (KV)
   - 流式: SSE 转发

3. **性能**:
   - 启动: < 5ms
   - 内存: < 128MB
   - 转发延迟: < 10ms (除 LLM 本身)
   - 全球 300+ 节点, 就近接入

4. **可靠性**:
   - 多后端 failover
   - 健康检查
   - 熔断: 后端故障切备用

5. **配置**:
   - KV 存储路由规则
   - 灰度: 按用户分桶

### 题 2（高级）：基于 WASM 的 AI Agent Plugin 平台

**答题要点**：

1. **架构**:
   - Plugin SDK: Rust + Extism PDK
   - Plugin 注册中心: WASM 文件存储
   - Plugin 运行时: Extism + Wasmtime
   - Agent 框架: 调用 Plugin

2. **接口设计** (WIT):
   - execute(input) → output
   - info() → metadata
   - validate(input) → bool

3. **安全**:
   - 网络: 白名单 (allowed_hosts)
   - 文件: 仅指定路径 (preopens)
   - 资源: fuel metering 限制 CPU
   - 内存: 上限限制

4. **跨语言**:
   - Plugin: Rust/Go/JS/Python 编译 WASM
   - 宿主: 任意语言 (Extism SDK)

5. **加载流程**:
   - Agent 决定调用哪个 Plugin
   - Plugin Manager 加载 WASM (缓存)
   - 调用 execute, 接收结果
   - 销毁实例

6. **优势**:
   - 安全: WASM 沙箱, LLM 生成代码无法逃逸
   - 跨语言: 复用各生态库
   - 轻量: < 1MB per plugin
   - 快加载: < 1ms

---

## 十三、故障复盘

### 13.1 案例 1：WASM 模块过大导致冷启动慢

**背景**：2024 年某公司 Rust WASM 模块 20MB, 启动 200ms, 不及预期。

**根因**：未优化体积, 包含大量调试信息与未使用代码。

**修复**：
- `wasm-opt -Oz` 优化体积
- `wasm-strip` 移除调试信息
- `cargo build --release` 启用 LTO
- 体积: 20MB → 2MB, 启动: 200ms → 5ms

**防范**：WASM 模块上线前必须体积优化, 监控启动时间。

### 13.2 案例 2：WASI 兼容性导致编译失败

**背景**：2025 年某公司 Rust 项目依赖 `reqwest`, 编译 WASM 失败。

**根因**：`reqwest` 依赖系统 socket API, WASI Preview 1 不完整支持。

**修复**：
- 替换为 `spin-sdk::http` (Spin 框架)
- 或用 `wasi-http` (WASI Preview 2)
- 重写 HTTP 客户端

**防范**：选 crate 时检查 WASM 兼容性, 优先 `no_std` 或 `wasm32-wasi` 支持。

### 13.3 案例 3：Plugin 内存泄漏

**背景**：2024 年某公司 Plugin 系统, 长期运行内存持续增长。

**根因**：Plugin 实例未销毁, 内存累积。

**修复**：
- 每次调用后销毁实例 (`plugin.close()`)
- 或用实例池 + LRU 淘汰
- 监控内存, 高水位重启

**防范**：Plugin 实例生命周期管理, 避免泄漏。

### 13.4 案例 4：WASM 网络性能差

**背景**：2025 年某公司 WASM 路由器, 网络吞吐不及原生 30%。

**根因**：WASI Preview 1 网络性能差, 频繁小请求放大开销。

**修复**：
- 升级 WASI Preview 2 (网络优化)
- 改 batch 请求 (一次发多个)
- 或用 Wasm-edge (wasmedge_wasi_socket 优化)

**防范**：网络密集场景评估 WASI 版本与运行时, 必要时用容器。

### 13.5 案例 5：LLM 生成代码绕过沙箱

**背景**：2024 年某公司 Agent 工具用 WASM 沙箱, LLM 生成代码尝试 `system()` 调用, 失败但未拦截。

**根因**：仅依赖 WASM 沙箱, 未做静态检查, LLM 代码可能含恶意意图。

**修复**：
- WASM 内置静态检查 (检查 forbidden API)
- 限制执行燃料 (fuel metering)
- 网络/文件白名单
- 输出审核 (检查工具调用结果)

**防范**：WASM 沙箱 + 静态检查 + 资源限制 + 输出审核, 多层防御。

---

## 十四、参考与延伸

### 14.1 工具与运行时

- Wasmtime — https://wasmtime.dev/
- Wasm-edge — https://wasmedge.org/
- WAMR — https://github.com/bytecodealliance/wasm-micro-runtime
- Wasmer — https://wasmer.io/
- Spin — https://developer.fermyon.com/spin
- Extism — https://extism.org/
- Cloudflare Workers — https://workers.cloudflare.com/
- Fastly Compute@Edge — https://www.fastly.com/products/edge-compute

### 14.2 标准与规范

- WebAssembly Spec — https://webassembly.org/
- WASI Preview 2 — https://github.com/WebAssembly/WASI/blob/main/wasip2/README.md
- Component Model — https://github.com/WebAssembly/component-model
- WIT (WASM Interface Type) — https://component-model.bytecodealliance.org/design/wit.html
- wasi-nn — https://github.com/WebAssembly/wasi-nn

### 14.3 跨模块链接

- [17-微虚拟机与沙箱运行时](./17-微虚拟机与沙箱运行时.md) —— MicroVM vs WASM
- [09-Agent系统部署与沙箱](./09-Agent系统部署与沙箱.md) —— Agent 代码沙箱
- [12-边缘AI部署](./12-边缘AI部署.md) —— 边缘 WASM
- [11-冷启动优化与Scale-to-Zero](./11-冷启动优化与Scale-to-Zero.md) —— WASM 冷启动
