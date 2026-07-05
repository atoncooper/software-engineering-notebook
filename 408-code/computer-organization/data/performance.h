/**
 * @file performance.h
 * @topic 计组 - 系统概述 - 性能指标 (CPU 时间/CPI/Amdahl/MIPS)
 *
 * @考点 408 大纲:计算机组成原理 > 系统概述 > 性能指标
 *   - 主频 f 与时钟周期 T = 1/f
 *   - CPI (Cycles Per Instruction):平均每条指令所需时钟周期数
 *   - CPU 时间 = IC × CPI × T = (IC × CPI) / f
 *   - MIPS = IC / (CPU 时间 × 10^6) = f / (CPI × 10^6)
 *   - FLOPS = 每秒浮点运算次数 (科学计算常用)
 *   - Amdahl 定律:加速比 S = 1 / ((1-p) + p/s)
 *     p=可加速部分占比, s=加速倍数
 *   - 系统加速比 ≤ 1/(1-p) (上限,当 s→∞)
 *
 * @业务 工业应用
 *   - SPEC CPU2017 / SPECint / SPECfp 基准测试
 *   - Geekbench / Cinebench 跑分
 *   - GPU TFLOPS 排行 (TOP500)
 *   - Apple Silicon vs x86 性能对比 (IPC + 频率)
 *   - 数据中心 TCO 优化 (perf/watt)
 *
 * @陷阱 408 高频
 *   - CPI 是平均值,不同指令类型 CPI 不同 → 加权 CPI = Σ(CPI_i × f_i)
 *   - MIPS 不能跨架构对比 (RISC CPI≈1 但 MIPS 高;CISC CPI 大但单条指令多做功)
 *   - Amdahl 上限:S ≤ 1/(1-p),p 决定理论上限
 *   - 频率提升不一定提速 (CPI 也变)
 *   - 时间是唯一可靠度量:基准测试比单维指标更可信
 */
#ifndef CS408_CO_DATA_PERFORMANCE_H
#define CS408_CO_DATA_PERFORMANCE_H

#include "common/types.h"
#include "common/utils.h"
#include <iostream>
#include <vector>
#include <string>

namespace cs408::co {

// CPU 时间 = IC × CPI × T = IC × CPI / f
// IC: 指令数, CPI: 每条指令周期数, f: 频率 (Hz)
inline double cpu_time(double IC, double CPI, double freq_hz) {
    return (IC * CPI) / freq_hz;
}

// MIPS = IC / (CPU 时间 × 10^6) = f / (CPI × 10^6)
inline double mips(double freq_hz, double CPI) {
    return freq_hz / (CPI * 1e6);
}

// 加权 CPI:多类指令混合
struct InstrMix { double count; double cpi; };

inline double weighted_cpi(const std::vector<InstrMix>& mix) {
    double total_cycles = 0, total_instr = 0;
    for (const auto& m : mix) {
        total_cycles += m.count * m.cpi;
        total_instr  += m.count;
    }
    return total_cycles / total_instr;
}

inline double total_cpu_time(const std::vector<InstrMix>& mix, double freq_hz) {
    double total_cycles = 0;
    for (const auto& m : mix) total_cycles += m.count * m.cpi;
    return total_cycles / freq_hz;
}

// Amdahl 定律: S = 1 / ((1-p) + p/s)
inline double amdahl(double p, double s) {
    return 1.0 / ((1.0 - p) + p / s);
}

// 系统加速比上限 (s→∞)
inline double amdahl_upper_bound(double p) {
    return 1.0 / (1.0 - p);
}

void performance_demo() {
    section("CPU 时间公式");
    std::cout << "CPU 时间 = IC × CPI / f\n";
    double IC = 1e9, CPI = 2.0, f = 3e9;
    std::cout << "  IC=" << IC << " 条, CPI=" << CPI << ", f=" << f/1e9 << " GHz\n";
    std::cout << "  CPU 时间 = " << cpu_time(IC, CPI, f) << " s\n";
    std::cout << "  MIPS = " << mips(f, CPI) << "\n";

    section("加权 CPI (多类指令混合)");
    std::vector<InstrMix> mix = {
        {5e8, 1.0},  // ALU 指令, 5 亿次, CPI=1
        {3e8, 2.0},  // Load/Store, CPI=2
        {1e8, 5.0},  // 分支, CPI=5
        {1e8, 10.0}, // 乘除, CPI=10
    };
    double avg_cpi = weighted_cpi(mix);
    double t = total_cpu_time(mix, f);
    std::cout << "  ALU 5亿×1 + LS 3亿×2 + BR 1亿×5 + MUL 1亿×10\n";
    std::cout << "  加权 CPI = " << avg_cpi << "\n";
    std::cout << "  总 CPU 时间 (3GHz) = " << t << " s\n";
    std::cout << "  MIPS = " << mips(f, avg_cpi) << "\n";

    section("Amdahl 定律");
    std::cout << "公式: S = 1 / ((1-p) + p/s)\n";
    std::cout << "  p=可加速比例, s=加速倍数\n\n";
    double p = 0.6;  // 60% 可加速
    for (double s : {2.0, 5.0, 10.0, 100.0, 1e6}) {
        std::cout << "  p=" << p << ", s=" << s
                  << " → S=" << amdahl(p, s) << "\n";
    }
    std::cout << "  上限 (s→∞): S = " << amdahl_upper_bound(p) << "\n";
    std::cout << "  → 即使无限加速 60% 部分,系统最多提速 " << amdahl_upper_bound(p) << " 倍\n";

    section("Amdahl 教训:加速比上限");
    std::cout << "  p=0.5 → 上限 S=2\n";
    std::cout << "  p=0.9 → 上限 S=10\n";
    std::cout << "  p=0.99 → 上限 S=100\n";
    std::cout << "  → 必须让可加速部分接近 100% 才能大幅提速\n";

    section("MIPS 跨架构对比的陷阱");
    std::cout << "  RISC (CPI≈1, 高频) → MIPS 高,但需更多指令完成同样工作\n";
    std::cout << "  CISC (CPI 大,低频) → MIPS 低,但单条指令多做功\n";
    std::cout << "  → 用 CPU 时间 (秒) 才是公平比较\n";

    section("真实数据参考 (2025)");
    std::cout << "  Apple M4: 4GHz, IPC≈6 (宽发射) → 单核 SPECint ~4000\n";
    std::cout << "  Intel Core Ultra 9 285K: 5.7GHz, IPC≈3\n";
    std::cout << "  AMD Zen 5 965X: 5.4GHz\n";
    std::cout << "  NVIDIA H100: 60 TFLOPS FP64\n";
}

bool performance_test() {
    // CPU 时间
    CS408_EXPECT_EQ(cpu_time(1e9, 2.0, 3e9), 2.0/3.0);
    // MIPS
    CS408_EXPECT_EQ(mips(3e9, 2.0), 1500.0);  // 3e9 / (2 * 1e6) = 1500
    // 加权 CPI
    std::vector<InstrMix> mix = {{5,1},{3,2},{1,5},{1,10}};
    // (5*1 + 3*2 + 1*5 + 1*10) / (5+3+1+1) = (5+6+5+10)/10 = 26/10 = 2.6
    CS408_EXPECT_EQ(weighted_cpi(mix), 2.6);
    // Amdahl
    // p=0.6, s=2 → 1/(0.4 + 0.3) = 1/0.7 ≈ 1.4286
    double a = amdahl(0.6, 2.0);
    CS408_EXPECT(a > 1.42 && a < 1.43);
    // 上限
    CS408_EXPECT_EQ(amdahl_upper_bound(0.6), 2.5);  // 1/0.4
    // 加速比必须 ≤ 上限
    CS408_EXPECT(amdahl(0.6, 1000.0) < amdahl_upper_bound(0.6));
    return true;
}

CS408_REGISTER_MODULE(
    "computer-organization", "data.performance", performance,
    "CPU时间=IC×CPI/f;MIPS=f/(CPI×10^6);加权CPI=Σ(CPI_i×f_i);Amdahl S=1/((1-p)+p/s)",
    "SPEC CPU2017;Geekbench;TOP500 FLOPS;Apple vs x86;perf/watt",
    "CPI加权平均;MIPS不能跨架构;Amdahl上限=1/(1-p);时间是唯一可靠度量",
    performance_demo, performance_test
);

} // namespace cs408::co
#endif // CS408_CO_DATA_PERFORMANCE_H
