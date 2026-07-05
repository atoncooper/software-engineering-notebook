# 408-in-code

> 用工业级 C/C++ 代码实现 408 (数据结构 / 操作系统 / 计算机组成原理 / 计算机网络) 全部核心知识点,每个模块从 **代码 / 业务 / 考点** 三视角注释,可运行演示 + 单元测试 + CLI 浏览。

---

## 设计哲学

| 原则 | 说明 |
|------|------|
| 代码即文档 | 每个类/函数 docstring 含 `考点` `业务` `陷阱` 三段 |
| 可运行 | 每个模块自带 `demo()` 与 `test()`,主程序可调 |
| 工业化 | 现代风格 (RAII、智能指针、模板),关键路径保留 C 风格以便映射 408 伪代码 |
| 三视角 | 代码角度 (实现细节) + 业务角度 (真实系统哪里用) + 考点角度 (408 怎么考) |

## 目录结构

```
408-code/
├── common/                       # 通用工具 (类型、测试框架、打印)
├── data-structures/              # 数据结构
│   ├── linear/                   # 线性表 (链表/栈/队列)
│   ├── tree/                     # 树 (BST/AVL/堆/Huffman)
│   ├── graph/                    # 图 (BFS/DFS/MST/最短路/拓扑)
│   ├── search/                   # 查找 (二分/哈希/B树)
│   └── sort/                     # 排序 (比较/非比较)
├── operating-systems/            # 操作系统
│   ├── process/                  # 进程 (调度/同步/经典模型)
│   ├── deadlock/                 # 死锁 (银行家/检测)
│   ├── memory/                   # 内存 (分页/TLB/置换)
│   ├── filesystem/               # 文件 (inode/FAT/位图)
│   └── io/                       # IO (磁盘调度/缓冲/SPOOLing)
├── computer-organization/        # 计算机组成原理
│   ├── data/                     # 数据表示 (IEEE754/Hamming/CRC/Booth)
│   ├── cpu/                      # CPU (寄存器/流水线/分支预测)
│   ├── memory/                   # 存储器 (Cache 映射/多级)
│   ├── instruction/              # 指令 (ISA/寻址)
│   └── bus/                      # 总线 (仲裁)
├── networks/                     # 网络
│   ├── physical/                 # 物理层 (奈氏/香农/编码/CDMA)
│   ├── datalink/                 # 链路层 (滑动窗口/CSMA-CD/交换机)
│   ├── network/                  # 网络层 (IP/子网/ARP/路由)
│   ├── transport/                # 传输层 (TCP 状态机/拥塞控制)
│   └── application/              # 应用层 (DNS/HTTP/SMTP)
├── tests/                        # 集成测试
├── 408-cli.cpp                   # CLI 浏览器入口
└── CMakeLists.txt                # 构建系统
```

## 构建

```bash
# 需要 CMake 3.16+ 与 C++17 编译器
cd 408-code
cmake -B build -S .
cmake --build build --config Release

# 运行 CLI
./build/408-cli list                       # 列出所有模块
./build/408-cli show data-structures.sort  # 查看某模块
./build/408-cli demo os.scheduler          # 运行演示
./build/408-cli test all                   # 跑全部测试
```

## 模块模板 (每个旗舰模块都遵循)

```cpp
/**
 * @file xxx.h
 * @topic 数据结构 - 排序 - 快速排序
 *
 * @考点 408 大纲位置:数据结构 > 排序 > 交换排序
 *   - 时间复杂度:平均 O(n log n),最坏 O(n²) (已有序)
 *   - 空间复杂度:O(log n) (递归栈)
 *   - 稳定性:不稳定 (跨距离交换)
 *   - 真题陷阱:Partition 选枢轴方式 (首/中/三数取中)
 *
 * @业务 工业应用
 *   - std::sort 混合排序 (IntroSort) 的内核:递归深度过深切堆排
 *   - 数据库 ExternalSort 内存排序阶段
 *   - Linux kernel lib/sort.c 用快速排序
 *
 * @陷阱 408 高频考点
 *   - 第一趟排序后,枢轴必在最终位置
 *   - 序列基本有序时退化为 O(n²),可用随机化枢轴避免
 *   - 三数取中法:取首/中/末的中位数作枢轴
 */
```

## 与 408 笔记的关系

- 笔记 (`../408/`) 是**理论**:形式化定义、公式推导、ASCII 图、真题陷阱
- 本项目 (`408-code/`) 是**代码**:可运行实现、工业映射、测试验证

两者互为补充,`@考点` 段落含 `[[xx-章节]]` 链接指向笔记。

## 进度

- [x] P1 项目骨架 (build + CLI + 通用)
- [x] P2 数据结构旗舰 (链表/栈/队列/BST/堆/图/排序/哈希)
- [x] P3 操作系统旗舰 (调度/同步/银行家/页面置换/磁盘调度)
- [x] P4 计组旗舰 (IEEE754/CRC/Hamming/Cache/流水线/分支预测)
- [x] P5 网络旗舰 (奈氏香农/滑动窗口/CSMA-CD/子网/TCP 状态机/拥塞控制)
- [ ] 全 4 科剩余模块补全 (按需扩展)
