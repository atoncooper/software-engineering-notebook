/**
 * @file bus.h
 * @topic 计组 - 总线 (带宽/三种仲裁/同步异步/分离事务)
 *
 * @考点 408 大纲:计算机组成原理 > 总线
 *   - 总线分类:
 *     · 按位置: 片内 (CPU 内) / 系统 (CPU 与主存 IO) / 通信 (计算机间)
 *     · 按功能: 数据总线 DB / 地址总线 AB / 控制总线 CB
 *   - 总线带宽 = 总线宽度 / 总线周期 = (传输字节数) / (秒)
 *     例: 32 位总线, 100MHz → 400 MB/s
 *   - 总线仲裁 (谁获得总线控制权):
 *     1) 链式查询: 一根 BG 线串行连接, 离仲裁器近的优先级高
 *        优点: 简单, 易扩充; 缺点: 不公平, 单点故障, 慢
 *     2) 计数器查询: 仲裁器内部计数, 各设备比较
 *        优点: 优先级可变; 缺点: 线多 (log2 N 根)
 *     3) 独立请求: 每个设备独立请求 + 授权线
 *        优点: 快, 灵活; 缺点: 线多 (2N 根), 复杂
 *   - 通信方式:
 *     · 同步通信: 统一时钟, 适用于速度相近设备 (PCIe/DDR)
 *     · 异步通信: 握手信号 (REQ/ACK), 适用于速度差异大设备
 *       - 不互锁 / 半互锁 / 全互锁
 *     · 半同步: 时钟 + WAIT 信号 (慢设备请求等待)
 *   - 分离事务: 把总线传输分成请求 + 应答两段, 中间释放总线
 *     → 提高总线利用率, 适合长延迟设备 (如 DRAM 读)
 *
 * @业务 工业应用
 *   - PCIe (高速串行, 多 lane, 分离事务)
 *   - DDR5 (差分时钟, on-die termination)
 *   - USB (令牌+数据+握手包, 半双工)
 *   - AXI (ARM 总线, 5 通道分离)
 *   - CXL (cache coherent, 分离事务)
 *
 * @陷阱 408 高频
 *   - 总线带宽 = 宽度 × 频率 / 8 (字节)
 *   - 链式查询优先级: 离仲裁器近 > 远
 *   - 独立请求线数 = 2N (N 设备)
 *   - 计数器查询线数 = log2 N + 1 (设备号 + BR)
 *   - 同步通信需各设备速度相近; 异步用握手
 *   - 分离事务提高吞吐, 但单次请求延迟不变 (甚至略增)
 */
#ifndef CS408_CO_BUS_BUS_H
#define CS408_CO_BUS_BUS_H

#include "common/types.h"
#include "common/utils.h"
#include <iostream>
#include <vector>
#include <string>
#include <cmath>

namespace cs408::co {

// 总线带宽计算
// width_bits: 总线宽度 (位)
// freq_hz: 总线频率 (Hz)
// 返回 MB/s
inline double bus_bandwidth_mbps(int width_bits, double freq_hz) {
    return (width_bits * freq_hz) / 8.0 / 1e6;
}

// 总线周期 = 1 / 频率
inline double bus_cycle_ns(double freq_hz) {
    return 1e9 / freq_hz;
}

// 仲裁方式
enum class Arbitration { CHAIN, COUNTER, INDEPENDENT };

inline const char* arb_str(Arbitration a) {
    switch (a) {
        case Arbitration::CHAIN:        return "链式查询";
        case Arbitration::COUNTER:      return "计数器查询";
        case Arbitration::INDEPENDENT:  return "独立请求";
    }
    return "?";
}

// 不同仲裁方式所需控制线数
struct ArbiterLines {
    int n_devices;  // 设备数

    // 链式查询: BS(总线忙) + BG(总线授权) + BR(总线请求) = 3 根
    int chain_lines() const { return 3; }

    // 计数器查询: BS + BR + log2(N) 设备号线
    int counter_lines() const {
        return 2 + static_cast<int>(std::ceil(std::log2(n_devices)));
    }

    // 独立请求: BS + N×BR + N×BG = 2N + 1
    int independent_lines() const {
        return 2 * n_devices + 1;
    }
};

// 链式查询优先级模拟
// 设备 0 离仲裁器最近, 优先级最高
class ChainArbiter {
public:
    explicit ChainArbiter(int n) : requests_(n, false), grant_(0) {}
    void request(int device) { requests_[device] = true; }
    int arbitrate() {
        for (int i = 0; i < static_cast<int>(requests_.size()); ++i) {
            if (requests_[i]) {
                requests_[i] = false;
                grant_ = i;
                return i;
            }
        }
        return -1;
    }
private:
    std::vector<bool> requests_;
    int grant_;
};

// 计数器查询: 计数器从 0 开始递增, 与请求设备号匹配则授权
class CounterArbiter {
public:
    explicit CounterArbiter(int n) : requests_(n, false), counter_(0) {}
    void request(int device) { requests_[device] = true; }
    int arbitrate() {
        for (int step = 0; step < static_cast<int>(requests_.size()); ++step) {
            int idx = (counter_ + step) % requests_.size();
            if (requests_[idx]) {
                requests_[idx] = false;
                counter_ = (idx + 1) % requests_.size();  // 下次从这里开始
                return idx;
            }
        }
        return -1;
    }
private:
    std::vector<bool> requests_;
    int counter_;
};

// 独立请求: 仲裁器内置优先级编码器
class IndependentArbiter {
public:
    explicit IndependentArbiter(int n) : requests_(n, false) {}
    void request(int device) { requests_[device] = true; }
    int arbitrate() {
        for (int i = 0; i < static_cast<int>(requests_.size()); ++i) {
            if (requests_[i]) {
                requests_[i] = false;
                return i;  // 固定优先级
            }
        }
        return -1;
    }
private:
    std::vector<bool> requests_;
};

// 分离事务模拟 (split transaction)
// 传统: 主设备占用总线整个传输期间 (请求 → 等待 → 数据)
// 分离: 主设备请求 → 释放总线 → 从设备准备 → 仲裁获取总线 → 数据
struct BusTransaction {
    double request_time;   // 请求时刻
    double service_time;   // 服务时间 (从设备准备 + 数据)
    bool split;            // 是否分离事务

    // 占用总线时间 (传统)
    double traditional_hold() const {
        return request_time + service_time;  // 一直占用
    }
    // 占用总线时间 (分离): 2 次短占用
    double split_hold() const {
        return 1.0 + 1.0;  // 请求 1 + 应答 1 (中间不占)
    }
};

void bus_demo() {
    section("总线带宽计算");
    std::cout << "公式: 带宽 = 宽度 × 频率 / 8 (字节/秒)\n";
    std::cout << "  32 位, 100 MHz → "
              << bus_bandwidth_mbps(32, 100e6) << " MB/s\n";
    std::cout << "  64 位, 200 MHz → "
              << bus_bandwidth_mbps(64, 200e6) << " MB/s\n";
    std::cout << "  128 位, 400 MHz (DDR) → "
              << bus_bandwidth_mbps(128, 400e6) << " MB/s\n";

    section("三种总线仲裁对比");
    ArbiterLines a{8};  // 8 设备
    std::cout << "8 个设备的仲裁器所需线数:\n";
    std::cout << "  链式查询:    " << a.chain_lines() << " 根 (BS+BG+BR)\n";
    std::cout << "  计数器查询:  " << a.counter_lines() << " 根 (BS+BR+log2(8) 设备号)\n";
    std::cout << "  独立请求:    " << a.independent_lines() << " 根 (BS + 8BR + 8BG)\n";

    section("链式查询优先级演示 (设备 0~7 优先级递减)");
    ChainArbiter chain(8);
    chain.request(3);
    chain.request(5);
    chain.request(1);
    std::cout << "  同时请求 3, 5, 1 → 授权顺序:\n";
    std::cout << "  第 1 次: " << chain.arbitrate() << " (期望 1, 优先级最高)\n";
    std::cout << "  第 2 次: " << chain.arbitrate() << " (期望 3)\n";
    std::cout << "  第 3 次: " << chain.arbitrate() << " (期望 5)\n";
    std::cout << "  → 缺点: 设备 7 永远最低优先级, 可能饥饿\n";

    section("计数器查询 (轮转起点变化, 较公平)");
    CounterArbiter cnt(8);
    cnt.request(3); cnt.request(5); cnt.request(1);
    std::cout << "  同时请求 3, 5, 1 (计数器从 0 开始):\n";
    std::cout << "  第 1 次: " << cnt.arbitrate() << " (期望 1)\n";
    std::cout << "  第 2 次: " << cnt.arbitrate() << " (从 2 开始, 期望 3)\n";
    std::cout << "  第 3 次: " << cnt.arbitrate() << " (从 4 开始, 期望 5)\n";

    section("同步 vs 异步通信");
    std::cout << "同步通信:\n";
    std::cout << "  · 统一时钟, 周期固定\n";
    std::cout << "  · 各设备速度需相近\n";
    std::cout << "  · 例: PCIe, DDR, PCI\n";
    std::cout << "异步通信 (握手):\n";
    std::cout << "  · REQ → ACK → 数据 → ACK\n";
    std::cout << "  · 速度差异大设备适用\n";
    std::cout << "  · 例: USB, I2C, 异步串口\n";
    std::cout << "  · 三种互锁: 不互锁 / 半互锁 / 全互锁\n";
    std::cout << "半同步: 时钟 + WAIT 信号 (慢设备请求等待)\n";

    section("分离事务 (split transaction)");
    std::cout << "传统事务:\n";
    std::cout << "  主设备发请求 → 占用总线 → 等待从设备 → 数据 → 释放\n";
    std::cout << "  → 总线长时间被占用, 利用率低\n\n";
    std::cout << "分离事务:\n";
    std::cout << "  主设备发请求 → 立即释放总线\n";
    std::cout << "  从设备准备数据 (期间总线可服务其他设备)\n";
    std::cout << "  从设备准备好 → 重新仲裁获取总线 → 传数据\n";
    std::cout << "  → 提高吞吐量, 适合 DRAM/SSD 等长延迟设备\n";

    section("现代总线实例");
    std::cout << "PCIe 5.0: 32 GT/s/lane × 16 lane = 64 GB/s (双向)\n";
    std::cout << "DDR5-6400: 6400 MT/s × 8 字节 = 51.2 GB/s\n";
    std::cout << "USB4: 40 Gbps (5 GB/s)\n";
    std::cout << "AXI (ARM): 5 通道分离 (read addr/data/resp + write addr/data/resp)\n";
    std::cout << "CXL 2.0: cache coherent, 分离事务, 数据中心互联\n";
}

bool bus_test() {
    // 总线带宽
    // 32 位, 100 MHz → 32e8 / 8 = 4e8 = 400 MB/s
    CS408_EXPECT_EQ(bus_bandwidth_mbps(32, 100e6), 400.0);
    // 64 位, 200 MHz → 64 * 2e8 / 8 = 1600 MB/s
    CS408_EXPECT_EQ(bus_bandwidth_mbps(64, 200e6), 1600.0);

    // 总线周期
    // 100 MHz → 10 ns
    CS408_EXPECT_EQ(bus_cycle_ns(100e6), 10.0);

    // 仲裁线数
    ArbiterLines a8{8};
    CS408_EXPECT_EQ(a8.chain_lines(), 3);
    CS408_EXPECT_EQ(a8.counter_lines(), 5);  // 2 + 3
    CS408_EXPECT_EQ(a8.independent_lines(), 17);  // 2*8+1

    ArbiterLines a16{16};
    CS408_EXPECT_EQ(a16.counter_lines(), 6);  // 2 + 4
    CS408_EXPECT_EQ(a16.independent_lines(), 33);  // 2*16+1

    // 链式查询优先级
    ChainArbiter chain(8);
    chain.request(3); chain.request(5); chain.request(1);
    CS408_EXPECT_EQ(chain.arbitrate(), 1);  // 最高
    CS408_EXPECT_EQ(chain.arbitrate(), 3);
    CS408_EXPECT_EQ(chain.arbitrate(), 5);

    // 计数器查询
    CounterArbiter cnt(8);
    cnt.request(3); cnt.request(5); cnt.request(1);
    CS408_EXPECT_EQ(cnt.arbitrate(), 1);  // 从 0 开始找, 1 是最近的
    CS408_EXPECT_EQ(cnt.arbitrate(), 3);  // 从 2 开始, 3 最近
    CS408_EXPECT_EQ(cnt.arbitrate(), 5);  // 从 4 开始, 5 最近

    return true;
}

CS408_REGISTER_MODULE(
    "computer-organization", "bus.bus", bus,
    "带宽=宽度×频率/8;三仲裁 (链式/计数器/独立请求) 线数 3/log2+2/2N+1;同步/异步握手;分离事务",
    "PCIe 5.0;DDR5;USB4;ARM AXI 5 通道;CXL cache coherent",
    "带宽=宽度×频率/8;链式 3 线优先级近>远;计数器 log2+2;独立 2N+1;分离事务提高吞吐不降单次延迟",
    bus_demo, bus_test
);

} // namespace cs408::co
#endif // CS408_CO_BUS_BUS_H
