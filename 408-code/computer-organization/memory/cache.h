/**
 * @file cache.h
 * @topic 计组 - 存储系统 - Cache 三种映射方式 + 替换 + 写策略
 *
 * @考点 408 大纲:计算机组成原理 > 存储系统 > 高速缓冲存储器
 *   - 直接映射:主存块 j → Cache 行 j mod C (C=Cache 行数)
 *     优点:快;缺点:冲突多
 *   - 全相联映射:主存块可放任意 Cache 行
 *     优点:灵活;缺点:比较器多,慢
 *   - 组相联映射:主存块 j → Cache 组 j mod Q,组内任意放
 *     折中,Q=Cache 组数,组内 N 路相联
 *   - 地址分割:Tag + Set/Index + Block Offset
 *   - 命中率 = 命中次数 / 总访问
 *   - 平均访问时间 T = h × T_c + (1-h) × T_m
 *
 * @业务 工业应用
 *   - CPU L1/L2/L3 Cache (L1 8路,L2 16路)
 *   - TLB (全相联或组相联)
 *   - CDN 边缘缓存 (类似全相联)
 *   - 数据库 Buffer Pool
 *   - 浏览器 HTTP 缓存
 *
 * @陷阱 408 高频
 *   - 直接映射:地址 = Tag + Line + Offset,Line 位数 = log2(行数)
 *   - 全相联:地址 = Tag + Offset (无 Line)
 *   - 组相联:地址 = Tag + Set + Offset,Set 位数 = log2(组数)
 *   - Cache 行数 = 总容量 / (行大小);组数 = 行数 / 路数
 *   - LRU 替换:二路以上常用 LRU,直接映射无须替换 (唯一位置)
 *   - 写策略:全写法 (write-through) + 写缓冲;回写法 (write-back) + 脏位
 *   - 写不命中:写分配 (write-allocate) vs 非写分配 (no-write-allocate)
 *   - 通常搭配:回写+写分配;全写+非写分配
 */
#ifndef CS408_CO_MEMORY_CACHE_H
#define CS408_CO_MEMORY_CACHE_H

#include "common/types.h"
#include "common/utils.h"
#include <vector>
#include <list>
#include <unordered_map>
#include <iostream>
#include <cmath>
#include <algorithm>

namespace cs408::co {

enum class Mapping { DIRECT, FULL_ASSOC, SET_ASSOC };

struct CacheConfig {
    int capacity_bytes;   // Cache 总容量
    int block_bytes;      // 每块大小
    int ways;             // 路数 (1=直接映射, ∞=全相联)
    Mapping mapping;
};

// 计算:地址 = Tag + Index + Offset 各占多少位
struct AddrSplit {
    int tag_bits;
    int index_bits;  // 组号 / 行号
    int offset_bits;
    int num_lines;
    int num_sets;
    int ways;
};

inline AddrSplit analyze(const CacheConfig& cfg) {
    AddrSplit a;
    a.offset_bits = static_cast<int>(std::log2(cfg.block_bytes));
    a.num_lines = cfg.capacity_bytes / cfg.block_bytes;
    a.ways = cfg.ways;
    if (cfg.mapping == Mapping::DIRECT) {
        a.num_sets = a.num_lines;
        a.ways = 1;
    } else if (cfg.mapping == Mapping::FULL_ASSOC) {
        a.num_sets = 1;
        a.ways = a.num_lines;
    } else {
        a.num_sets = a.num_lines / cfg.ways;
    }
    a.index_bits = static_cast<int>(std::log2(a.num_sets));
    a.tag_bits = 32 - a.index_bits - a.offset_bits;  // 假设 32 位地址
    return a;
}

// Cache 模拟器 (LRU 替换,写回+写分配)
class Cache {
public:
    explicit Cache(const CacheConfig& cfg) : cfg_(cfg), a_(analyze(cfg)) {
        sets_.resize(a_.num_sets);
        for (auto& s : sets_) s.lines.resize(a_.ways, {0, false, false, 0});
        hits_ = 0; misses_ = 0;
    }

    bool access(uint32_t addr, bool is_write = false) {
        uint32_t offset_mask = (1u << a_.offset_bits) - 1;
        uint32_t index_mask  = (1u << a_.index_bits) - 1;
        uint32_t offset = addr & offset_mask;
        uint32_t index  = (addr >> a_.offset_bits) & index_mask;
        uint32_t tag    = addr >> (a_.offset_bits + a_.index_bits);
        (void)offset;

        auto& set = sets_[index];
        // 查找
        for (auto& ln : set.lines) {
            if (ln.valid && ln.tag == tag) {
                hits_++;
                ln.dirty = ln.dirty || is_write;
                ln.last_used = ++clock_;
                return true;
            }
        }
        // Miss
        misses_++;
        // 找空行或 LRU
        int victim = 0; uint64_t oldest = UINT64_MAX;
        for (int i = 0; i < static_cast<int>(set.lines.size()); ++i) {
            if (!set.lines[i].valid) { victim = i; break; }
            if (set.lines[i].last_used < oldest) {
                oldest = set.lines[i].last_used; victim = i;
            }
        }
        bool was_dirty = set.lines[victim].dirty;
        if (was_dirty) writebacks_++;
        set.lines[victim] = {tag, true, is_write, ++clock_};
        return false;
    }

    int hits() const { return hits_; }
    int misses() const { return misses_; }
    int writebacks() const { return writebacks_; }
    double hit_rate() const {
        int t = hits_ + misses_;
        return t == 0 ? 0 : static_cast<double>(hits_) / t;
    }

    void print_info() const {
        std::cout << "Cache 配置:\n";
        std::cout << "  映射方式: "
                  << (cfg_.mapping == Mapping::DIRECT ? "直接映射" :
                      cfg_.mapping == Mapping::FULL_ASSOC ? "全相联" : "组相联")
                  << "\n";
        std::cout << "  容量 = " << cfg_.capacity_bytes << " B, 块大小 = "
                  << cfg_.block_bytes << " B\n";
        std::cout << "  行数 = " << a_.num_lines << ", 组数 = "
                  << a_.num_sets << ", 路数 = " << a_.ways << "\n";
        std::cout << "  地址分割: Tag " << a_.tag_bits << " 位 + Index "
                  << a_.index_bits << " 位 + Offset " << a_.offset_bits << " 位\n";
        std::cout << "  命中 " << hits_ << " / 缺失 " << misses_
                  << "  命中率 = " << hit_rate() << "\n";
        std::cout << "  写回次数 = " << writebacks_ << "\n";
    }

private:
    struct Line { uint32_t tag; bool valid; bool dirty; uint64_t last_used; };
    struct Set { std::vector<Line> lines; };
    CacheConfig cfg_;
    AddrSplit a_;
    std::vector<Set> sets_;
    int hits_, misses_;
    int writebacks_ = 0;
    uint64_t clock_ = 0;
};

void cache_demo() {
    section("直接映射 Cache (256B, 块 32B, 1 路)");
    CacheConfig direct{256, 32, 1, Mapping::DIRECT};
    Cache c1(direct);
    for (int x : {0, 32, 64, 96, 0, 32, 64, 96}) c1.access(x);
    c1.print_info();
    std::cout << "  (前 4 次冷缺失,后 4 次全命中)\n\n";

    section("组相联 Cache (256B, 块 32B, 2 路 → 4 组)");
    CacheConfig sa{256, 32, 2, Mapping::SET_ASSOC};
    Cache c2(sa);
    // 0 和 256 都映射到 set 0 (8 mod 4 = 0),2 路可同时容纳
    for (int x : {0, 256, 0, 256}) c2.access(x);
    c2.print_info();
    std::cout << "  (2 路组相联,0 和 256 同组但不同路,第 3、4 次命中)\n\n";

    section("全相联 Cache (256B, 块 32B, 8 路 → 1 组)");
    CacheConfig fa{256, 32, 8, Mapping::FULL_ASSOC};
    Cache c3(fa);
    for (int x : {0, 32, 64, 96, 128, 160, 192, 224, 0, 32}) c3.access(x);
    c3.print_info();
    std::cout << "  (全相联,任何块可放任意位置,最后 2 次命中)\n\n";

    section("地址分割计算示例");
    std::cout << "32 位地址,Cache 32KB, 块 64B, 8 路组相联:\n";
    CacheConfig cpu_l1{32 * 1024, 64, 8, Mapping::SET_ASSOC};
    AddrSplit a = analyze(cpu_l1);
    std::cout << "  行数 = " << a.num_lines << ", 组数 = " << a.num_sets << "\n";
    std::cout << "  Offset = " << a.offset_bits << " 位 (块内寻址)\n";
    std::cout << "  Index  = " << a.index_bits << " 位 (组号)\n";
    std::cout << "  Tag    = " << a.tag_bits << " 位\n";

    section("平均访问时间");
    double h = 0.95, Tc = 1.0, Tm = 100.0;
    double T = h * Tc + (1 - h) * Tm;
    std::cout << "命中率 h = " << h << ", Tc = " << Tc << " ns, Tm = " << Tm << " ns\n";
    std::cout << "T = h×Tc + (1-h)×Tm = " << T << " ns\n";
}

bool cache_test() {
    // 直接映射:0 和 256 都映射到 set 0 (256/32=8, mod 8 = 0),冲突颠簸
    CacheConfig direct{256, 32, 1, Mapping::DIRECT};
    Cache c1(direct);
    c1.access(0);   // miss
    c1.access(256); // miss (替换 0)
    c1.access(0);   // miss (替换 256) - 颠簸
    CS408_EXPECT_EQ(c1.hits(), 0);
    CS408_EXPECT_EQ(c1.misses(), 3);

    // 2 路组相联:0 和 256 同组不同路,不冲突
    CacheConfig sa{256, 32, 2, Mapping::SET_ASSOC};
    Cache c2(sa);
    c2.access(0);   // miss
    c2.access(256); // miss
    c2.access(0);   // hit
    c2.access(256); // hit
    CS408_EXPECT_EQ(c2.hits(), 2);
    CS408_EXPECT_EQ(c2.misses(), 2);

    // 地址分割验证:32KB, 64B 块, 8 路
    CacheConfig l1{32 * 1024, 64, 8, Mapping::SET_ASSOC};
    AddrSplit a = analyze(l1);
    CS408_EXPECT_EQ(a.offset_bits, 6);  // log2(64) = 6
    CS408_EXPECT_EQ(a.num_lines, 512);  // 32KB / 64B
    CS408_EXPECT_EQ(a.num_sets, 64);    // 512 / 8
    CS408_EXPECT_EQ(a.index_bits, 6);   // log2(64) = 6
    CS408_EXPECT_EQ(a.tag_bits, 32 - 6 - 6);  // 20

    return true;
}

CS408_REGISTER_MODULE(
    "computer-organization", "memory.cache", cache,
    "直接映射 j mod C;全相联任意放;组相联 j mod Q 组内 N 路;地址 Tag+Index+Offset;T=hTc+(1-h)Tm",
    "CPU L1/L2/L3;TLB;CDN 边缘;DB Buffer Pool;浏览器 HTTP 缓存",
    "直接映射冲突多;全相联慢;组相联折中;Cache 行=容量/块;组数=行数/路数;LRU/回写+写分配",
    cache_demo, cache_test
);

} // namespace cs408::co
#endif // CS408_CO_MEMORY_CACHE_H
