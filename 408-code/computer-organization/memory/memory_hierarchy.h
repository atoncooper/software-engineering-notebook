/**
 * @file memory_hierarchy.h
 * @topic 计组 - 存储器层次结构 (SRAM/DRAM/多体交叉/主存扩展/ROM)
 *
 * @考点 408 大纲:计算机组成原理 > 存储器 > 主存组织
 *   - SRAM vs DRAM:
 *     · SRAM: 触发器存储, 6T 结构, 不需刷新, 快但贵 → Cache
 *     · DRAM: 电容存储, 1T1C, 需定期刷新 (2ms/64ms), 慢但密度高 → 主存
 *   - DRAM 刷新:
 *     · 集中刷新: 一段时间集中刷新所有行, 有死时间
 *     · 异步刷新: 每隔一段时间刷一行, 分散死时间
 *   - 多体交叉存储器 (提高吞吐):
 *     · 高位交叉: 地址高位选体, 顺序存, 不提速并行
 *     · 低位交叉: 地址低位选体, 流水存取, 提速 (体数 m ≥ T/τ)
 *   - 主存扩展:
 *     · 位扩展: 加宽字长 (8 片 ×1bit → 8bit)
 *     · 字扩展: 增加容量 (片选逻辑选不同芯片)
 *     · 字位同时扩展
 *   - ROM: MROM / PROM / EPROM / EEPROM / Flash (NOR/NAND)
 *
 * @业务 工业应用
 *   - DDR5 DRAM ( Banks / Bank Groups, on-die ECC)
 *   - HBM (高带宽存储器, GPU/AI 加速器)
 *   - SSD NAND Flash (SLC/MLC/TLC/QLC)
 *   - Intel Optane (3D XPoint, 介于 DRAM 和 SSD)
 *   - 嵌入式 SRAM (CPU L1/L2)
 *
 * @陷阱 408 高频
 *   - SRAM 不需刷新, DRAM 需刷新
 *   - 刷新只对 DRAM, 与命中无关
 *   - 低位交叉:连续地址分布在不同体 → 可并行/流水
 *   - 低位交叉存取周期: T/m (理想), 需 m ≥ T/τ
 *   - DRAM 行刷新:每次刷一行, 行数 = 2^(行地址位数)
 *   - 位扩展不改地址, 字扩展增加片选
 */
#ifndef CS408_CO_MEMORY_MEMORY_HIERARCHY_H
#define CS408_CO_MEMORY_MEMORY_HIERARCHY_H

#include "common/types.h"
#include "common/utils.h"
#include <iostream>
#include <vector>
#include <cmath>

namespace cs408::co {

// DRAM 刷新时间计算
struct DRAMRefresh {
    int rows;              // 行数
    double cell_refresh_ms;// 每个单元需刷新的间隔 (2ms / 64ms)
    double row_refresh_us; // 刷新一行的时间

    // 集中刷新:死时间 = rows × row_refresh_us
    double concentrated_dead_time_us() const {
        return rows * row_refresh_us;
    }
    // 集中刷新占比
    double concentrated_dead_ratio() const {
        return concentrated_dead_time_us() / (cell_refresh_ms * 1000);
    }
    // 异步刷新:每隔 cell_refresh_ms/rows 刷一行,每行死时间 = row_refresh_us
    double async_interval_us() const {
        return (cell_refresh_ms * 1000) / rows;
    }
    double async_dead_per_row_us() const {
        return row_refresh_us;  // 每次只死一行时间
    }
};

// 多体交叉存储器
enum class InterleaveMode { HIGH, LOW };  // 高位交叉 / 低位交叉

struct InterleavedMemory {
    int num_banks;        // 体数 m
    double T;             // 存取周期 (μs)
    double tau;           // 总线周期 (μs)
    InterleaveMode mode;

    // 低位交叉:连续地址在不同体,可流水 → 等效周期 = T/m (若 m ≥ T/τ)
    // 否则: T (无加速)
    double effective_period() const {
        if (mode == InterleaveMode::LOW) {
            if (num_banks >= std::ceil(T / tau)) return T / num_banks;
            return T;
        }
        return T;  // 高位交叉无加速
    }
    // 吞吐率 (请求/秒)
    double throughput() const {
        return 1.0 / effective_period();
    }
    // 加速比 vs 单体
    double speedup() const {
        return T / effective_period();
    }
};

// 主存扩展:计算所需芯片数
struct MemoryExpansion {
    // 目标:容量 target_words × word_bits
    // 芯片:chip_words × chip_bits
    static int chip_count(int target_words, int word_bits,
                          int chip_words, int chip_bits) {
        int by_word = (target_words + chip_words - 1) / chip_words;  // 字扩展数
        int by_bit  = (word_bits + chip_bits - 1) / chip_bits;       // 位扩展数
        return by_word * by_bit;
    }
};

void memory_hierarchy_demo() {
    section("SRAM vs DRAM");
    std::cout << "SRAM: 6T 触发器, 不需刷新, 快但贵 → Cache\n";
    std::cout << "DRAM: 1T1C 电容, 需刷新, 慢但密度高 → 主存\n\n";
    std::cout << "        SRAM       DRAM\n";
    std::cout << "存储原理  触发器      电容\n";
    std::cout << "是否刷新  否          是 (2ms/64ms)\n";
    std::cout << "集成度    低          高\n";
    std::cout << "速度      快          慢\n";
    std::cout << "成本      高          低\n";
    std::cout << "功耗      高          低\n";
    std::cout << "用途      Cache       主存\n";

    section("DRAM 刷新计算 (1M×1 位, 刷新周期 2ms, 刷一行 0.5μs)");
    DRAMRefresh r{1024, 2.0, 0.5};  // 1M = 1024 行 × 1024 列
    std::cout << "  行数 = " << r.rows << " (1024 行 × 1024 列)\n";
    std::cout << "  集中刷新: 死时间 = " << r.concentrated_dead_time_us()
              << " μs (" << r.concentrated_dead_ratio() * 100 << "%)\n";
    std::cout << "  异步刷新: 每 " << r.async_interval_us()
              << " μs 刷一行, 死时间仅 " << r.async_dead_per_row_us() << " μs/次\n";
    std::cout << "  → 异步刷新分散死时间, 实时性更好\n";

    section("多体交叉存储器");
    std::cout << "低位交叉: 地址低位选体, 连续地址分布在不同体\n";
    std::cout << "  → 可流水存取, 等效周期 = T/m (需 m ≥ T/τ)\n";
    std::cout << "高位交叉: 地址高位选体, 顺序存, 不提速\n\n";
    InterleavedMemory low{4, 1.0, 0.25, InterleaveMode::LOW};
    InterleavedMemory high{4, 1.0, 0.25, InterleaveMode::HIGH};
    std::cout << "  4 体, T=1μs, τ=0.25μs (m=4 ≥ T/τ=4, 临界)\n";
    std::cout << "  低位交叉: 等效周期 = " << low.effective_period()
              << " μs, 加速比 = " << low.speedup() << "\n";
    std::cout << "  高位交叉: 等效周期 = " << high.effective_period()
              << " μs, 加速比 = " << high.speedup() << "\n";

    InterleavedMemory low2{8, 1.0, 0.25, InterleaveMode::LOW};
    std::cout << "  8 体时: 等效周期 = " << low2.effective_period()
              << " μs (受 T/τ 限制, 最多 4 体有加速效果)\n";

    section("主存扩展 (芯片数计算)");
    std::cout << "  例: 用 16K×8 位芯片组成 64K×16 位主存\n";
    int n = MemoryExpansion::chip_count(64 * 1024, 16, 16 * 1024, 8);
    int by_word = (64 * 1024) / (16 * 1024);  // 4
    int by_bit  = 16 / 8;                      // 2
    std::cout << "  字扩展 = 64K/16K = " << by_word << " 片\n";
    std::cout << "  位扩展 = 16/8 = " << by_bit << " 片\n";
    std::cout << "  总芯片数 = " << by_word << " × " << by_bit << " = " << n << " 片\n";

    section("ROM 分类");
    std::cout << "  MROM  : 掩膜 ROM, 出厂定型\n";
    std::cout << "  PROM  : 可编程 (一次性熔丝)\n";
    std::cout << "  EPROM : 紫外线擦除\n";
    std::cout << "  EEPROM: 电擦除 (字节级)\n";
    std::cout << "  Flash : NOR (随机读, XIP) / NAND (页读, 密度高)\n";

    section("存储层次结构 (从快到慢)");
    std::cout << "  寄存器    < 1 ns     < 1 KB\n";
    std::cout << "  L1 Cache  ~1 ns      ~32 KB\n";
    std::cout << "  L2 Cache  ~10 ns     ~256 KB\n";
    std::cout << "  L3 Cache  ~30 ns     ~32 MB\n";
    std::cout << "  DRAM      ~100 ns    ~16 GB\n";
    std::cout << "  SSD NAND  ~100 μs    ~1 TB\n";
    std::cout << "  HDD       ~10 ms     ~10 TB\n";
    std::cout << "  → 每层 10x 容量增加, 10x 延迟增加\n";
}

bool memory_hierarchy_test() {
    // 集中刷新死时间: 1024 行 × 0.5μs = 512μs
    DRAMRefresh r{1024, 2.0, 0.5};
    CS408_EXPECT_EQ(r.concentrated_dead_time_us(), 512.0);
    // 死时间占比 = 512μs / 2ms = 25.6%
    CS408_EXPECT(r.concentrated_dead_ratio() > 0.25 && r.concentrated_dead_ratio() < 0.26);

    // 异步刷新间隔
    // 2ms / 1024 行 = 1.953 μs
    double interval = r.async_interval_us();
    CS408_EXPECT(interval > 1.9 && interval < 2.0);

    // 多体交叉
    InterleavedMemory low{4, 1.0, 0.25, InterleaveMode::LOW};
    // m=4, T/τ=4, 满足 m ≥ T/τ, 等效 = T/m = 0.25
    CS408_EXPECT_EQ(low.effective_period(), 0.25);
    CS408_EXPECT_EQ(low.speedup(), 4.0);

    InterleavedMemory low_short{2, 1.0, 0.25, InterleaveMode::LOW};
    // m=2 < T/τ=4, 不能完整流水, 退化为 T
    CS408_EXPECT_EQ(low_short.effective_period(), 1.0);

    InterleavedMemory high{4, 1.0, 0.25, InterleaveMode::HIGH};
    CS408_EXPECT_EQ(high.effective_period(), 1.0);  // 高位无加速

    // 芯片数计算: 64K×16 用 16K×8 → 4×2 = 8
    CS408_EXPECT_EQ(MemoryExpansion::chip_count(64*1024, 16, 16*1024, 8), 8);
    // 1M×8 用 256K×4 → 4×2 = 8
    CS408_EXPECT_EQ(MemoryExpansion::chip_count(1024*1024, 8, 256*1024, 4), 8);

    return true;
}

CS408_REGISTER_MODULE(
    "computer-organization", "memory.memory_hierarchy", memory_hierarchy,
    "SRAM 触发器/DRAM 电容需刷新;低位交叉 T/m 需 m≥T/τ;位扩展+字扩展;ROM 5 类型",
    "DDR5 on-die ECC;HBM GPU;SSD SLC/MLC/TLC/QLC;Intel Optane;嵌入式 SRAM",
    "SRAM 不刷新 DRAM 刷;低位交叉加速, 高位不加速;集中刷新有死时间异步刷新分散;片数=字扩×位扩",
    memory_hierarchy_demo, memory_hierarchy_test
);

} // namespace cs408::co
#endif // CS408_CO_MEMORY_MEMORY_HIERARCHY_H
