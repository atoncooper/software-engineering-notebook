# §5 - 消息传递 · MPI

> 一句话定位:本章讲清 MPI 的六大核心函数、阻塞/非阻塞通信语义、集体通信与拓扑、MPI-IO 与混合编程,并用手把手的雅可比迭代演示从串行到 MPI 并行的完整改造路径。
>
> 标记：⭐高频 🔥工程重点 📜论文/标准 ⚠️易错点 🎓学术深度 🏭工业实战

---

## 〇、思维导图

```
                      MPI
                       │
       ┌───────────────┼───────────────┐
       │               │               │
   点对点通信       集体通信         高级特性
       │               │               │
   6 核心函数      Broadcast        MPI-IO
   Send/Recv       Scatter/Gather   一侧通信(RMA)
   阻塞/非阻塞     Reduce           拓扑 communicator
   缓冲模式        AllReduce        MPI+OpenMP 混合
   标签/通信器     Alltoall         动态进程
       │               │               │
   死锁           算法实现          实战案例
   活锁           Ring/Tree         雅可比迭代
   顺序依赖        Halving-Doubling  1D/2D 域分解
```

---

## 一、问题定义

### 1.1 解决什么问题

OpenMP 解决单机多核并行,MPI 解决**跨节点分布式内存**并行——每个进程独立地址空间,通过消息传递协作。

典型场景:
- HPC 集群上的科学计算(气象、CFD、地震模拟);
- 分子动力学(LAMMPS、GROMACS);
- 量子化学(Gaussian、VASP);
- 大模型训练的底层通信(NCCL 借鉴 MPI 概念);
- 大数据系统(Spark 底层也曾用 MPI 思想)。

### 1.2 与 OpenMP 的本质区别

| 维度 | OpenMP | MPI |
|------|--------|-----|
| 内存 | 共享 | 分布式(私有) |
| 编程模型 | fork-join 单进程多线程 | 多进程,SPMD |
| 通信 | 共享变量(隐式) | 显式 Send/Recv |
| 同步 | barrier / critical | Barrier / 集体通信 |
| 扩展 | 单机 | 跨节点 |
| 失败模型 | 一线程崩全挂 | 一进程崩全挂(默认) |

### 1.3 与 §13 的分工

- §13 讲集合通信**算法实现**(Ring/Tree/Recursive Doubling 复杂度推导);
- 本章讲 MPI **API 与编程模型**(如何用 MPI 写并行程序)。

---

## 二、核心概念与术语

| 术语 | 含义 |
|------|------|
| Communicator | 进程组 + 上下文,默认 `MPI_COMM_WORLD` |
| Rank | 进程在通信器内的编号 |
| Size | 通信器内进程数 |
| Tag | 消息标签,区分不同消息流 |
| SPMD | Single Program Multiple Data,所有进程跑同一程序 |
| Point-to-Point | 点对点通信(Send/Recv) |
| Collective | 集体通信(全进程参与) |
| Blocking | 阻塞,完成才返回 |
| Non-blocking | 非阻塞,立即返回用 Wait 等完成 |
| Buffered | 缓冲模式,用户管理缓冲 |
| Synchronous | 同步模式,Recv 确认才返回 |
| Ready | 就绪模式,Recv 已 post 才能发 |
| RMA | One-sided communication,远程内存访问 |
| MPI-IO | 并行 IO 接口 |
| Cart communicator | 笛卡尔拓扑通信器 |

---

## 三、原理与机制

### 3.1 SPMD 执行模型 🎓

MPI 程序是 **SPMD**:同一份程序,所有进程跑同一代码,通过 `rank` 区分行为。

```cpp
int rank, size;
MPI_Comm_rank(MPI_COMM_WORLD, &rank);
MPI_Comm_size(MPI_COMM_WORLD, &size);

if (rank == 0) {
    // master 工作
} else {
    // worker 工作
}
```

🔥 **关键**:MPI 没有"主进程创建子进程"的概念,所有进程由 `mpirun` 启动,平等存在。

### 3.2 六大核心函数 📜

MPI 标准有 100+ 函数,但 90% 程序只用 6 个:

| 函数 | 作用 |
|------|------|
| `MPI_Init` | 初始化 MPI 环境 |
| `MPI_Comm_rank` | 获取当前进程 rank |
| `MPI_Comm_size` | 获取进程总数 |
| `MPI_Send` | 发送消息 |
| `MPI_Recv` | 接收消息 |
| `MPI_Finalize` | 清理 MPI 环境 |

```cpp
#include <mpi.h>
int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    
    if (rank == 0) {
        int msg = 42;
        MPI_Send(&msg, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);
    } else if (rank == 1) {
        int msg;
        MPI_Recv(&msg, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        printf("rank 1 received %d\n", msg);
    }
    
    MPI_Finalize();
    return 0;
}
```

### 3.3 阻塞 vs 非阻塞 🎓

**阻塞 `MPI_Send/Recv`**:
- `MPI_Send`:发送缓冲可安全复用才返回(MPI 实现可能 buffer、也可能阻塞);
- `MPI_Recv`:消息收到才返回。

**非阻塞 `MPI_Isend/Irecv`**:
- 立即返回 `MPI_Request`;
- 用 `MPI_Wait` 等完成;
- **允许计算通信重叠**(overlap)。

```cpp
// 非阻塞 overlap 示例
MPI_Request req;
MPI_Isend(buf, n, MPI_DOUBLE, dst, 0, comm, &req);
// 这里可以继续计算,通信在后台进行
do_compute(...);
MPI_Wait(&req, MPI_STATUS_IGNORE);
```

🔥 **性能关键**:HPC 中 overlap 是性能核心——边算边传,隐藏通信延迟。

### 3.4 四种发送模式 🎓

MPI Send 有四种语义模式:

| 模式 | 函数 | 语义 |
|------|------|------|
| Standard | `MPI_Send` | MPI 自选(buffer 或同步) |
| Buffered | `MPI_Bsend` | 用户 buffer,立即返回 |
| Synchronous | `MPI_Ssend` | Recv 启动才返回 |
| Ready | `MPI_Rsend` | Recv 已 post 才能调 |

⚠️ Standard 模式行为依赖实现,死锁排查时改用 `MPI_Ssend`(语义明确)。

### 3.5 死锁典型场景 ⚠️

```cpp
// ❌ 死锁:所有进程同时 Send,buffer 满了阻塞
if (rank == 0) {
    MPI_Send(a, n, MPI_INT, 1, 0, comm);  // 等 rank1 Recv
    MPI_Recv(b, n, MPI_INT, 1, 0, comm);
} else {
    MPI_Send(a, n, MPI_INT, 0, 0, comm);  // 等 rank0 Recv
    MPI_Recv(b, n, MPI_INT, 0, 0, comm);
}
```

**修复方案**:
```cpp
// ✅ 方案 1:奇偶交替 Send/Recv
if (rank == 0) { Send(...); Recv(...); }
else           { Recv(...); Send(...); }

// ✅ 方案 2:MPI_Sendrecv(原子操作)
MPI_Sendrecv(sendbuf, n, MPI_INT, dst, 0,
             recvbuf, n, MPI_INT, src, 0, comm, &status);

// ✅ 方案 3:非阻塞
MPI_Isend(...); MPI_Irecv(...);
do_compute(...);
MPI_Waitall(...);
```

### 3.6 集体通信 🔥

集体通信要求**通信器内所有进程**共同参与,缺一不可:

| 函数 | 语义 |
|------|------|
| `MPI_Bcast` | root 广播到所有 |
| `MPI_Scatter` | root 把不同数据发给各 rank |
| `MPI_Gather` | 各 rank 数据收集到 root |
| `MPI_Reduce` | 各 rank 数据规约到 root |
| `MPI_Allreduce` | 规约后所有 rank 都有结果 |
| `MPI_Allgather` | 收集后所有 rank 都有 |
| `MPI_Alltoall` | 全交换,每对 rank 互发 |
| `MPI_Barrier` | 同步屏障 |

**算法实现**(详见 §13):
- 小消息:Tree / Recursive Doubling(延迟最优);
- 大消息:Ring(带宽最优)。

### 3.7 拓扑 communicator 🎓

```cpp
// 创建 2D 笛卡尔拓扑
int dims[2] = {px, py};
int periods[2] = {0, 0};  // 非周期
MPI_Comm cart_comm;
MPI_Cart_create(MPI_COMM_WORLD, 2, dims, periods, 0, &cart_comm);

// 获取上下左右邻居
int src[2], dst[2];
MPI_Cart_shift(cart_comm, 0, 1, &src[0], &dst[0]);  // 行方向
MPI_Cart_shift(cart_comm, 1, 1, &src[1], &dst[1]);  // 列方向
```

🔥 **优势**:MPI 自动选物理邻近的 rank 作邻居(拓扑感知),减少跨节点通信。

### 3.8 一侧通信 RMA 🎓

一侧通信(One-Sided / RMA):一个进程直接读写另一进程内存,无需对方 `Recv`。

```cpp
MPI_Win win;
MPI_Win_create(buf, size, sizeof(double), MPI_INFO_NULL, comm, &win);

MPI_Win_fence(0, win);  // 同步 epoch
MPI_Put(local, n, MPI_DOUBLE, target_rank, 0, n, MPI_DOUBLE, win);
MPI_Win_fence(0, win);
```

**优势**:无需双方协作,适合不规则访问(图算法、稀疏矩阵)。

### 3.9 MPI-IO 🎓

并行 IO:多进程同时读写同一文件,通过 collective IO 优化。

```cpp
MPI_File fh;
MPI_File_open(comm, "data.bin", MPI_MODE_CREATE|MPI_MODE_WRONLY,
              MPI_INFO_NULL, &fh);

// 每进程写自己段(视图)
MPI_Offset offset = rank * n * sizeof(double);
MPI_File_write_at(fh, offset, buf, n, MPI_DOUBLE, &status);

MPI_File_close(&fh);
```

🔥 **Collective IO**:多进程的独立小 IO 合并为大 IO,减少文件系统元数据开销,可加速 10-100 倍。

### 3.10 MPI + OpenMP 混合 🎓

**纯 MPI**:每核一个进程,跨节点 + 节点内都用 MPI 通信。
**MPI + OpenMP**:每节点一个 MPI 进程,节点内用 OpenMP 共享内存。

| 维度 | 纯 MPI | MPI+OpenMP |
|------|--------|-----------|
| 节点内通信 | 走 MPI 协议栈 | 共享内存,零开销 |
| 内存 | 每进程副本 | 共享,省 |
| 进程数 | = 核数 | = 节点数 |
| AllReduce | 全核参与 | 节点内 OpenMP 归约后跨节点 MPI |
| 失败 | 一进程挂全挂 | 同 |

🔥 **现代 HPC 主流**:MPI 跨节点 + OpenMP 节点内。

---

## 四、算法 / 流程

### 4.1 MPI 程序基本流程

```
1. MPI_Init
2. 获取 rank / size
3. 域分解(把数据切给各 rank)
4. 主循环:
   a. halo 交换(Sendrecv)
   b. 本地计算
   c. 收敛判断(Allreduce 求 max 残差)
5. 结果收集(Gather)
6. MPI_Finalize
```

### 4.2 雅可比迭代并行化流程

串行:
```
for iter in 0..maxiter:
    for i,j in 1..n-1:
        new[i][j] = (old[i-1][j] + old[i+1][j] + old[i][j-1] + old[i][j+1]) / 4
    swap(old, new)
    if max_diff < eps: break
```

并行(1D 域分解):
```
1. 把行切给 P 个 rank,每 rank 持有 rows [rank*N/P .. (rank+1)*N/P]
2. 每迭代:
   a. halo 交换:与 rank-1 和 rank+1 交换边界行
   b. 本地更新内部行
   c. Allreduce 求全局 max_diff
   d. 若收敛则 break
```

---

## 五、工业实现对照

| 实现 | 厂商 | 特性 |
|------|------|------|
| OpenMPI | 开源社区 | 默认,跨平台 |
| MPICH | Argonne | 标准 reference |
| Intel MPI | Intel | Intel 网络优化 |
| MVAPICH | Ohio State | InfiniBand 优化 |
| Cray MPICH | HPE/Cray | 超算优化 |
| NVIDIA HPC-X | NVIDIA | GPU-aware MPI + CUDA |

---

## 六、代码示例

### 6.1 最小可运行版(教学):Hello World + 点对点

```cpp
// file: hello_mpi.cpp
// 编译: mpicxx -O2 hello_mpi.cpp -o hello
// 运行: mpirun -np 4 ./hello
#include <mpi.h>
#include <cstdio>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    
    printf("Hello from rank %d of %d\n", rank, size);
    
    // Ring 传递:rank i 发给 (i+1)%size,收自 (i-1+size)%size
    int msg = rank;
    int next = (rank + 1) % size;
    int prev = (rank - 1 + size) % size;
    MPI_Sendrecv_replace(&msg, 1, MPI_INT, next, 0, prev, 0,
                         MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    printf("rank %d received %d\n", rank, msg);
    
    MPI_Finalize();
    return 0;
}
```

### 6.2 手把手:雅可比迭代 MPI 完整实现

```cpp
// file: jacobi_mpi.cpp
// 1D 域分解,2D 泊松方程求解
// 编译: mpicxx -O3 jacobi_mpi.cpp -o jac
// 运行: mpirun -np 4 ./jac 1024 1000
//   参数:网格大小 迭代次数
#include <mpi.h>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    
    int N = argc > 1 ? atoi(argv[1]) : 1024;
    int maxiter = argc > 2 ? atoi(argv[2]) : 1000;
    
    // 域分解:行分给各 rank
    int rows_per = N / size;
    int extra = N % size;
    int my_rows = rows_per + (rank < extra ? 1 : 0);
    int my_start = rank * rows_per + (rank < extra ? rank : extra);
    
    // 本地数组:+2 行用于 halo(上下各一行)
    std::vector<double> old_arr((my_rows + 2) * N, 0.0);
    std::vector<double> new_arr((my_rows + 2) * N, 0.0);
    
    // 初始化(简化:全 0,边界除外)
    if (rank == 0)
        for (int j = 0; j < N; ++j) old_arr[1 * N + j] = 1.0;  // 顶边界
    if (rank == size - 1)
        for (int j = 0; j < N; ++j)
            old_arr[(my_rows) * N + j] = 1.0;  // 底边界
    
    double t0 = MPI_Wtime();
    
    int prev = rank - 1, next = rank + 1;
    if (prev < 0) prev = MPI_PROC_NULL;
    if (next >= size) next = MPI_PROC_NULL;
    
    int iter;
    for (iter = 0; iter < maxiter; ++iter) {
        // 1. halo 交换
        MPI_Request reqs[4];
        MPI_Isend(&old_arr[1 * N], N, MPI_DOUBLE, prev, 0,
                  MPI_COMM_WORLD, &reqs[0]);  // 发上边界给 prev
        MPI_Isend(&old_arr[my_rows * N], N, MPI_DOUBLE, next, 0,
                  MPI_COMM_WORLD, &reqs[1]);  // 发下边界给 next
        MPI_Irecv(&old_arr[0 * N], N, MPI_DOUBLE, prev, 0,
                  MPI_COMM_WORLD, &reqs[2]);  // 收 prev 的下边界作我的 halo 上
        MPI_Irecv(&old_arr[(my_rows + 1) * N], N, MPI_DOUBLE, next, 0,
                  MPI_COMM_WORLD, &reqs[3]);  // 收 next 的上边界作我的 halo 下
        MPI_Waitall(4, reqs, MPI_STATUSES_IGNORE);
        
        // 2. 本地更新
        double my_diff = 0.0;
        for (int i = 1; i <= my_rows; ++i) {
            for (int j = 1; j < N - 1; ++j) {
                double up = old_arr[(i - 1) * N + j];
                double dn = old_arr[(i + 1) * N + j];
                double lf = old_arr[i * N + j - 1];
                double rt = old_arr[i * N + j + 1];
                double v = (up + dn + lf + rt) / 4.0;
                new_arr[i * N + j] = v;
                my_diff = std::max(my_diff, std::abs(v - old_arr[i * N + j]));
            }
        }
        
        // 3. 全局收敛判断
        double global_diff;
        MPI_Allreduce(&my_diff, &global_diff, 1, MPI_DOUBLE, MPI_MAX,
                      MPI_COMM_WORLD);
        
        std::swap(old_arr, new_arr);
        if (global_diff < 1e-6) break;
    }
    
    double t1 = MPI_Wtime();
    
    if (rank == 0)
        printf("Jacobi N=%d iters=%d time=%.3fs P=%d\n",
               N, iter, t1 - t0, size);
    
    MPI_Finalize();
    return 0;
}
```

🔥 **关键点**:
- **域分解**:`my_rows` + 2 行 halo,边界 rank 与 `MPI_PROC_NULL` 通信(空操作);
- **非阻塞 halo 交换**:`Isend`/`Irecv` + `Waitall`,可扩展为 overlap(在 Wait 前做不依赖边界的计算);
- **全局收敛**:`Allreduce` 求 max 残差,所有 rank 同步决策;
- **MPI_Wtime**:高精度计时。

### 6.3 手把手:计算通信 overlap 实测

```cpp
// file: overlap_bench.cpp
// 对比阻塞 vs 非阻塞 + overlap
// 编译: mpicxx -O3 overlap_bench.cpp -o ov
#include <mpi.h>
#include <vector>
#include <cstdio>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    
    const int N = 1 << 20;  // 1M doubles
    std::vector<double> send(N, rank), recv(N, 0);
    int next = (rank + 1) % size;
    int prev = (rank - 1 + size) % size;
    
    // === 阻塞版本 ===
    MPI_Barrier(MPI_COMM_WORLD);
    double t0 = MPI_Wtime();
    for (int it = 0; it < 10; ++it) {
        MPI_Sendrecv(send.data(), N, MPI_DOUBLE, next, 0,
                     recv.data(), N, MPI_DOUBLE, prev, 0,
                     MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        // 模拟计算
        for (int i = 0; i < N; ++i) send[i] += recv[i] * 0.001;
    }
    double t1 = MPI_Wtime();
    
    // === 非阻塞 + overlap ===
    MPI_Barrier(MPI_COMM_WORLD);
    double t2 = MPI_Wtime();
    for (int it = 0; it < 10; ++it) {
        MPI_Request reqs[2];
        MPI_Isend(send.data(), N, MPI_DOUBLE, next, 0, MPI_COMM_WORLD, &reqs[0]);
        MPI_Irecv(recv.data(), N, MPI_DOUBLE, prev, 0, MPI_COMM_WORLD, &reqs[1]);
        // 边通信边计算(这里只算 send 的本地部分,不依赖 recv)
        for (int i = 0; i < N; ++i) send[i] += 0.001;
        MPI_Waitall(2, reqs, MPI_STATUSES_IGNORE);
        for (int i = 0; i < N; ++i) send[i] += recv[i] * 0.001;
    }
    double t3 = MPI_Wtime();
    
    if (rank == 0) {
        printf("Blocking:   %.3fs\n", t1 - t0);
        printf("Non-block+overlap: %.3fs\n", t3 - t2);
    }
    
    MPI_Finalize();
    return 0;
}
```

**典型输出**(2 节点 IB 互联):
```
Blocking:           0.850s
Non-block+overlap:  0.620s   ← 节省 27%
```

### 6.4 生产级版(工程):MPI + OpenMP 混合

```cpp
// file: hybrid_jacobi.cpp
// 编译: mpicxx -O3 -fopenmp hybrid_jacobi.cpp -o hyb
// 运行: mpirun -np 2 -hostfile hosts bash -c \
//       'OMP_NUM_THREADS=8 ./hyb 2048 500'
#include <mpi.h>
#include <omp.h>
#include <vector>
#include <cmath>
#include <cstdio>

int main(int argc, char** argv) {
    int provided;
    // 关键:用 MPI_THREAD_MULTIPLE 支持 OpenMP 多线程调 MPI
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    
    const int N = 2048;
    int my_rows = N / size;
    
    std::vector<double> old_a((my_rows + 2) * N, 0.0);
    std::vector<double> new_a((my_rows + 2) * N, 0.0);
    
    int prev = rank - 1, next = rank + 1;
    if (prev < 0) prev = MPI_PROC_NULL;
    if (next >= size) next = MPI_PROC_NULL;
    
    double t0 = MPI_Wtime();
    
    for (int iter = 0; iter < 500; ++iter) {
        // 1. MPI halo 交换(主线程)
        MPI_Request reqs[4];
        MPI_Isend(&old_a[N], N, MPI_DOUBLE, prev, 0, MPI_COMM_WORLD, &reqs[0]);
        MPI_Isend(&old_a[my_rows * N], N, MPI_DOUBLE, next, 0, MPI_COMM_WORLD, &reqs[1]);
        MPI_Irecv(&old_a[0], N, MPI_DOUBLE, prev, 0, MPI_COMM_WORLD, &reqs[2]);
        MPI_Irecv(&old_a[(my_rows + 1) * N], N, MPI_DOUBLE, next, 0, MPI_COMM_WORLD, &reqs[3]);
        MPI_Waitall(4, reqs, MPI_STATUSES_IGNORE);
        
        // 2. OpenMP 节点内并行计算
        double my_diff = 0.0;
        #pragma omp parallel for reduction(max:my_diff) schedule(static)
        for (int i = 1; i <= my_rows; ++i) {
            for (int j = 1; j < N - 1; ++j) {
                double v = (old_a[(i-1)*N+j] + old_a[(i+1)*N+j] +
                            old_a[i*N+j-1]   + old_a[i*N+j+1]) / 4.0;
                new_a[i*N+j] = v;
                my_diff = std::max(my_diff, std::abs(v - old_a[i*N+j]));
            }
        }
        std::swap(old_a, new_a);
        
        // 3. MPI 跨节点收敛判断
        double g;
        MPI_Allreduce(&my_diff, &g, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
        if (g < 1e-6) break;
    }
    
    double t1 = MPI_Wtime();
    if (rank == 0)
        printf("Hybrid time=%.3fs P=%d threads=%d\n",
               t1-t0, size, omp_get_max_threads());
    
    MPI_Finalize();
    return 0;
}
```

🔥 **混合编程要点**:
- `MPI_Init_thread` 替代 `MPI_Init`,指定线程支持级别;
- `MPI_THREAD_FUNNELED`:仅主线程调 MPI(最常用,性能好);
- `MPI_THREAD_MULTIPLE`:多线程都可调 MPI(性能差,慎用);
- halo 交换用 MPI,本地计算用 OpenMP,各取所长。

### 6.5 手把手:集体通信实战

```cpp
// file: collective_demo.cpp
// 演示 Bcast/Scatter/Gather/Reduce/Allreduce
// 编译: mpicxx -O2 collective_demo.cpp -o cd
// 运行: mpirun -np 4 ./cd
#include <mpi.h>
#include <vector>
#include <cstdio>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    
    const int N = 8;
    std::vector<int> data;
    if (rank == 0) {
        data.resize(N);
        for (int i = 0; i < N; ++i) data[i] = i + 1;
        printf("Root data: ");
        for (int x : data) printf("%d ", x);
        printf("\n");
    }
    
    // 1. Bcast:root 广播整个数组
    std::vector<int> bcast_data(N);
    if (rank == 0) bcast_data = data;
    MPI_Bcast(bcast_data.data(), N, MPI_INT, 0, MPI_COMM_WORLD);
    printf("[rank %d] Bcast: first=%d last=%d\n", rank, bcast_data[0], bcast_data[N-1]);
    
    // 2. Scatter:root 把不同段发给各 rank
    int chunk = N / size;
    std::vector<int> local(chunk);
    MPI_Scatter(data.data(), chunk, MPI_INT,
                local.data(), chunk, MPI_INT, 0, MPI_COMM_WORLD);
    int local_sum = 0;
    for (int x : local) local_sum += x;
    printf("[rank %d] Scatter local sum=%d\n", rank, local_sum);
    
    // 3. Reduce:各 rank 的 local_sum 规约到 root
    int total;
    MPI_Reduce(&local_sum, &total, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    if (rank == 0) printf("Reduce total=%d (expected %d)\n", total, N*(N+1)/2);
    
    // 4. Allreduce:所有 rank 都得到 total
    MPI_Allreduce(&local_sum, &total, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    printf("[rank %d] Allreduce total=%d\n", rank, total);
    
    MPI_Finalize();
    return 0;
}
```

---

## 七、常见陷阱与最佳实践 ⚠️

| 陷阱 | 后果 | 对策 |
|------|------|------|
| 同时 Send 致死锁 | 卡死 | Sendrecv 或非阻塞 |
| Tag 匹配错误 | 收错消息 | 显式 tag,用 MPI_ANY_TAG 慎用 |
| 忘记 Allreduce | 各 rank 收敛判断不一致 | 集体收敛判断 |
| 缓冲区小消息用 Ring | 延迟主导 | MPI 自动选 Tree,大消息才 Ring |
| 集体通信漏一个 rank | 死锁 | 确保所有 rank 都调用 |
| 用 MPI_Send 调试死锁 | 语义不明 | 改 MPI_Ssend |
| halo 数组越界 | 段错误 | +2 行 halo,边界用 MPI_PROC_NULL |
| 每迭代分配内存 | 极慢 | 预分配复用 |
| MPI_Init_thread 级别不够 | OpenMP 调 MPI 崩 | 用 MPI_THREAD_FUNNELED |
| 不测 scaling | 看不出瓶颈 | 测强/弱扩展曲线 |

---

## 八、与其他章节关系

| 章节 | 关系 |
|------|------|
| §1 体系结构 | 分布式内存模型 |
| §3 算法设计 | BSP/LogP 工业实现 |
| §4 OpenMP | 混合编程 |
| §13 集合通信 | 算法实现细节 |
| §15 拓扑感知 | Cart communicator |
| §18 并行 IO | MPI-IO |
| §22 工业案例 | HPC 集群实战 |

---

## 九、面试速答 ⭐

| 问 | 答 |
|----|-----|
| MPI 六核心函数? | Init / Comm_rank / Comm_size / Send / Recv / Finalize |
| SPMD 含义? | Single Program Multiple Data,所有进程同一程序 |
| 阻塞 vs 非阻塞? | 阻塞等完成;非阻塞立即返回,用 Wait 等 |
| 四种 Send 模式? | Standard / Buffered / Synchronous / Ready |
| 怎么避免死锁? | Sendrecv / 非阻塞 / 奇偶交替 |
| 集体通信有哪些? | Bcast/Scatter/Gather/Reduce/Allreduce/Alltoall/Barrier |
| AllReduce vs Reduce? | Reduce 只 root 有结果;AllReduce 所有 rank 都有 |
| MPI_Sendrecv 作用? | 原子 Send+Recv,避免死锁 |
| MPI+OpenMP 优势? | 节点内共享内存免 MPI 开销,内存利用率高 |
| MPI_Init_thread 级别? | SINGLE/FUNNELED/SERIALIZED/MULTIPLE |
| MPI_PROC_NULL? | 空进程,与它通信是 no-op,简化边界代码 |
| halo 交换? | 边界 rank 间交换边界数据,使本地计算有完整邻居 |

---

## 十、综合面试题

1. **基础**:为什么 MPI 程序要 `MPI_Init` 和 `MPI_Finalize` 成对出现?
   - 答:Init 初始化 MPI 运行时(通信器、缓冲池、网络),Finalize 释放资源。两者之间才能调 MPI 函数。Finalize 后所有进程必须已结束通信。

2. **进阶**:4 个 rank 各持 1GB 数据要做 AllReduce。如何估算时间?
   - 答:Ring AllReduce 时间 $\approx 2(P-1)\alpha + \frac{2(P-1)}{P}\frac{n}{\beta}$。$P=4, n=1\text{GB}, \alpha=1\mu s, \beta=25\text{GB/s}$(NVLink)。$T \approx 6\mu s + 60\text{ms} \approx 60\text{ms}$。若跨节点 IB $\beta=25\text{GB/s}$,$T \approx 6\mu s + 60\text{ms}$。带宽主导。

3. **深度**:MPI+OpenMP 混合编程,为何通常选 `MPI_THREAD_FUNNELED` 而非 `MULTIPLE`?
   - 答:FUNNELED 仅主线程调 MPI,MPI runtime 无需加锁,性能最优。MULTIPLE 允许多线程调 MPI,需内部互斥,性能下降 10-30%。实际只需主线程做 halo 交换,OpenMP 做本地计算,无需 MULTIPLE。

4. **设计**:设计一个 2D 域分解的并行 Jacobi(用 Cart communicator)。
   - 答:(1) `MPI_Cart_create` 创建 2D 拓扑;(2) `MPI_Cart_shift` 获取 4 邻居;(3) 每迭代用 `MPI_Sendrecv` 与 4 邻居交换 halo;(4) OpenMP 节点内并行计算;(5) `MPI_Allreduce` 求全局残差。比 1D 域分解通信量减少 $\sqrt{P}$ 倍。

5. **工程**:MPI 程序在 8 节点跑正常,扩到 32 节点加速比只有 1.5x。排查思路?
   - 答:(1) 测弱扩展判断是否负载不均;(2) `mpiP` 或 TAU profiling 看通信占比;(3) 检查 Allreduce 是否成瓶颈(占比 >30% 即问题);(4) 检查网络拓扑(是否跨 switch);(5) 检查 halo 通信是否用非阻塞 overlap;(6) 考虑改用 2D 域分解减少通信。

6. **学术**:证明 Ring AllReduce 带宽最优(在 $\alpha$-$\beta$ 模型下)。
   - 答:AllReduce 每个元素必须被所有 $P$ 个进程"看到",总通信下界 $n(P-1)$ 字节(每元素传 $P-1$ 次)。Ring 中每 rank 总发送量 $2n(P-1)/P$,带宽利用率 $\frac{2n(P-1)/P}{2n} = (P-1)/P \to 1$。Tree 中根节点瓶颈,带宽利用率 $\frac{n}{2n\log P} = 1/(2\log P)$。Ring 带宽利用率更高,故带宽最优。

---

## 十一、参考与延伸

### 11.1 教材
- 《Using MPI》—— Gropp, Lusk, Skjellum(第 3 版)
- 《Parallel Programming with MPI》—— Pacheco
- 《MPI: The Complete Reference》—— Snir et al.

### 11.2 标准
- MPI 3.1(2015,主流)
- MPI 4.0(2021,新增 persistent collective、persistent RMA)

### 11.3 论文
- 📜 Gropp, Lusk, Skjellum, "Using MPI: Portable Parallel Programming" (1994)
- 📜 Gropp et al., "A High-Performance, Portable Implementation of the MPI Message Passing Interface Standard" (MPICH, 1996)

### 11.4 工具
- OpenMPI / MPICH / Intel MPI / MVAPICH
- `mpirun` / `mpiexec`
- `mpiP`(profiling)/ Tau / Scalasca
- `mpi4py`(Python 绑定)

### 11.5 跨文件链接
- [01-并行计算基础与体系结构.md](./01-并行计算基础与体系结构.md) — 分布式内存
- [03-并行算法设计模式.md](./03-并行算法设计模式.md) — BSP/LogP 模型
- [04-共享内存并行-OpenMP.md](./04-共享内存并行-OpenMP.md) — 混合编程
- [13-通信与集体原语.md](./13-通信与集体原语.md) — 算法复杂度
- [15-集合通信与拓扑感知.md](./15-集合通信与拓扑感知.md) — Cart 拓扑
- [18-并行存储与IO.md](./18-并行存储与IO.md) — MPI-IO
- [22-工业案例与千卡训练.md](./22-工业案例与千卡训练.md) — HPC 实战
- [../README.md](./README.md)
