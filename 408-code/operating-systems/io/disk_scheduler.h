/**
 * @file disk_scheduler.h
 * @topic 操作系统 - IO管理 - 磁盘调度 (FCFS/SSTF/SCAN/C-SCAN/LOOK/C-LOOK)
 *
 * @考点 408 大纲:操作系统 > IO管理 > 磁盘调度
 *   - 磁盘访问时间 = 寻道时间 + 旋转延迟 + 传输时间
 *   - 寻道时间是主要优化目标
 *   - FCFS 先来先服务:简单公平,寻道距离长
 *   - SSTF 最短寻道时间优先:可能饥饿 (远处请求)
 *   - SCAN 电梯算法:朝一方向扫到端,反向;无饥饿
 *   - C-SCAN 循环扫描:朝一方向扫到端,直接回起点再同向
 *   - LOOK/C-LOOK:SCAN/C-SCAN 的改进,不扫到端,扫到最远请求即反
 *
 * @业务 工业应用
 *   - Linux 内核 IO 调度器 (CFQ/Deadline/Anticipatory/None)
 *   - SSD 调度 (无寻道,只需合并请求)
 *   - 数据库查询优化 (范围扫描)
 *
 * @陷阱 408 高频
 *   - SSTF 可能饥饿远处请求
 *   - SCAN 必扫到端 (0 或 max);LOOK 只扫到最远请求
 *   - C-SCAN 回程不服务请求,直接回起点
 *   - 寻道距离 = |当前位置 - 下一请求|,然后累加
 *   - 真题给方向 (向上/向下) 必须遵守
 */
#ifndef CS408_OS_IO_DISK_SCHEDULER_H
#define CS408_OS_IO_DISK_SCHEDULER_H

#include "common/types.h"
#include "common/utils.h"
#include <vector>
#include <algorithm>
#include <iostream>
#include <numeric>

namespace cs408::os {

struct DiskScheduleResult {
    std::vector<int> order;        // 服务顺序
    int total_seek;                // 总寻道距离
};

inline void print_disk_result(const std::string& name, const DiskScheduleResult& r) {
    std::cout << name << ": ";
    for (int x : r.order) std::cout << x << " ";
    std::cout << "  总寻道距离 = " << r.total_seek << "\n";
}

// ===== FCFS 先来先服务 =====
inline DiskScheduleResult fcfs_disk(int head, const std::vector<int>& reqs) {
    DiskScheduleResult r; r.order = reqs;
    r.total_seek = 0;
    int cur = head;
    for (int x : reqs) {
        r.total_seek += std::abs(x - cur);
        cur = x;
    }
    return r;
}

// ===== SSTF 最短寻道优先 =====
inline DiskScheduleResult sstf(int head, std::vector<int> reqs) {
    DiskScheduleResult r; r.total_seek = 0;
    int cur = head;
    while (!reqs.empty()) {
        auto it = std::min_element(reqs.begin(), reqs.end(),
            [cur](int a, int b){ return std::abs(a - cur) < std::abs(b - cur); });
        r.total_seek += std::abs(*it - cur);
        cur = *it;
        r.order.push_back(cur);
        reqs.erase(it);
    }
    return r;
}

// ===== SCAN 电梯算法 (向 dir 方向扫到端,反向) =====
// dir=+1 向上 (大磁道), dir=-1 向下
inline DiskScheduleResult scan(int head, std::vector<int> reqs, int dir, int max_cylinder) {
    DiskScheduleResult r; r.total_seek = 0;
    std::sort(reqs.begin(), reqs.end());
    int cur = head;
    if (dir == 1) {
        // 向上扫到 max,再向下
        for (int x : reqs) if (x >= head) {
            r.total_seek += std::abs(x - cur); cur = x; r.order.push_back(cur);
        }
        // 扫到端
        if (cur != max_cylinder) { r.total_seek += std::abs(max_cylinder - cur); cur = max_cylinder; }
        // 反向
        for (int i = reqs.size() - 1; i >= 0; --i) if (reqs[i] < head) {
            r.total_seek += std::abs(reqs[i] - cur); cur = reqs[i]; r.order.push_back(cur);
        }
    } else {
        // 向下扫到 0,再向上
        for (int i = reqs.size() - 1; i >= 0; --i) if (reqs[i] <= head) {
            r.total_seek += std::abs(reqs[i] - cur); cur = reqs[i]; r.order.push_back(cur);
        }
        if (cur != 0) { r.total_seek += std::abs(0 - cur); cur = 0; }
        for (int x : reqs) if (x > head) {
            r.total_seek += std::abs(x - cur); cur = x; r.order.push_back(cur);
        }
    }
    return r;
}

// ===== C-SCAN 循环扫描 (扫到端,直接回起点再同向) =====
inline DiskScheduleResult cscan(int head, std::vector<int> reqs, int max_cylinder) {
    DiskScheduleResult r; r.total_seek = 0;
    std::sort(reqs.begin(), reqs.end());
    int cur = head;
    // 向上扫到 max
    for (int x : reqs) if (x >= head) {
        r.total_seek += std::abs(x - cur); cur = x; r.order.push_back(cur);
    }
    if (cur != max_cylinder) { r.total_seek += std::abs(max_cylinder - cur); cur = max_cylinder; }
    // 回到 0 (不服务请求)
    r.total_seek += max_cylinder; cur = 0;
    // 继续向上扫
    for (int x : reqs) if (x < head) {
        r.total_seek += std::abs(x - cur); cur = x; r.order.push_back(cur);
    }
    return r;
}

// ===== LOOK (不扫到端,扫到最远请求即反) =====
inline DiskScheduleResult look(int head, std::vector<int> reqs, int dir) {
    DiskScheduleResult r; r.total_seek = 0;
    std::sort(reqs.begin(), reqs.end());
    int cur = head;
    if (dir == 1) {
        for (int x : reqs) if (x >= head) {
            r.total_seek += std::abs(x - cur); cur = x; r.order.push_back(cur);
        }
        for (int i = reqs.size() - 1; i >= 0; --i) if (reqs[i] < head) {
            r.total_seek += std::abs(reqs[i] - cur); cur = reqs[i]; r.order.push_back(cur);
        }
    } else {
        for (int i = reqs.size() - 1; i >= 0; --i) if (reqs[i] <= head) {
            r.total_seek += std::abs(reqs[i] - cur); cur = reqs[i]; r.order.push_back(cur);
        }
        for (int x : reqs) if (x > head) {
            r.total_seek += std::abs(x - cur); cur = x; r.order.push_back(cur);
        }
    }
    return r;
}

// ===== C-LOOK =====
inline DiskScheduleResult clook(int head, std::vector<int> reqs) {
    DiskScheduleResult r; r.total_seek = 0;
    std::sort(reqs.begin(), reqs.end());
    int cur = head;
    for (int x : reqs) if (x >= head) {
        r.total_seek += std::abs(x - cur); cur = x; r.order.push_back(cur);
    }
    // 跳回最小请求 (不回 0)
    if (!reqs.empty()) {
        auto it = std::min_element(reqs.begin(), reqs.end());
        r.total_seek += std::abs(*it - cur); cur = *it; r.order.push_back(cur);
        for (int x : reqs) if (x < head) {
            r.total_seek += std::abs(x - cur); cur = x; r.order.push_back(cur);
        }
    }
    return r;
}

// ===== 演示 (经典 408 推演题) =====
void disk_scheduler_demo() {
    int head = 53;
    std::vector<int> reqs = {98, 183, 37, 122, 14, 124, 65, 67};
    int max_cyl = 199;
    section("磁盘调度算法对比 (起始 53, 请求队列 98,183,37,122,14,124,65,67)");
    std::cout << "请求队列: "; print_vec(reqs);
    std::cout << "起始位置: " << head << "  最大柱面: " << max_cyl << "\n\n";
    print_disk_result("FCFS   ", fcfs_disk(head, reqs));
    print_disk_result("SSTF   ", sstf(head, reqs));
    print_disk_result("SCAN↑  ", scan(head, reqs, +1, max_cyl));
    print_disk_result("C-SCAN ", cscan(head, reqs, max_cyl));
    print_disk_result("LOOK↑  ", look(head, reqs, +1));
    print_disk_result("C-LOOK ", clook(head, reqs));
}

bool disk_scheduler_test() {
    int head = 53;
    std::vector<int> reqs = {98, 183, 37, 122, 14, 124, 65, 67};
    // FCFS: 53→98 (45) →183 (85) →37 (146) →122 (85) →14 (108) →124 (110) →65 (59) →67 (2) = 640
    CS408_EXPECT_EQ(fcfs_disk(head, reqs).total_seek, 640);
    // SSTF: 53→65 (12) →67 (2) →37 (30) →14 (23) →98 (84) →122 (24) →124 (2) →183 (59) = 236
    CS408_EXPECT_EQ(sstf(head, reqs).total_seek, 236);
    // SCAN↑: 53→65 (12) →67 (2) →98 (31) →122 (24) →124 (2) →183 (59) →199 (16) →37 (162) →14 (23) = 331
    CS408_EXPECT_EQ(scan(head, reqs, +1, 199).total_seek, 331);
    return true;
}

CS408_REGISTER_MODULE(
    "operating-systems", "io.disk_scheduler", disk_scheduler,
    "FCFS/SSTF/SCAN/C-SCAN/LOOK/C-LOOK;寻道时间主要优化;LOOK 不扫端只扫最远请求;C-SCAN 回程不服务",
    "Linux CFQ/Deadline/Anticipatory;SSD 仅合并;数据库范围扫描",
    "SSTF 可能饥饿;SCAN 扫到端 LOOK 不扫;C-SCAN 回程不服务直接回起点;真题方向必须遵守",
    disk_scheduler_demo, disk_scheduler_test
);

} // namespace cs408::os
#endif // CS408_OS_IO_DISK_SCHEDULER_H
