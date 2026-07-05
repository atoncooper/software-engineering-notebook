# §4 - 共享内存并行 · OpenMP

> 一句话定位:本章讲清 OpenMP 的 fork-join 执行模型、四种 schedule 策略、reduction 实现原理、NUMA first-touch 与 false sharing,让你在多核 CPU 上写出接近线性加速的代码。
>
> 标记:⭐高频 🔥工程重点 📜论文/标准 ⚠️易错点 🎓学术深度 🏭工业实战

---

## 〇、思维导图

```
                      OpenMP
                        │
       ┌────────────────┼────────────────┐
       │                │                │
   执行模型          编程构造         性能优化
       │                │                │
   fork-join        parallel for     NUMA first-touch
   线程团队          task / taskwait  OMP_PROC_BIND
   隐式 barrier      reduction        false sharing
       │            schedule         alignas(64)
       │            critical/atomic   padding
       │            barrier          cache-aware
       │                │                │
   内存模型          同步原语         实战案例
   relaxed          critical         矩阵向量乘
   flush            atomic           schedule 对比
                   lock             reduce 三步
```

---

## 一、问题定义

### 1.1 解决什么问题

多核 CPU 是最常见的并行硬件。OpenMP 是 C/C++/Fortran 的**共享内存并行标准**,通过编译制导(`#pragma`)把串行代码变并行,无需重写。

典型场景:
- 数值计算(矩阵、FFT、有限元)在 16-64 核服务器上加速;
- HPC 应用(气象、CFD、分子动力学);
- 机器学习 CPU 推理路径(数据预处理、batch 推理);
- 与 MPI 混合(节点内 OpenMP + 跨节点 MPI)。

### 1.2 与其他并行方案的对比

| 方案 | 优势 | 劣势 | 适用 |
|------|------|------|------|
| OpenMP | 简单(pragma)、可移植、incremental | 仅共享内存 | 单机多核 |
| pthread/std::thread | 灵活 | 手写同步易错 | 复杂并发逻辑 |
| TBB | 任务调度优 | API 重 | 不规则任务 |
| MPI | 跨节点 | 编程复杂 | 集群 |
| CUDA | GPU 海量并行 | 仅 GPU | GPU |

---

## 二、核心概念与术语

| 术语 | 含义 |
|------|------|
| fork-join | 主线程 fork 团队,并行执行,join 同步 |
| team | 一组线程,主线程为 master |
| region | parallel / workshare 等 OpenMP 区域 |
| workshare | `for` / `sections` / `single` / `task` |
| schedule | 循环迭代分配策略 |
| reduction | 跨线程归约变量 |
| firstprivate / lastprivate | 私有副本的初始化/最终回写 |
| critical | 互斥区 |
| atomic | 单语句原子操作 |
| barrier | 团队同步点 |
| flush | 内存屏障 |
| task | 动态任务(配合 taskwait) |
| depend | 任务依赖(in/out/inout) |
| proc_bind | 线程绑核策略 |

---

## 三、原理与机制

### 3.1 fork-join 执行模型 🔥

```
时间 →
主线程 ─┬─ fork ──┬── T1 ──┬─ join ──┬─ fork ──┬─ join ──
        │         ├── T2 ──┤         ├── T1 ──┤
        │         ├── T3 ──┤         ├── T2 ──┤
        │         └── T4 ──┘         ├── T3 ──┤
        │                                       └── T4 ──┘
        serial    parallel           serial    parallel    serial
```

**核心**:每个 `#pragma omp parallel` 区域 fork 一个线程团队,区域结束隐式 `barrier`(join)。串行部分只主线程执行。

### 3.2 parallel for 与 workshare

```cpp
#pragma omp parallel for
for (int i = 0; i < n; ++i) {
    c[i] = a[i] + b[i];
}
```

等价于:
```cpp
#pragma omp parallel
{
    #pragma omp for
    for (int i = 0; i < n; ++i) {
        c[i] = a[i] + b[i];
    }
}
```

⚠️ `for` 循环必须**可向量化**(无跨迭代依赖、明确迭代次数)。

### 3.3 schedule 策略 🎓

四种迭代分配策略:

```
n=16, P=4

static (默认):            static, chunk=4:
T0: 0 1 2 3               T0: 0 1 2 3
T1: 4 5 6 7               T1: 4 5 6 7
T2: 8 9 10 11             T2: 8 9 10 11
T3: 12 13 14 15           T3: 12 13 14 15

dynamic, chunk=4:         guided:
T0: 0 1 2 3 → 12 13 14 15  T0: 0-5 → 12-13
T1: 4 5 6 7 → ...          T1: 6-10 → 14
T2: 8 9 10 11              T2: 11 → 15
T3: ...
```

| 策略 | 分配 | 开销 | 适用 |
|------|------|------|------|
| static | 编译时固定 | 最小 | 迭代均衡 |
| static, k | 固定块大小 | 最小 | 均衡 + cache 友好 |
| dynamic, k | 运行时申请 | 中 | 迭代不均 |
| guided | 大块递减 | 中 | 不均 + 减少申请 |

🔥 **工程经验**:
- 90% 情况用 `static`(默认)或 `static, k`;
- 只在迭代成本差异大(如稀疏矩阵)时用 `dynamic`;
- `guided` 适合难以预估迭代成本。

### 3.4 reduction 实现原理 🎓

```cpp
#pragma omp parallel for reduction(+:sum)
for (int i = 0; i < n; ++i) sum += a[i];
```

**实际执行**:
1. 每线程创建私有 `sum_local = identity`(对 `+` 是 0);
2. 各线程用自己的 `sum_local` 累加,无竞争;
3. 区域结束,所有 `sum_local` 用 `+` 合并到全局 `sum`。

**支持的算子**:`+ - * & | ^ && || max min`。

🔥 **性能**:reduction 比用 `critical` 或 `atomic` 快 $O(\log P)$ 倍——树形合并 vs 串行化。

### 3.5 NUMA first-touch 🔥

物理页分配发生在**首次写**时(详见 §1)。OpenMP 程序若主线程初始化大数组,所有页都在主线程的 NUMA 节点,其他线程访问全远程。

**正确做法**:
```cpp
// ❌ 串行初始化
double* A = malloc(N * sizeof(double));
for (int i = 0; i < N; ++i) A[i] = 0;  // 全部在主线程 NUMA 节点

// ✅ 并行 first-touch
double* A = malloc(N * sizeof(double));
#pragma omp parallel for
for (int i = 0; i < N; ++i) A[i] = 0;  // 触摸线程决定 NUMA 位置
```

### 3.6 false sharing 详解

详见 §1。OpenMP 下典型场景:

```cpp
// ❌ 反模式
int counters[P];
#pragma omp parallel num_threads(P)
{
    int tid = omp_get_thread_num();
    for (int i = 0; i < N; ++i) counters[tid]++;  // 4 字节,但与邻居同缓存线
}

// ✅ 对齐
struct alignas(64) Counter { int v; };
Counter counters[P];
```

### 3.7 内存模型与 flush

OpenMP 内存模型是 **relaxed consistency**:线程私有视图 + `flush` 同步。

```cpp
// 经典双线程通信
// T0:                     T1:
data = 42;                 while (!ready) {}
#pragma omp flush(data)    #pragma omp flush(ready)
ready = 1;                 #pragma omp flush(data)
#pragma omp flush(ready)   print(data);  // 42
```

⚠️ 现代 OpenMP 推荐 `atomic` with `seq_cst` 或 `reduction`,减少手动 `flush`。

### 3.8 task 与 taskwait

```cpp
#pragma omp parallel
{
    #pragma omp single
    {
        #pragma omp task
        process(left);  // 异步任务
        #pragma omp task
        process(right);
        #pragma omp taskwait  // 等所有子任务
    }
}
```

适合**不规则并行**(递归、图遍历、while 循环)。详见 §3 工作偷取。

---

## 四、算法 / 流程

### 4.1 OpenMP 程序编写流程

```
1. 识别可并行循环/任务
2. 加 #pragma omp parallel for
3. 处理共享 vs 私有变量(private/firstprivate)
4. 用 reduction 处理归约
5. 选 schedule(static 默认)
6. 加 proc_bind 绑核
7. 测 scaling,调 chunk
```

### 4.2 三步 reduce 优化流程

```
Step 1: 朴素 atomicAdd(慢)
Step 2: 块内私有 + critical(中)
Step 3: reduction 子句(快,树形)
```

---

## 五、工业实现对照

| 能力 | OpenMP | Intel TBB | C++ std | pthread |
|------|--------|-----------|---------|---------|
| 循环并行 | `parallel for` | `parallel_for` | 无原生 | 手写 |
| 任务 | `task` | `task_group` | `async` | 手写 |
| 归约 | `reduction` | `parallel_reduce` | `reduce` (C++17) | 手写 |
| 绑核 | `proc_bind` | `task_arena` | 无 | `pthread_setaffinity` |
| 内存模型 | relaxed + flush | sequential consistency | std::memory_order | 弱有序 |
| 跨平台 | 是 | 是 | 是 | POSIX |

---

## 六、代码示例

### 6.1 最小可运行版(教学):矩阵向量乘

```cpp
// file: matvec.cpp
// 编译: g++ -O3 -fopenmp matvec.cpp -o mv
// 运行: ./mv
#include <vector>
#include <cstdio>
#include <omp.h>

int main() {
    const int n = 8192;
    std::vector<float> A(n*n), x(n), y(n, 0);
    for (int i = 0; i < n*n; ++i) A[i] = 0.001f;
    for (int i = 0; i < n; ++i) x[i] = 1.0f;
    
    double t0 = omp_get_wtime();
    #pragma omp parallel for
    for (int i = 0; i < n; ++i) {
        float s = 0;
        for (int j = 0; j < n; ++j) s += A[i*n + j] * x[j];
        y[i] = s;
    }
    double t1 = omp_get_wtime();
    
    double flops = 2.0 * n * n;
    printf("Time=%.2fms GFLOPS=%.1f threads=%d\n",
           (t1-t0)*1000, flops/(t1-t0)/1e9, omp_get_max_threads());
    return 0;
}
```

### 6.2 手把手:三种 schedule 对比

```cpp
// file: schedule_cmp.cpp
// 演示不同 schedule 在不均匀负载下的表现
// 编译: g++ -O3 -fopenmp schedule_cmp.cpp -o sc
#include <cmath>
#include <cstdio>
#include <omp.h>

double work(int i) {
    // 模拟不均匀负载:越大越慢
    double s = 0;
    int n = 100 + i * i / 100;
    for (int k = 0; k < n; ++k) s += std::sin(k) * std::cos(k);
    return s;
}

double bench(const char* name, int kind) {
    const int n = 1000;
    double sum = 0;
    double t0 = omp_get_wtime();
    
    if (kind == 0) {
        #pragma omp parallel for reduction(+:sum) schedule(static)
        for (int i = 0; i < n; ++i) sum += work(i);
    } else if (kind == 1) {
        #pragma omp parallel for reduction(+:sum) schedule(dynamic, 16)
        for (int i = 0; i < n; ++i) sum += work(i);
    } else {
        #pragma omp parallel for reduction(+:sum) schedule(guided)
        for (int i = 0; i < n; ++i) sum += work(i);
    }
    
    double t1 = omp_get_wtime();
    printf("%-25s %.2fms (sum=%.1f)\n", name, (t1-t0)*1000, sum);
    return t1 - t0;
}

int main() {
    printf("Threads: %d\n", omp_get_max_threads());
    bench("static (default)",      0);
    bench("dynamic, chunk=16",     1);
    bench("guided",                2);
    return 0;
}
```

**典型输出**(不均匀负载,16 线程):
```
static (default)          180ms
dynamic, chunk=16         95ms    ← 不均负载时快近 2 倍
guided                    102ms
```

🔥 **结论**:负载不均时 `dynamic` 显著优于 `static`。负载均匀时 `static` 最优(无调度开销)。

### 6.3 手把手:false sharing 实测

```cpp
// file: false_sharing.cpp
// 编译: g++ -O3 -fopenmp false_sharing.cpp -o fs
#include <atomic>
#include <chrono>
#include <cstdio>
#include <omp.h>

constexpr int N = 100'000'000;

// 反模式:计数器紧凑排列
struct BadCounters {
    std::atomic<int> c[64];  // 4B 间隔,同一缓存线
};

// 修复:每计数器独占缓存线
struct alignas(64) AlignedCounter { std::atomic<int> v{0}; };
struct GoodCounters {
    AlignedCounter c[64];
};

int main() {
    printf("Threads: %d, N=%d per thread\n", omp_get_max_threads(), N);
    
    // === Bad: false sharing ===
    {
        BadCounters b;
        double t0 = omp_get_wtime();
        #pragma omp parallel num_threads(8)
        {
            int tid = omp_get_thread_num();
            for (int i = 0; i < N; ++i)
                b.c[tid].fetch_add(1, std::memory_order_relaxed);
        }
        double t1 = omp_get_wtime();
        printf("Bad  (false sharing): %.0f ms\n", (t1-t0)*1000);
    }
    
    // === Good: cache aligned ===
    {
        GoodCounters g;
        double t0 = omp_get_wtime();
        #pragma omp parallel num_threads(8)
        {
            int tid = omp_get_thread_num();
            for (int i = 0; i < N; ++i)
                g.c[tid].v.fetch_add(1, std::memory_order_relaxed);
        }
        double t1 = omp_get_wtime();
        printf("Good (cache aligned): %.0f ms\n", (t1-t0)*1000);
    }
    return 0;
}
```

**典型输出**(8 线程):
```
Bad  (false sharing): 4500 ms
Good (cache aligned): 850 ms
```
5 倍差距来自 coherence traffic。

### 6.4 生产级版(工程):NUMA 感知 + 绑核 + reduction

```cpp
// file: numa_pi.cpp
// 用 Monte Carlo 估算 π,演示 NUMA first-touch + 绑核 + reduction
// 编译: g++ -O3 -fopenmp -lnuma numa_pi.cpp -o pi
// 运行: OMP_PROC_BIND=close OMP_PLACES=cores numactl --cpunodebind=0 ./pi
#include <cstdio>
#include <random>
#include <omp.h>

int main() {
    const long N = 1'000'000'000;
    long inside = 0;
    
    double t0 = omp_get_wtime();
    #pragma omp parallel reduction(+:inside)
    {
        // 每线程独立 RNG,避免竞争
        std::mt19937_64 rng(42 + omp_get_thread_num());
        std::uniform_real_distribution<double> dist(-1.0, 1.0);
        
        #pragma omp for schedule(static)
        for (long i = 0; i < N; ++i) {
            double x = dist(rng), y = dist(rng);
            if (x*x + y*y <= 1.0) inside++;
        }
    }
    double t1 = omp_get_wtime();
    
    double pi = 4.0 * inside / N;
    printf("π ≈ %.6f (true 3.141593)\n", pi);
    printf("Time: %.2fs, Threads: %d, Samples/s: %.1fM\n",
           t1-t0, omp_get_max_threads(), N/(t1-t0)/1e6);
    return 0;
}
```

🔥 **生产级要点**:
- `OMP_PROC_BIND=close`:线程紧凑绑核(同 socket 优先);
- `OMP_PLACES=cores`:每个 place 是一个物理核;
- `numactl --cpunodebind=0`:限定 NUMA 节点;
- `reduction(+:inside)`:树形归约,无竞争;
- 每线程独立 RNG,避免原子操作。

### 6.5 手把手:task 实现并行快速排序

```cpp
// file: parallel_qsort.cpp
// 编译: g++ -O3 -fopenmp parallel_qsort.cpp -o pqs
#include <vector>
#include <algorithm>
#include <cstdio>
#include <omp.h>

void parallel_qsort(int* a, int lo, int hi, int depth) {
    if (hi - lo < 1000) {
        std::sort(a + lo, a + hi);
        return;
    }
    int pivot = a[lo + (hi - lo) / 2];
    int* mid = std::partition(a + lo, a + hi,
        [pivot](int x) { return x < pivot; });
    if (depth < 6) {
        #pragma omp task shared(a)
        parallel_qsort(a, lo, mid - a, depth + 1);
        #pragma omp task shared(a)
        parallel_qsort(a, mid - a, hi, depth + 1);
        #pragma omp taskwait
    } else {
        parallel_qsort(a, lo, mid - a, depth + 1);
        parallel_qsort(a, mid - a, hi, depth + 1);
    }
}

int main() {
    const int n = 1 << 22;
    std::vector<int> a(n);
    for (int i = 0; i < n; ++i) a[i] = (i * 16807) % n;
    
    double t0 = omp_get_wtime();
    #pragma omp parallel
    {
        #pragma omp single
        parallel_qsort(a.data(), 0, n, 0);
    }
    double t1 = omp_get_wtime();
    
    printf("n=%d sorted=%d time=%.2fms threads=%d\n",
           n, std::is_sorted(a.begin(), a.end()), (t1-t0)*1000,
           omp_get_max_threads());
    return 0;
}
```

---

## 七、常见陷阱与最佳实践 ⚠️

| 陷阱 | 后果 | 对策 |
|------|------|------|
| 循环有跨迭代依赖 | 数据竞争/结果错 | 重构算法或用 task depend |
| 用 critical 做归约 | 串行化,慢 | 用 reduction |
| 串行初始化大数组 | NUMA 远程访存 | 并行 first-touch |
| 默认 schedule 在不均负载 | 慢 | 改 dynamic/guided |
| 不设 proc_bind | 线程跨核迁移 | `OMP_PROC_BIND=close` |
| false sharing | 2-10 倍损失 | alignas(64) 或 padding |
| task 无深度阈值 | 调度开销爆炸 | 设 depth 阈值转串行 |
| shared 变量未识别 | 数据竞争 | 显式标注 private/shared |
| atomic 用在循环内 | 串行化 | 改 reduction 或私有化 |
| 忘记 `parallel for` 的隐式 barrier | 误以为串行 | 加 `nowait` 取消(若安全) |

---

## 八、与其他章节关系

| 章节 | 关系 |
|------|------|
| §1 体系结构 | NUMA / false sharing 物理基础 |
| §3 算法设计 | task / 分治模式落地 |
| §5 MPI | MPI + OpenMP 混合 |
| §8 同步原语 | critical / atomic / lock |
| §9 内存模型 | relaxed + flush |
| §10 线性代数 | BLAS 多线程底层 |

---

## 九、面试速答 ⭐

| 问 | 答 |
|----|-----|
| fork-join 模型? | 主线程 fork 团队并行,join 同步 |
| schedule 四种? | static / static,k / dynamic,k / guided |
| 不均负载用哪种? | dynamic 或 guided |
| reduction 原理? | 每线程私有副本,结束树形合并 |
| reduction 比 critical 快多少? | $O(\log P)$ 倍,树形 vs 串行 |
| NUMA first-touch? | 物理页按首次写线程的 NUMA 节点分配 |
| false sharing 怎么消除? | alignas(64) 或 padding |
| OMP_PROC_BIND 作用? | 线程绑核,避免迁移 |
| task 何时用? | 不规则并行(递归/图遍历) |
| parallel for 隐式 barrier? | 是,可用 nowait 取消 |
| critical vs atomic? | critical 通用互斥;atomic 单语句硬件原子,更快 |
| OpenMP 内存模型? | relaxed,需 flush 同步 |

---

## 十、综合面试题

1. **基础**:为什么 `reduction` 比 `atomic` 快?
   - 答:reduction 每线程私有副本无竞争,结束才树形合并($O(\log P)$ 步);atomic 每次更新都串行化($O(P)$ 步)。$P=64$ 时差 6 倍。

2. **进阶**:16 核机器跑矩阵向量乘,加速比只有 8x。可能原因?
   - 答:(1) NUMA 远程访存(检查 first-touch);(2) 内存带宽饱和(矩阵向量乘是带宽受限);(3) false sharing(检查 y 数组写);(4) 不绑核导致迁移;(5) `schedule` 不当。

3. **深度**:设计一个 NUMA 感知的并行 reduce,使带宽最大化。
   - 答:(1) 数据按 NUMA 节点分片,first-touch 在本地;(2) 节点内多线程并行 reduce 到本地部分和;(3) 跨节点只交换部分和(小数据);(4) `proc_bind=close` 绑核;(5) 用 reduction 子句。比朴素 reduce 快 2-3 倍。

4. **设计**:用 OpenMP task 实现并行归并排序,分析复杂度。
   - 答:递归切分到 $n/P$ 规模转串行,task 并行两半,taskwait 后归并。$W = O(n\log n)$, $D = O(\log^2 n)$(每层归并 $O(\log n)$),$T_P \approx O(n\log n / P)$。设 depth 阈值避免 task 爆炸。

5. **工程**:OpenMP + MPI 混合编程相比纯 MPI 的优势?
   - 答:(1) 节点内共享内存免 MPI 通信开销;(2) 进程数减少,AllReduce 在节点内用共享内存;(3) 更好利用多核(纯 MPI 进程数 = 核数时 context switch 大);(4) 内存利用率高(无每进程副本)。劣势:OpenMP 难做负载均衡。

6. **学术**:证明 OpenMP reduction 在 $P$ 线程下的合并复杂度是 $O(\log P)$。
   - 答:reduction 实现为树形合并:第 $k$ 层有 $P/2^k$ 个部分和,共 $\lceil \log_2 P \rceil$ 层,每层 $O(1)$ 操作。总合并步数 $O(\log P)$。对比 critical 的 $O(P)$ 串行合并,加速比 $P/\log P$。

---

## 十一、参考与延伸

### 11.1 教材
- 《Using OpenMP》—— Chapman, Jost, Van Der Pas
- 《OpenMP in Action》—— van der Pas
- 《Parallel Programming in OpenMP》—— Chandra, Dagum, Kohr

### 11.2 标准
- OpenMP 5.2 规范(2021)
- OpenMP 6.0 规范(2024,新增 target offload 改进)

### 11.3 论文
- 📜 Dagum & Menon, "OpenMP: An Industry-Standard API for Shared-Memory Programming" (1998)
- 📜 Ayguadé et al., "The Design of OpenMP Tasks" (2009)

### 11.4 工具
- GCC / Clang / Intel ICC 都支持 `-fopenmp`
- `OMP_NUM_THREADS` / `OMP_PROC_BIND` / `OMP_PLACES`
- Intel VTune / `perf` 分析 OpenMP
- LLVM OpenMP runtime 源码

### 11.5 跨文件链接
- [01-并行计算基础与体系结构.md](./01-并行计算基础与体系结构.md) — NUMA / false sharing
- [03-并行算法设计模式.md](./03-并行算法设计模式.md) — task 模式
- [05-消息传递-MPI.md](./05-消息传递-MPI.md) — 混合编程
- [08-同步与并发原语.md](./08-同步与并发原语.md) — critical / atomic
- [09-内存模型与一致性.md](./09-内存模型与一致性.md) — relaxed + flush
- [../README.md](./README.md)
