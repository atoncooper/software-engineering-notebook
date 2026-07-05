# 并行计算 · 面试、原理、学术与工业笔记

> 定位：四层深度并重——
> 1. **面试速查层**（高频问答骨架，覆盖 HPC / AI Infra / 系统岗 / 大厂基础架构）
> 2. **原理层**（体系结构 → 编程模型 → 算法设计 → 性能模型 → 通信 → 存储）
> 3. **学术层**（PRAM / BSP / LogP / Roofline / Amdahl-Gustafson / 经典论文）
> 4. **工业层**（CUDA / OpenMP / MPI / NCCL / Ray / Spark / Megatron / DeepSpeed / 千卡训练）
>
> 原则：Correctness > Completeness > Speed；每章按四层递进。
> 约定：⭐高频 🔥工程重点 📜论文/标准 ⚠️易错点 🎓学术深度 🏭工业实战

---

## 0. 阅读指南

- **面试速查** → §23 + 每章「面试要点」
- **系统学习** → §1→§22 顺序，章间有依赖（体系结构 → 编程模型 → 算法 → 通信 → 性能 → 工业落地）
- **论文深读** → 每章「学术/论文」+ §24 论文清单
- **工业实战** → 每章「工业实战」+ §22 千卡训练案例库
- **硬件对照** → §附录 D CPU / GPU / 加速卡 / 网络拓扑对照
- **标记**：⭐ 🔥 📜 ⚠️ 🎓 🏭

### 共享元数据文件

| 文件 | 用途 |
|------|------|
| [_章节模板.md](_章节模板.md) | 新建章节时复制的模板 |
| [_术语表.md](_术语表.md) | 中英对照术语表（持续更新） |
| [_符号约定.md](_符号约定.md) | 全仓数学/复杂度/通信符号统一 |
| [_标记规范.md](_标记规范.md) | ⭐🔥📜⚠️🎓🏭 使用边界 |
| [_Review清单.md](_Review清单.md) | 单章与全仓 review checklist |

---

## 0.1 快速导航表

| 章 | 文件 | 主题 | 关键词 | 状态 |
|----|------|------|--------|------|
| §1 | [01-并行计算基础与体系结构.md](01-并行计算基础与体系结构.md) | 基础与体系结构 | Flynn 分类 / SISD·SIMD·MIMD / 共享 vs 分布式 / NUMA / 缓存一致性 / 内存墙 | ✅ |
| §2 | [02-性能模型与度量.md](02-性能模型与度量.md) | 性能模型 | Amdahl / Gustafson / Karp-Flatt / Roofline / 工作-跨度 / 强弱扩展 | ✅ |
| §3 | [03-并行算法设计模式.md](03-并行算法设计模式.md) | 算法设计 | PRAM / 划分·分治·流水·波前 / Foster 方法 / 依赖分析 / 工作偷取 | ✅ |
| §4 | [04-共享内存并行-OpenMP.md](04-共享内存并行-OpenMP.md) | OpenMP | pragma / fork-join / reduction / schedule / NUMA aware / false sharing | ✅ |
| §5 | [05-消息传递-MPI.md](05-消息传递-MPI.md) | MPI | 6 函数 / 集体通信 / 拓扑 / 一侧通信 / 非阻塞 / MPI+OpenMP 混合 | ✅ |
| §6 | [06-GPU并行-CUDA.md](06-GPU并行-CUDA.md) | CUDA | SIMT / Warp / Block / Grid / 共享内存 / Bank Conflict / Coalesce / Occupancy | ✅ |
| §7 | [07-GPU高级优化.md](07-GPU高级优化.md) | GPU 进阶 | Tensor Core / WMMA / CuDNN / Kernel Fusion / Stream / Graph / Triton | 📝 |
| §8 | [08-同步与并发原语.md](08-同步与并发原语.md) | 同步原语 | 自旋锁 / 互斥锁 / RCU / 事务内存 / 无锁队列 / MCS 锁 / Memory Barrier | ✅ |
| §9 | [09-内存模型与一致性.md](09-内存模型与一致性.md) | 内存模型 | Sequential / TSO / Release-Consistency / DRF / C++/Java/Rust 内存模型 | ✅ |
| §10 | [10-并行数值线性代数.md](10-并行数值线性代数.md) | 数值线性代数 | BLAS / LAPACK / ScaLAPACK / Cannon / DNS / Fox / Cholesky 分解并行 | ✅ |
| §11 | [11-并行排序与选择.md](11-并行排序与选择.md) | 排序与选择 | 并行归并 / Sample Sort / Bitonic / Radix 并行 / Top-K / 第 K 大 | ✅ |
| §12 | [12-并行图算法.md](12-并行图算法.md) | 图算法 | BFS/DFS 并行 / PageRank / SSSP / Label Propagation / Ligra / 图划分 | ✅ |
| §13 | [13-通信与集体原语.md](13-通信与集体原语.md) | 通信与集体原语 | Ring / Tree / Recursive Doubling / AllReduce / AllGather / ReduceScatter / NCCL | ✅ |
| §14 | [14-分布式深度学习.md](14-分布式深度学习.md) | 分布式训练 | DP / TP / PP / ZeRO / FSDP / 1F1B / interleaved / Megatron-LM / DeepSpeed | ✅ |
| §15 | [15-集合通信与拓扑感知.md](15-集合通信与拓扑感知.md) | 拓扑与网络 | NVLink / NVSwitch / InfiniBand / RoCE / Fat-Tree / Dragonfly / 拓扑感知调度 | 📝 |
| §16 | [16-大规模数据处理.md](16-大规模数据处理.md) | 大数据处理 | MapReduce / Spark / RDD / Shuffle / 调度 / 数据本地性 / 容错 | 📝 |
| §17 | [17-流计算与迭代计算.md](17-流计算与迭代计算.md) | 流式与迭代 | Flink / Storm / Watermark / 反压 / BSP / Pregel / 迭代收敛 | 📝 |
| §18 | [18-并行存储与IO.md](18-并行存储与IO.md) | 并行 IO | POSIX IO / MPI-IO / 并行文件系统 / Lustre / GPFS / 数据布局 / Stripe | 📝 |
| §19 | [19-性能分析与调优.md](19-性能分析与调优.md) | 性能分析 | VTune / Nsight / Tau / Strong/Weak Scaling / Profiling / Roofline 落地 | 📝 |
| §20 | [20-异构计算与任务并行.md](20-异构计算与任务并行.md) | 异构与任务并行 | TBB / Cilk / Ray / Dask / HPX / DAG 调度 / CPU+GPU+FPGA | 📝 |
| §21 | [21-并行调试与正确性.md](21-并行调试与正确性.md) | 调试与正确性 | 竞态 / 死锁 / 数据竞争 / ThreadSanitizer / Must / Replay / 形式化验证 | 📝 |
| §22 | [22-工业案例与千卡训练.md](22-工业案例与千卡训练.md) | 工业实战 | GPT-3 / LLaMA 训练 / RLHF 并行 / 推理并行 / 阿里/字节/DeepSeek 实践 | 📝 |
| §23 | [23-面试高频问题.md](23-面试高频问题.md) | 面试速查 | 50+ 高频问答骨架（HPC / AI Infra / 系统岗） | 📝 |
| §24 | [24-标准与论文清单.md](24-标准与论文清单.md) | 标准与论文 | 经典论文 / SPEC / HPCG / HPL-AI / MLPerf / Top500 | 📝 |
| §25 | [25-附录.md](25-附录.md) | 附录 | 术语表 / 符号 / 硬件对照 / 代码模板 / 调试速查 | 📝 |

**状态图例**：🚧 写作中 · ✅ 完成 · 📝 仅骨架 · 🔍 待 review

---

## 0.2 阅读顺序图

```
入门
  │
  ├─→ §1 体系结构（Flynn / NUMA / 缓存 / 内存墙）
  │
  ├─→ §2 性能模型（Amdahl / Gustafson / Roofline）
  │
  ├─→ §3 算法设计模式（Foster / PRAM / 划分·分治·流水·波前）
  │
  └─→ §4–§6 三大编程模型
              │
       ┌──────┼──────────┐
       ▼      ▼          ▼
    §4 OpenMP  §5 MPI   §6 CUDA
   (共享内存) (消息传递) (SIMT)
       │      │          │
       └──────┼──────────┘
              ▼
       §7 GPU 高级优化（Tensor Core / Triton）
              │
              ▼
       §8–§9 同步原语 + 内存模型（正确性根基）
              │
              ▼
       §10–§12 经典并行算法（线性代数 / 排序 / 图）
              │
              ▼
       §13 集合通信与 NCCL（千卡训练根基）
              │
              ▼
       §14 分布式深度学习（DP/TP/PP/ZeRO/FSDP）
              │
       ┌──────┼──────────┐
       ▼      ▼          ▼
    §15 拓扑  §16 MR/Spark  §17 流/迭代
              │
              ▼
       §18 并行 IO  §19 性能分析  §20 异构任务并行
              │
              ▼
       §21 调试与正确性
              │
              ▼
       §22 工业案例 / 千卡训练实战
              │
              ▼
       §23 面试  §24 论文  §25 附录
```

---

## 0.3 知识地图：并行计算全景

```
                  ┌────────────────────────────────────────────┐
                  │            并行计算 全景                     │
                  └────────────────────┬───────────────────────┘
                                       │
        ┌──────────────┬───────────────┼──────────────┬────────────────┐
        │              │               │              │                │
     体系结构        编程模型         算法设计       通信与同步        工业系统
   ┌────┴────┐    ┌───┴────┐      ┌────┴────┐    ┌───┴────┐      ┌────┴────┐
   Flynn 分类   OpenMP      PRAM   划分         集体通信        分布式训练
   NUMA         MPI         Foster 分治         AllReduce      Megatron
   缓存一致性   CUDA        流水   归并         点对点          DeepSpeed
   内存墙       SIMT        波前   图算法        NCCL           Ray
   向量化       Triton             排序         拓扑感知        Spark
   GPU 架构     TBB/Ray            线性代数     内存一致性      Flink
   │                                                            │
   └────────────────────────────────────────────────────────────┘
                                │
                                ▼
                    §14 分布式深度学习（融合层）
                                │
              ┌─────────────────┼──────────────────┐
              ▼                 ▼                  ▼
          数据并行           张量并行            流水并行
          DP / ZeRO          TP / Megatron       PP / 1F1B
          FSDP               Sequence Parallel   Interleaved
                                │
                                ▼
                    §22 千卡训练工业实战
                    GPT-3 / LLaMA / DeepSeek
```

---

## 1. 每章统一结构

每篇章节遵循如下 12 节结构（与分布式系统 / 云计算安全项目一致）：

1. **思维导图**：ASCII 树概览全章脉络
2. **问题定义**：解决什么问题、典型场景、硬件差异
3. **核心概念与术语**：定义清楚后再展开
4. **原理与机制**：第一性原理推导，标明体系结构/算法意义
5. **算法/流程**：伪代码 + 复杂度（计算/通信/存储三维）
6. **工业实现对照**：OpenMP / MPI / CUDA / NCCL / Ray / Spark 实现
7. **代码示例**：可运行最小版 + 生产级版（含调优参数）
8. **常见陷阱与最佳实践**：⚠️ 工程化视角（false sharing / bank conflict / 死锁 …）
9. **与其他章节关系**：横向对比表
10. **面试速答**：高频问题的一句话答案
11. **综合面试题**：由浅入深，含答题要点
12. **参考与延伸**：标准、论文、白皮书、跨文件链接

---

## 2. 面试高频考点速查

### 2.1 体系结构与性能模型
- Flynn 四分类（SISD/SIMD/MISD/MIMD）与典型硬件对应
- 共享内存 vs 分布式内存的本质差异
- NUMA 架构下线程绑定的必要性
- 缓存一致性协议（MESI / MOESI / MESIF）的区别
- Amdahl vs Gustafson：强扩展 vs 弱扩展
- Roofline 模型如何预估性能上限
- 工作-跨度模型（work-span）与 $T_P = T_1/P + T_\infty$
- 内存墙、IO 墙、通信墙的成因

### 2.2 OpenMP / 共享内存
- fork-join 模型的执行流
- `#pragma omp parallel for` 的调度策略（static/dynamic/guided/runtime）
- reduction 子句的实现原理
- false sharing 的成因与消除（padding / 对齐）
- NUMA aware 编程（first-touch 策略）
- 与 C++ `std::thread` / `std::async` 的差异

### 2.3 MPI / 消息传递
- MPI 六个核心函数
- 阻塞 vs 非阻塞通信的语义
- 集体通信的算法实现（Ring / Tree / Recursive Doubling）
- AllReduce 在分布式训练中的核心地位
- 一侧通信（RMA）vs 双侧通信
- MPI + OpenMP 混合编程的优缺点
- 死锁的典型场景与规避

### 2.4 CUDA / GPU
- SIMT 执行模型 vs SIMD
- Warp / Block / Grid 的层次结构与硬件映射
- 全局内存 / 共享内存 / 寄存器 / L1/L2 的延迟与带宽
- Coalesced memory access 的判定
- Shared memory bank conflict 的成因与消除
- Occupancy 公式与计算
- Tensor Core 与 cuBLAS/cuDNN 的关系
- CUDA Stream / Event / Graph 的差异
- Kernel fusion 何时有益

### 2.5 同步与内存模型
- 自旋锁 vs 互斥锁 vs 读写锁的适用场景
- MCS 锁 / CLH 锁的链表实现
- 无锁数据结构（队列 / 栈）与 ABA 问题
- RCU 在 Linux 内核的应用
- 事务内存（HTM/STM）的优势与限制
- Sequential Consistency vs TSO vs Release Consistency
- Data-Race-Free 程序的语义
- Memory Barrier / Fence 的指令对应

### 2.6 并行算法
- 并行归并排序的分治与通信复杂度
- Bitonic sort 的 $O(\log^2 n)$ 深度
- 并行矩阵乘法：Cannon / DNS / Fox 算法
- 并行 BFS 的层级同步与方向优化
- PageRank 的迭代与稀疏矩阵向量乘
- 拓扑排序的并行性分析
- 并行 prefix sum（scan）的 Blelloch 算法

### 2.7 通信与集合原语
- Ring AllReduce 的带宽最优性
- Tree AllReduce 的延迟最优性
- Recursive Doubling 的对数延迟
- AllGather / ReduceScatter 与 AllReduce 的关系
- NCCL 的拓扑探测与路径选择
- 计算通信重叠（overlap）的实现

### 2.8 分布式深度学习
- DP / TP / PP / ZeRO 的本质差异
- ZeRO-1/2/3 切分的是什么
- FSDP 与 ZeRO-3 的关系
- Megatron 张量并行的切分维度
- 1F1B 流水并行的内存优势
- Interleaved 1F1B 的 micro-batch 数选择
- Sequence Parallel 解决什么问题
- Expert Parallel 与 MoE

### 2.9 网络与拓扑
- NVLink vs PCIe vs InfiniBand 的带宽对比
- Fat-Tree / Dragonfly / Torus 拓扑的扩展性
- RoCE vs InfiniBand 的协议差异
- 拓扑感知调度对训练吞吐的影响
- GPUDirect RDMA 的旁路机制

### 2.10 大数据与流计算
- MapReduce 的 shuffle 性能瓶颈
- Spark RDD 的血缘与容错
- BSP 模型与 Pregel
- Flink Watermark 与乱序处理
- 反压（backpressure）机制
- 数据本地性对调度的影响

### 2.11 性能分析与调试
- Roofline 落地的步骤
- 强扩展 vs 弱扩展曲线的解读
- Nsight Compute / Nsight Systems 的层次
- 竞态条件、死锁、数据竞争的区分
- ThreadSanitizer / CUDA-memcheck 的使用
- 千卡训练常见的 straggler 问题

---

## 3. 经典模型与定律索引

| 模型/定律 | 表达式 | 适用范围 | 关键章节 |
|-----------|--------|----------|----------|
| Amdahl 定律 | $S_P = \frac{1}{(1-f)+f/P}$ | 强扩展（固定问题规模） | §2 |
| Gustafson 定律 | $S_P = P - \alpha(P-1)$ | 弱扩展（问题规模随 P 增长） | §2 |
| Karp-Flatt 度量 | $e = \frac{(1/S_P) - (1/P)}{1 - (1/P)}$ | 串行比例实验测定 | §2 |
| Work-Span | $T_P \ge \max(T_1/P, T_\infty)$ | 算法并行度上界 | §2, §3 |
| Roofline | $A = \min(\pi I, \beta I, \hat{B})$ | 硬件性能上限 | §2, §19 |
| PRAM | CRCW / CREW / ERCW / EREW | 算法理论模型 | §3 |
| BSP | $T = \sum (w_i + h_i g + L)$ | 超步模型 | §3, §17 |
| LogP | $(L, o, g, P)$ | 分布式算法分析 | §3, §13 |
| Little 定律 | $N = \lambda W$ | 排队与吞吐 | §19 |
| Gustafson-Barsis | 同 Gustafson | 弱扩展修正 | §2 |

---

## 4. 硬件对照表（速查，详见 §25 附录 D）

### 4.1 计算硬件

| 类型 | 典型代表 | 峰值算力 (FP16) | 内存带宽 | 互联 | 关键场景 |
|------|----------|----------------|----------|------|----------|
| CPU | AMD EPYC 9654 / Xeon 8480+ | ~2 TFLOPS | ~400 GB/s | PCIe5 / CXL | 控制流 / 串行 |
| GPU | NVIDIA H100 SXM | 1979 TFLOPS (稀疏) | 3.35 TB/s | NVLink4 900GB/s | 训练 / 推理 |
| GPU | NVIDIA A100 80G | 624 TFLOPS (稀疏) | 2.0 TB/s | NVLink3 600GB/s | 训练 |
| GPU | AMD MI300X | 1307 TFLOPS | 5.3 TB/s | Infinity Fabric | 训练 |
| TPU | Google TPU v5e/v5p | 197/457 TFLOPS | 816/2765 GB/s | ICI | 训练（JAX） |
| 加速卡 | 华为昇腾 910B | ~320 TFLOPS | ~1.6 TB/s | HCCS | 国产训练 |
| FPGA | Xilinx Versal / Agilex | 可变 | 可变 | PCIe / 网络 | 推理 / DSL |

### 4.2 网络与互联

| 互联 | 单链带宽 | 典型延迟 | 适用 |
|------|----------|----------|------|
| PCIe 5.0 x16 | 64 GB/s | ~1 μs | CPU↔GPU |
| NVLink 4.0 | 900 GB/s | ~200 ns | GPU↔GPU（同节点） |
| NVSwitch | 全互联无阻塞 | — | 8 卡节点内 |
| InfiniBand HDR | 200 Gb/s | ~1 μs | 节点间（HPC/训练） |
| InfiniBand NDR | 400 Gb/s | ~1 μs | 新一代训练集群 |
| RoCE v2 | 100/200/400 Gb/s | ~2 μs | 通用以太网 RDMA |
| 基于以太网 TCP | 100/400 Gb/s | ~10 μs | 大数据 / 推理 |

### 4.3 并行软件栈对照

| 层 | 共享内存 | 分布式内存 | GPU | 大数据 | AI 训练 |
|----|----------|-----------|-----|--------|---------|
| 编程模型 | OpenMP / pthread | MPI / PGAS | CUDA / HIP / SYCL | Spark / Flink | PyTorch / JAX |
| 通信 | shared memory | MPI / SHMEM | NCCL / RCCL | Netty / gRPC | NCCL / Gloo |
| 调度 | OS / TBB | MPI mpirun | Stream / Graph | DAG Scheduler | Torchrun / Slurm |
| 存储 | mmap / RAM | MPI-IO / Lustre | GDS / cuFile | HDFS / S3 | 本地 SSD / NFS |
| 分析 | VTune / perf | Tau / Scalasca | Nsight / CUPTI | Spark UI | PyTorch Profiler |

---

## 5. 经典论文与基准（详见 §24）

### 5.1 体系结构与理论
- Amdahl, "Validity of the Single Processor Approach to Achieving Large Scale Computing Capabilities" (1967)
- Gustafson, "Reevaluating Amdahl's Law" (1988)
- Culler et al., "LogP: Towards a Realistic Model of Parallel Computation" (1993)
- Valiant, "A Bridging Model for Parallel Computation" (BSP, 1990)
- Williams et al., "Roofline: An Insightful Visual Performance Model" (2009)
- Leiserson, "The Cilk Concurrency Platform" (2009)

### 5.2 算法
- Blelloch, "Scans and Primitive Recursion" (Prefix Sum, 1988)
- Cannon, "A Cellular Computer for Matrix Multiplication" (1969)
- Nassimi & Sahni, "Data Broadcasting and Parallel Matrix Multiplication" (DNS, 1981)
- Malewicz et al., "Pregel: A System for Large-Scale Graph Processing" (2010)
- Gonzalez et al., "PowerGraph: Distributed Graph-Parallel Computation" (2012)

### 5.3 系统
- Dean & Ghemawat, "MapReduce: Simplified Data Processing on Large Clusters" (2004)
- Zaharia et al., "Resilient Distributed Datasets" (2012)
- Carbone et al., "Apache Flink: Stream and Batch Processing" (2015)
- Moritz et al., "Ray: A Distributed Framework for Emerging AI Applications" (2018)

### 5.4 分布式训练
- Krizhevsky, "One Weird Trick for Parallelizing CNNs" (Model Parallel, 2014)
- Shazeer et al., "The Sparsely-Gated MoE Layer" (2017)
- Shoeybi et al., "Megatron-LM" (2019)
- Rajbhandari et al., "ZeRO: Memory Optimizations Toward Training Trillion Parameter Models" (2019)
- Narayanan et al., "Efficient Large-Scale Language Model Training on GPU Clusters" (Megatron + 1F1B, 2021)
- Korthikanti et al., "Reducing Activation Recomputation in LLM Training" (Sequence Parallel, 2022)
- Hu et al., "LoRA: Low-Rank Adaptation" (2021)

### 5.5 基准
- HPL / HPCG / HPL-AI / MLPerf / Top500 / Graph500

---

## 6. 笔记约定

- **语言与框架**：核心示例为 C/C++ (OpenMP/MPI) + CUDA + Python (PyTorch/JAX)
- **硬件中立**：正文概念中立，每章「工业实现对照」小节列出 NVIDIA / AMD / 国产硬件差异
- **数学符号**：统一见 [_符号约定.md](_符号约定.md)
- **复杂度**：每算法标明计算复杂度 $W$、通信复杂度 $C$、深度 $D$
- **图示**：优先 ASCII 图说明算法/拓扑；复杂图标注来源
- **公式**：关键推导步骤必须标明物理意义
- **代码示例**：从零最小可运行版（教学）+ 生产级版（工程，含调优参数/事件/profiling）
- **跨文件链接**：相关概念使用相对路径链接，便于跳转
- **基准标注**：涉及性能时显式引用数据来源（如 "H100 实测 / HPL 报告"）

---

## 7. TODO / 待完善

- [ ] 按章节逐篇完善 §1 → §22 内容
- [ ] 每章补充真实工业案例（覆盖 §22）
- [ ] 补充 NVIDIA / AMD / 国产硬件三套示例
- [ ] 增加并行算法可视化图集（依赖图 / 通信模式 / 拓扑）
- [ ] 增加千卡训练故障排查矩阵（straggler / 溢出 / 通信瓶颈）
- [ ] 增加大厂面试真题汇编（按公司/岗位分类）
- [ ] 增加调试 recipe 库（死锁 / 竞态 / 数值发散 / NCCL hang）
- [ ] 跟踪新硬件（B200 / MI350 / 昇腾 910C）与新算法（Ring-Attention / DeepSeek-EP）

---

## 8. 与仓库其他子项目的关系

- [../分布式系统/](../分布式系统/README.md)：共识/复制/事务是分布式训练与大数据的底座
- [../云计算安全/](../云计算安全/README.md)：集群安全、密钥、隔离（容器/K8s 多租户训练）
- [../数据库/](../数据库/README.md)：并行查询、向量化执行、列存
- [../机器学习/](../机器学习/README.md)：监督/无监督算法的并行化
- [../深度学习/](../深度学习/README.md)：分布式训练算法基础（DP/TP/PP 在此深入实现）
- [../LLM/](../LLM/README.md)：千卡训练 + RLHF 并行 + 推理并行
- [../Agent开发/](../Agent开发/README.md)：Ray / 异步任务调度
- [../infra开发/](../infra开发/README.md)：Slurm / K8s / IaC 与算力调度
- [../软件工程系统分析与设计/](../软件工程系统分析与设计/README.md)：并发设计与可扩展性分析
