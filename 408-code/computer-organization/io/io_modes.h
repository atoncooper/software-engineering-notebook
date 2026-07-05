/**
 * @file io_modes.h
 * @topic 计组 - 输入输出系统 (4 种方式 + 中断 + DMA + 通道)
 *
 * @考点 408 大纲:计算机组成原理 > 输入输出系统
 *   - I/O 控制方式 4 种 (由低到高):
 *     1) 程序查询 (polling): CPU 不断读状态寄存器, 等待设备就绪
 *        → CPU 浪费严重, 适合慢速简单设备
 *     2) 程序中断: 设备就绪后发中断, CPU 跳到 ISR 处理
 *        → CPU 与 IO 并行, 但每次传输需中断 (开销大)
 *     3) DMA (直接存储器访问): DMA 控制器直接管理主存与设备间传输
 *        → CPU 仅开始/结束介入, 传输期间 CPU 可并行工作
 *     4) 通道: 专用 IO 处理器, 能执行通道指令
 *        → CPU 委派一组任务给通道, 完全并行
 *   - 中断处理 5 步:
 *     1) 关中断 (防止嵌套)
 *     2) 保存断点 (PC, PSW 入栈)
 *     3) 取中断向量 → PC (跳转到 ISR)
 *     4) 开中断 (允许更高优先级)
 *     5) 执行 ISR, 最后 IRET 返回
 *   - DMA 三阶段:
 *     1) 预处理: CPU 设置 DMA 参数 (源/目的/长度)
 *     2) 数据传输: DMA 控制器接管总线, 与主存直接传输
 *     3) 后处理: DMA 完成后中断 CPU, CPU 收尾
 *   - I/O 编址:
 *     · 统一编址 (内存映射 IO): IO 与主存同地址空间, 用 MOV 访问
 *     · 独立编址 (端口编址): IO 单独地址空间, 用 IN/OUT 指令
 *   - DMA 与主存冲突:
 *     · 周期挪用 (周期窃取): DMA 在 CPU 不访存的周期插空
 *     · 停止 CPU 访存: DMA 强占, CPU 暂停
 *     · 交替访问: DMA 与 CPU 分时
 *
 * @业务 工业应用
 *   - x86 IN/OUT 指令 (独立编址, 64KB IO 端口)
 *   - ARM 内存映射 IO (统一编址, MMIO)
 *   - Linux NVMe 驱动 (基于 DMA + 中断)
 *   - GPU DMA (PCIe 分离事务)
 *   - RDMA (远程 DMA, InfiniBand/RoCE)
 *
 * @陷阱 408 高频
 *   - 4 种方式 CPU 介入程度递减
 *   - 中断响应优先级 > 中断服务优先级 (硬件 > 软件)
 *   - DMA 不需要 CPU 干预传输过程, 但需要 CPU 启动和收尾
 *   - DMA 周期挪用: 每次 DMA 拿走 1 个存储周期
 *   - 中断 CPU 占用率 = 中断次数 × ISR 时间
 *   - 通道比 DMA 更智能 (能执行通道程序)
 *   - 统一编址: 不能用 IO 指令, 用 MOV; 独立编址: 用 IN/OUT
 */
#ifndef CS408_CO_IO_IO_MODES_H
#define CS408_CO_IO_IO_MODES_H

#include "common/types.h"
#include "common/utils.h"
#include <iostream>
#include <vector>
#include <string>

namespace cs408::co {

// I/O 控制方式
enum class IOMode { POLLING, INTERRUPT, DMA, CHANNEL };

inline const char* io_mode_str(IOMode m) {
    switch (m) {
        case IOMode::POLLING:   return "程序查询";
        case IOMode::INTERRUPT: return "程序中断";
        case IOMode::DMA:       return "DMA";
        case IOMode::CHANNEL:   return "通道";
    }
    return "?";
}

// CPU 占用率分析: 传输 n 字节, 每字节传输时间 t, CPU 介入时间
struct IOAnalysis {
    IOMode mode;
    int bytes;            // 传输字节数
    double t_device;      // 设备每字节时间 (μs)
    double t_cpu_setup;   // CPU 启动时间 (μs)
    double t_isr;         // 中断服务时间 (μs, 中断/DMA 用)

    // CPU 总占用时间
    double cpu_busy_us() const {
        switch (mode) {
            case IOMode::POLLING:
                // CPU 全程忙等
                return bytes * t_device;
            case IOMode::INTERRUPT:
                // 每字节一次中断
                return t_cpu_setup + bytes * t_isr;
            case IOMode::DMA:
                // 仅启动 + 结束中断
                return t_cpu_setup + t_isr;
            case IOMode::CHANNEL:
                // 仅启动 (CPU 委派整组任务)
                return t_cpu_setup;
        }
        return 0;
    }

    // 传输总时间 (含设备)
    double total_time_us() const {
        return t_cpu_setup + bytes * t_device;
    }

    // CPU 占用率
    double cpu_utilization() const {
        return cpu_busy_us() / total_time_us();
    }
};

// 中断处理流程
void print_interrupt_flow() {
    std::cout << "中断响应 5 步 (硬件自动):\n";
    std::cout << "  1) 关中断       (防嵌套保护)\n";
    std::cout << "  2) 保存断点     (PC, PSW 入栈)\n";
    std::cout << "  3) 取中断向量   (中断向量表 → ISR 地址)\n";
    std::cout << "  4) 跳转到 ISR\n";
    std::cout << "  5) 开中断       (允许更高优先级嵌套)\n\n";
    std::cout << "ISR 内容:\n";
    std::cout << "  1) 保护现场 (寄存器入栈)\n";
    std::cout << "  2) 处理中断 (读数据/写数据)\n";
    std::cout << "  3) 恢复现场\n";
    std::cout << "  4) 开中断 + IRET 返回\n";
}

// DMA 三阶段
void print_dma_phases() {
    std::cout << "DMA 三阶段:\n";
    std::cout << "  阶段 1 - 预处理 (CPU 介入):\n";
    std::cout << "    · CPU 设置 DMA 参数 (源/目的地址, 传输长度, 方向)\n";
    std::cout << "    · CPU 启动 DMA, 继续执行其他任务\n\n";
    std::cout << "  阶段 2 - 数据传输 (DMA 独立工作):\n";
    std::cout << "    · DMA 控制器申请总线\n";
    std::cout << "    · DMA 直接管理 主存 ↔ 设备\n";
    std::cout << "    · CPU 与 DMA 分时使用总线 (周期挪用)\n\n";
    std::cout << "  阶段 3 - 后处理 (CPU 介入):\n";
    std::cout << "    · DMA 完成后发中断通知 CPU\n";
    std::cout << "    · CPU 校验数据, 决定是否继续\n";
}

// DMA 与 CPU 总线冲突的 3 种解决方式
void print_dma_bus_conflict() {
    std::cout << "DMA 与 CPU 主存冲突解决:\n";
    std::cout << "  1) 停止 CPU 访存: DMA 强占总线, CPU 暂停\n";
    std::cout << "     → 数据传输连贯, 但 CPU 长时间空闲\n";
    std::cout << "  2) 周期挪用 (周期窃取): DMA 在 CPU 不访存时插入\n";
    std::cout << "     → CPU 影响小, 但 DMA 传输可能不连贯\n";
    std::cout << "  3) 交替访问: CPU 和 DMA 严格分时使用\n";
    std::cout << "     → 适合主存周期 > CPU 周期 2 倍\n";
}

void io_modes_demo() {
    section("I/O 控制 4 种方式 (CPU 介入递减)");
    std::cout << "方式     CPU 介入              传输单位   适用场景\n";
    std::cout << "程序查询 全程忙等               字节       慢速简单设备\n";
    std::cout << "程序中断 每次中断介入           字节/字    中速设备 (键盘/鼠标)\n";
    std::cout << "DMA     仅启动+收尾            块         快速块设备 (磁盘/网卡)\n";
    std::cout << "通道    仅启动 (执行通道程序)  块组       大型机/高速 IO\n\n";

    section("CPU 占用率对比 (传 1KB, 设备 1μs/字节)");
    IOAnalysis polling{IOMode::POLLING, 1024, 1.0, 1.0, 0};
    IOAnalysis intr{IOMode::INTERRUPT, 1024, 1.0, 1.0, 5.0};  // ISR 5μs
    IOAnalysis dma{IOMode::DMA, 1024, 1.0, 10.0, 5.0};        // 启动 10μs
    IOAnalysis ch{IOMode::CHANNEL, 1024, 1.0, 20.0, 0};

    std::cout << "方式       CPU占用时间   总传输时间   CPU占用率\n";
    for (const auto* a : {&polling, &intr, &dma, &ch}) {
        std::cout << io_mode_str(a->mode) << "\t"
                  << a->cpu_busy_us() << " μs\t"
                  << a->total_time_us() << " μs\t"
                  << a->cpu_utilization() << "\n";
    }
    std::cout << "  → DMA/通道 CPU 占用率几乎为 0, 高速 IO 必备\n";

    section("程序查询 vs 中断 (传 1KB)");
    std::cout << "程序查询: CPU 全程等待, 1024μs 全占用\n";
    std::cout << "中断: CPU 每字节介入一次 ISR (5μs), 共 5120μs CPU 时间\n";
    std::cout << "  → 中断适合慢速零散数据, 不适合块设备\n";

    section("中断处理流程");
    print_interrupt_flow();

    section("DMA 三阶段");
    print_dma_phases();

    section("DMA 总线冲突 3 种解决");
    print_dma_bus_conflict();

    section("I/O 编址方式");
    std::cout << "统一编址 (内存映射 IO, MMIO):\n";
    std::cout << "  · IO 寄存器与主存同地址空间\n";
    std::cout << "  · 用 MOV / LOAD / STORE 访问\n";
    std::cout << "  · 优点: 不需专用 IO 指令; 缺点: 占主存空间\n";
    std::cout << "  · ARM, RISC-V, MIPS 用此方式\n\n";
    std::cout << "独立编址 (端口编址, Port IO):\n";
    std::cout << "  · IO 单独地址空间, 与主存分离\n";
    std::cout << "  · 用 IN / OUT 指令访问\n";
    std::cout << "  · 优点: 主存空间不受影响; 缺点: 需专用指令\n";
    std::cout << "  · x86 用此方式 (64KB IO 端口)\n";

    section("通道 (Channel) - 比 DMA 更高级");
    std::cout << "通道是专用 IO 处理器, 能执行通道指令 (通道程序)\n";
    std::cout << "CPU 委派一组任务 → 通道独立执行 → 完成后中断 CPU\n";
    std::cout << "三种通道:\n";
    std::cout << "  · 字节多路通道: 多设备分时, 适合慢速设备\n";
    std::cout << "  · 选择通道: 一次独占一个设备, 适合高速设备\n";
    std::cout << "  · 数组多路通道: 结合两者, 块传输 + 交叉\n";

    section("工业实例");
    std::cout << "NVMe SSD: PCIe DMA + 多队列中断, 7GB/s 读取\n";
    std::cout << "100GbE 网卡: RDMA, 零拷贝, CPU 不介入\n";
    std::cout << "GPU: PCIe DMA 加载纹理, 分离事务\n";
    std::cout << "x86 IN/OUT: 独立编址, 0x60 键盘, 0x3F8 串口\n";
    std::cout << "ARM MMIO: 统一编址, 寄存器映射到 0x40000000\n";
}

bool io_modes_test() {
    // CPU 占用率
    IOAnalysis polling{IOMode::POLLING, 1024, 1.0, 1.0, 0};
    CS408_EXPECT_EQ(polling.cpu_busy_us(), 1024.0);  // 全程
    CS408_EXPECT_EQ(polling.cpu_utilization(), 1024.0 / 1025.0);

    IOAnalysis intr{IOMode::INTERRUPT, 1024, 1.0, 1.0, 5.0};
    // 启动 1 + 1024×5 = 5121 μs CPU 时间
    CS408_EXPECT_EQ(intr.cpu_busy_us(), 5121.0);

    IOAnalysis dma{IOMode::DMA, 1024, 1.0, 10.0, 5.0};
    // 仅启动 10 + 结束中断 5 = 15 μs CPU 时间
    CS408_EXPECT_EQ(dma.cpu_busy_us(), 15.0);
    CS408_EXPECT(dma.cpu_utilization() < 0.02);  // < 2%

    IOAnalysis ch{IOMode::CHANNEL, 1024, 1.0, 20.0, 0};
    CS408_EXPECT_EQ(ch.cpu_busy_us(), 20.0);  // 仅启动

    // 方式枚举数
    CS408_EXPECT_EQ(static_cast<int>(io_mode_str(IOMode::POLLING) != nullptr), 1);

    return true;
}

CS408_REGISTER_MODULE(
    "computer-organization", "io.io_modes", io_modes,
    "4 方式:查询/中断/DMA/通道 CPU 介入递减;中断 5 步;DMA 三阶段;统一 vs 独立编址;周期挪用",
    "x86 IN/OUT;ARM MMIO;NVMe DMA;GPU PCIe DMA;RDMA 零拷贝",
    "4 方式 CPU 介入递减;DMA 仅启动+收尾;周期挪用插空;统一编址 MOV/独立 IN-OUT;通道执行通道程序",
    io_modes_demo, io_modes_test
);

} // namespace cs408::co
#endif // CS408_CO_IO_IO_MODES_H
