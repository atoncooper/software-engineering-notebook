/**
 * @file page_replacement.h
 * @topic 操作系统 - 内存管理 - 页面置换算法 (OPT/FIFO/LRU/CLOCK/LFU)
 *
 * @考点 408 大纲:操作系统 > 内存管理 > 虚拟内存 > 页面置换
 *   - OPT 最佳置换 (未来最远使用):理论最优,不可实现,作对比
 *   - FIFO 先进先出:简单,但有 Belady 异常 (页框增加缺页反增)
 *   - LRU 最近最久未使用:性能好,实现需硬件支持 (计数器/栈)
 *   - CLOCK 时钟置换 (二次机会):LRU 近似,用访问位循环扫描
 *   - LFU 最少使用:计数器,对新页面不利 (需老化)
 *   - 改进 CLOCK:同时考虑访问位 + 修改位 (优先淘汰 0,0)
 *
 * @业务 工业应用
 *   - Linux 早期用 CLOCK,后期改进为 Active/Inactive 双链表 (类似 LRU)
 *   - Redis 内存回收近似 LRU/LFU (采样)
 *   - 数据库 Buffer Pool (MySQL InnoDB 用改进 LRU)
 *   - CPU Cache 替换策略 (LRU / 伪 LRU)
 *   - CDN 缓存 (LRU-K / TinyLFU)
 *
 * @陷阱 408 高频
 *   - Belady 异常:仅 FIFO 出现,LRU 是栈算法不会出现
 *   - LRU 用栈实现 O(1) (双链表+哈希)
 *   - CLOCK 第一轮找 (0,*) 淘汰,期间变 1 的置 0;第二轮找 (0,*)
 *   - 改进 CLOCK 优先级:(0,0) > (0,1) > (1,0) > (1,1)
 *   - OPT 不可能实现 (无法预知未来)
 */
#ifndef CS408_OS_MEMORY_PAGE_REPLACEMENT_H
#define CS408_OS_MEMORY_PAGE_REPLACEMENT_H

#include "common/types.h"
#include "common/utils.h"
#include <vector>
#include <list>
#include <unordered_map>
#include <algorithm>
#include <iostream>

namespace cs408::os {

// 缺页统计
struct ReplaceResult {
    std::vector<std::vector<int>> frames_per_step;  // 每步页框快照
    int page_faults;
};

// ===== OPT 最佳置换 (未来最远) =====
inline ReplaceResult opt(const std::vector<int>& refs, int n_frames) {
    std::vector<int> frames;
    ReplaceResult r; r.page_faults = 0;
    for (size_t i = 0; i < refs.size(); ++i) {
        int page = refs[i];
        if (std::find(frames.begin(), frames.end(), page) != frames.end()) {
            r.frames_per_step.push_back(frames); continue;
        }
        ++r.page_faults;
        if (static_cast<int>(frames.size()) < n_frames) {
            frames.push_back(page);
        } else {
            // 找未来最远使用 (或不再使用) 的页
            int victim = -1, farthest = -1;
            for (int k = 0; k < static_cast<int>(frames.size()); ++k) {
                int next = INT_MAX;
                for (size_t j = i + 1; j < refs.size(); ++j) {
                    if (refs[j] == frames[k]) { next = static_cast<int>(j); break; }
                }
                if (next > farthest) { farthest = next; victim = k; }
            }
            frames[victim] = page;
        }
        r.frames_per_step.push_back(frames);
    }
    return r;
}

// ===== FIFO 先进先出 =====
inline ReplaceResult fifo(const std::vector<int>& refs, int n_frames) {
    std::vector<int> frames;
    int head = 0;
    ReplaceResult r; r.page_faults = 0;
    for (int page : refs) {
        if (std::find(frames.begin(), frames.end(), page) != frames.end()) {
            r.frames_per_step.push_back(frames); continue;
        }
        ++r.page_faults;
        if (static_cast<int>(frames.size()) < n_frames) {
            frames.push_back(page);
        } else {
            frames[head] = page;
            head = (head + 1) % n_frames;
        }
        r.frames_per_step.push_back(frames);
    }
    return r;
}

// ===== LRU 最近最久未使用 =====
inline ReplaceResult lru(const std::vector<int>& refs, int n_frames) {
    std::list<int> lst;  // 链表头=最近使用,尾=最久未用
    std::unordered_map<int, std::list<int>::iterator> pos;
    ReplaceResult r; r.page_faults = 0;
    for (int page : refs) {
        auto it = pos.find(page);
        if (it != pos.end()) {
            lst.erase(it->second);
            lst.push_front(page);
            pos[page] = lst.begin();
            std::vector<int> snap(lst.begin(), lst.end());
            r.frames_per_step.push_back(snap);
            continue;
        }
        ++r.page_faults;
        if (static_cast<int>(lst.size()) >= n_frames) {
            int victim = lst.back(); lst.pop_back();
            pos.erase(victim);
        }
        lst.push_front(page);
        pos[page] = lst.begin();
        std::vector<int> snap(lst.begin(), lst.end());
        r.frames_per_step.push_back(snap);
    }
    return r;
}

// ===== CLOCK 二次机会 (用访问位) =====
inline ReplaceResult clock(const std::vector<int>& refs, int n_frames) {
    std::vector<int> frames(n_frames, -1);
    std::vector<int> ref_bit(n_frames, 0);
    int hand = 0, count = 0;
    ReplaceResult r; r.page_faults = 0;
    for (int page : refs) {
        auto it = std::find(frames.begin(), frames.end(), page);
        if (it != frames.end()) {
            ref_bit[it - frames.begin()] = 1;
            std::vector<int> snap(frames.begin(), frames.begin() + std::min(count, n_frames));
            r.frames_per_step.push_back(snap);
            continue;
        }
        ++r.page_faults;
        while (true) {
            if (count < n_frames) {
                frames[hand] = page; ref_bit[hand] = 1;
                hand = (hand + 1) % n_frames; ++count; break;
            }
            if (ref_bit[hand] == 0) {
                frames[hand] = page; ref_bit[hand] = 1;
                hand = (hand + 1) % n_frames; break;
            }
            ref_bit[hand] = 0;
            hand = (hand + 1) % n_frames;
        }
        std::vector<int> snap(frames.begin(), frames.end());
        r.frames_per_step.push_back(snap);
    }
    return r;
}

// ===== LFU 最少使用 (计数器) =====
inline ReplaceResult lfu(const std::vector<int>& refs, int n_frames) {
    std::vector<int> frames;
    std::unordered_map<int, int> freq;
    ReplaceResult r; r.page_faults = 0;
    for (int page : refs) {
        if (std::find(frames.begin(), frames.end(), page) != frames.end()) {
            freq[page]++;
            r.frames_per_step.push_back(frames); continue;
        }
        ++r.page_faults;
        if (static_cast<int>(frames.size()) < n_frames) {
            frames.push_back(page); freq[page] = 1;
        } else {
            int victim = -1, min_freq = INT_MAX;
            for (int k = 0; k < static_cast<int>(frames.size()); ++k) {
                if (freq[frames[k]] < min_freq) {
                    min_freq = freq[frames[k]]; victim = k;
                }
            }
            freq.erase(frames[victim]);
            frames[victim] = page; freq[page] = 1;
        }
        r.frames_per_step.push_back(frames);
    }
    return r;
}

// ===== 演示 =====
void page_replacement_demo() {
    std::vector<int> refs = {7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2, 1, 2, 0, 1, 7, 0, 1};
    int n = 3;
    section("页面置换算法对比 (3 页框,访问序列经典例)");
    std::cout << "访问序列: ";
    print_vec(refs);

    auto print_result = [](const std::string& name, const ReplaceResult& r) {
        std::cout << name << " 缺页数: " << r.page_faults
                  << "  缺页率: " << static_cast<double>(r.page_faults) / r.frames_per_step.size() << "\n";
    };
    print_result("OPT  ", opt(refs, n));
    print_result("FIFO ", fifo(refs, n));
    print_result("LRU  ", lru(refs, n));
    print_result("CLOCK", clock(refs, n));
    print_result("LFU  ", lfu(refs, n));

    section("Belady 异常验证 (FIFO: 3 框 vs 4 框)");
    std::vector<int> belady_refs = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};
    std::cout << "FIFO 3 框缺页: " << fifo(belady_refs, 3).page_faults << "\n";
    std::cout << "FIFO 4 框缺页: " << fifo(belady_refs, 4).page_faults << "  (4>3 反而更糟 = Belady)\n";
    std::cout << "LRU  3 框缺页: " << lru (belady_refs, 3).page_faults << "\n";
    std::cout << "LRU  4 框缺页: " << lru (belady_refs, 4).page_faults << "  (LRU 是栈算法,不会 Belady)\n";
}

bool page_replacement_test() {
    std::vector<int> refs = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};
    // 经典 Belady:FIFO 3 框 9 缺页,4 框 10 缺页
    CS408_EXPECT_EQ(fifo(refs, 3).page_faults, 9);
    CS408_EXPECT_EQ(fifo(refs, 4).page_faults, 10);
    // LRU 不会 Belady:4 框缺页数应 <= 3 框
    CS408_EXPECT(lru(refs, 4).page_faults <= lru(refs, 3).page_faults);
    // OPT 一定是最优 (缺页数最少)
    CS408_EXPECT(opt(refs, 3).page_faults <= lru(refs, 3).page_faults);
    return true;
}

CS408_REGISTER_MODULE(
    "operating-systems", "memory.page_replacement", page_replacement,
    "OPT(未来最远)/FIFO/LRU/CLOCK(二次机会)/LFU;Belady 异常仅 FIFO;改进 CLOCK (访问+修改位)",
    "Linux Active/Inactive 双链表;Redis 采样 LRU/LFU;MySQL InnoDB 改进 LRU;CPU Cache 伪 LRU;CDN TinyLFU",
    "Belady 仅 FIFO,LRU 是栈算法;LRU 用双链表+哈希 O(1);CLOCK 找 (0,*),改进 CLOCK 优先 (0,0);OPT 不可实现",
    page_replacement_demo, page_replacement_test
);

} // namespace cs408::os
#endif // CS408_OS_MEMORY_PAGE_REPLACEMENT_H
