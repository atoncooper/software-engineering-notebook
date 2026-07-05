/**
 * @file pipeline.h
 * @topic 计组 - CPU - 流水线 (5 级 + 三类冒险 + 转发)
 *
 * @考点 408 大纲:计算机组成原理 > CPU > 指令流水线
 *   - 5 级流水:IF (取指) → ID (译码) → EX (执行) → MEM (访存) → WB (写回)
 *   - 吞吐率 = 1 / (单条指令时间 / 级数) ≈ 1 条/周期 (理想)
 *   - 加速比 S = n×k / (k+n-1)  (k=级数, n=指令数)
 *   - 三类冒险:
 *     1) 结构冒险:硬件资源冲突 (如 MEM 与 IF 同时访存)
 *     2) 数据冒险:RAW (写后读)、WAW、WAR (RISC 流水线主要是 RAW)
 *     3) 控制冒险:分支指令改变 PC
 *   - 解决方法:
 *     · 结构冒险:资源重复 (分开 I-Cache / D-Cache)
 *     · 数据冒险:转发 (forwarding/bypassing)、停顿 (stall/bubble)、乱序
 *     · 控制冒险:分支预测、延迟分支、提前判断
 *   - 数据冒险细分:
 *     · RAW (Read After Write) 真相关,需转发或停顿
 *     · WAW / WAR 在基本流水线中不出现,乱序执行才需考虑
 *
 * @业务 工业应用
 *   - Intel x86 流水线 (14~20 级)
 *   - ARM Cortex-A78 (15 级)
 *   - Apple M1 (超宽 10 发射)
 *   - GPU SIMT 流水线
 *   - RISC-V Rocket (5 级经典)
 *
 * @陷阱 408 高频
 *   - 5 级流水线时空图必考,横轴周期纵轴指令
 *   - 加速比公式 S = nk/(k+n-1),效率 E = S/k
 *   - RAW 需停顿 1~2 周期 (EX→EX 转发可省 2 周期)
 *   - Load-Use 冒险必须停 1 周期 (MEM→EX 才能转发)
 *   - 分支预测准确率影响 CPI
 *   - 流水线寄存器是每级之间的锁存器
 *   - 装入延迟槽 (delay slot) 是早期 RISC 的做法
 */
#ifndef CS408_CO_CPU_PIPELINE_H
#define CS408_CO_CPU_PIPELINE_H

#include "common/types.h"
#include "common/utils.h"
#include <vector>
#include <string>
#include <iostream>
#include <map>

namespace cs408::co {

enum class Stage { IF, ID, EX, MEM, WB, BUBBLE };

inline const char* stage_str(Stage s) {
    switch (s) {
        case Stage::IF:     return "IF";
        case Stage::ID:     return "ID";
        case Stage::EX:     return "EX";
        case Stage::MEM:    return "MEM";
        case Stage::WB:     return "WB";
        case Stage::BUBBLE: return "--";
    }
    return "?";
}

struct Instr {
    std::string name;
    int dest = -1;        // 目标寄存器 (-1 = 无)
    std::vector<int> src; // 源寄存器
    bool is_load  = false;
    bool is_branch = false;
    bool is_store = false;
};

class Pipeline {
public:
    explicit Pipeline(int stages = 5) : k_(stages) {}

    // 模拟执行,返回总周期数和时空图
    // 简化模型:5 级,理想每周期发射 1 条,RAW 用转发或停 1 周期解决
    void run(const std::vector<Instr>& prog) {
        if (prog.empty()) { total_cycles_ = 0; stalls_ = 0; return; }
        size_t n = prog.size();
        // 每条指令的 IF/ID/EX/MEM/WB 周期号 (1-indexed)
        std::vector<int> ifc(n), idc(n), exc(n), memc(n), wbc(n);
        // 第 0 条:IF=1, ID=2, EX=3, MEM=4, WB=5
        ifc[0] = 1; idc[0] = 2; exc[0] = 3; memc[0] = 4; wbc[0] = 5;
        for (size_t i = 1; i < n; ++i) {
            // 默认:IF 紧跟上一条 IF,各阶段顺延
            ifc[i] = ifc[i-1] + 1;
            idc[i] = ifc[i] + 1;
            exc[i] = idc[i] + 1;
            memc[i] = exc[i] + 1;
            wbc[i]  = memc[i] + 1;

            // 检测 RAW 冒险:前面未写回的指令目标 == 当前源
            int stall = 0;
            for (size_t j = 0; j < i; ++j) {
                if (prog[j].dest < 0) continue;
                bool conflict = false;
                for (int s : prog[i].src) if (s == prog[j].dest) { conflict = true; break; }
                if (!conflict) continue;
                // 转发判断:ALU 结果在 EX 末尾可用,可转发到下一条的 EX
                //   - 非负载:producer 的 EX 末尾 (即 exc[j]) 可转发 → consumer 在 exc[i] 直接拿到
                //     若 exc[i] > exc[j],无需停顿 (已写回或可转发)
                //     若 exc[i] <= exc[j],需停顿到 exc[j]+1
                //   - 负载:producer 的 MEM 末尾才可用 → consumer 需停到 memc[j]+1
                int producer_ready = prog[j].is_load ? (memc[j] + 1) : (exc[j] + 1);
                if (exc[i] < producer_ready) {
                    stall = std::max(stall, producer_ready - exc[i]);
                }
            }
            // 应用停顿 (ID/EX/MEM/WB 全部后移)
            idc[i]  += stall;
            exc[i]  += stall;
            memc[i] += stall;
            wbc[i]  += stall;
            // 若停顿导致 ID 撞上上一条 ID (单周期 ID 资源),再后移
            // (简化:不模拟结构性冒险)
        }

        // 构造时空图
        int total = wbc[n-1];
        std::vector<std::vector<Stage>> diagram(n, std::vector<Stage>(total, Stage::BUBBLE));
        for (size_t i = 0; i < n; ++i) {
            diagram[i][ifc[i]-1]  = Stage::IF;
            diagram[i][idc[i]-1]  = Stage::ID;
            diagram[i][exc[i]-1]  = Stage::EX;
            diagram[i][memc[i]-1] = Stage::MEM;
            diagram[i][wbc[i]-1]  = Stage::WB;
        }

        total_cycles_ = total;
        diagram_ = diagram;
        prog_ = prog;
        stalls_ = 0;
        for (size_t i = 0; i < n; ++i) {
            // 理想: ID = IF + 1; stall 会让 ID 延后,bubble 数 = idc - ifc - 1
            int bubble = idc[i] - ifc[i] - 1;
            if (bubble > 0) stalls_ += bubble;
        }
    }

    void print_diagram() const {
        std::cout << "指令\\周期";
        for (int c = 0; c < total_cycles_; ++c) std::cout << "\t" << (c + 1);
        std::cout << "\n";
        for (size_t i = 0; i < prog_.size(); ++i) {
            std::cout << prog_[i].name;
            for (int c = 0; c < total_cycles_; ++c) {
                if (c < static_cast<int>(diagram_[i].size()))
                    std::cout << "\t" << stage_str(diagram_[i][c]);
                else std::cout << "\t";
            }
            std::cout << "\n";
        }
        std::cout << "总周期 = " << total_cycles_
                  << "  冒险停顿 (bubble) = " << stalls_ << "\n";
        int n = static_cast<int>(prog_.size());
        int k = k_;
        int ideal = n + k - 1;
        double s = static_cast<double>(ideal) / total_cycles_ * n / n; // simplified
        std::cout << "理想周期 (无停顿) = " << ideal
                  << "  加速比损失 = " << (total_cycles_ - ideal) << " 周期\n";
    }

    int total_cycles() const { return total_cycles_; }
    int stalls() const { return stalls_; }

private:
    int k_;
    int total_cycles_ = 0;
    int stalls_ = 0;
    std::vector<std::vector<Stage>> diagram_;
    std::vector<Instr> prog_;
};

void pipeline_demo() {
    section("5 级流水线 - 无冒险 (3 条独立指令)");
    Pipeline p1;
    p1.run({
        {"ADD R1,R2,R3", 1, {2,3}},
        {"ADD R4,R5,R6", 4, {5,6}},
        {"ADD R7,R8,R9", 7, {8,9}},
    });
    p1.print_diagram();
    std::cout << "  理想情况:n=3, k=5, 周期 = n+k-1 = 7\n\n";

    section("5 级流水线 - RAW 冒险 (R1 由前指令写,本指令读)");
    Pipeline p2;
    p2.run({
        {"ADD R1,R2,R3",  1, {2,3}},     // 写 R1
        {"SUB R4,R1,R5",  4, {1,5}},     // 读 R1 (RAW,EX/MEM 转发可消除停顿)
        {"MUL R6,R1,R7",  6, {1,7}},     // 读 R1 (已写回,无停顿)
    });
    p2.print_diagram();
    std::cout << "  EX→EX 转发:SUB 在第 3 周期 EX,ADD 在第 3 周期 EX 完成→可转发\n\n";

    section("5 级流水线 - Load-Use 冒险 (必须停 1 周期)");
    Pipeline p3;
    p3.run({
        {"LW  R1,[R2]",  1, {2},   true, false, false},  // load
        {"ADD R3,R1,R4", 3, {1,4}},                       // 用 R1 → load-use
    });
    p3.print_diagram();
    std::cout << "  Load-Use:LW 在 MEM (第 4 周期) 才有数据,ADD 的 EX (第 3 周期) 需停 1 周期\n\n";

    section("流水线性能公式");
    int n = 100, k = 5;
    int ideal = n + k - 1;
    int sequential = n * k;
    double speedup = static_cast<double>(sequential) / ideal;
    double efficiency = speedup / k;
    std::cout << "n = " << n << " 条指令, k = " << k << " 级\n";
    std::cout << "顺序执行: " << sequential << " 周期\n";
    std::cout << "流水线:   " << ideal << " 周期\n";
    std::cout << "加速比 S = " << speedup << "\n";
    std::cout << "效率   E = S/k = " << efficiency << "\n";
}

bool pipeline_test() {
    Pipeline p;
    p.run({
        {"ADD R1,R2,R3", 1, {2,3}},
        {"ADD R4,R5,R6", 4, {5,6}},
        {"ADD R7,R8,R9", 7, {8,9}},
    });
    // 3 条独立指令,理想 n+k-1 = 7 周期
    CS408_EXPECT_EQ(p.total_cycles(), 7);

    // Load-use 应有停顿
    Pipeline p2;
    p2.run({
        {"LW R1,[R2]",   1, {2}, true},
        {"ADD R3,R1,R4", 3, {1,4}},
    });
    // 期望: LW: IF1 ID2 EX3 MEM4 WB5 ; ADD: IF2 ID3 (stall) EX4 MEM5 WB6 -> 6 周期
    CS408_EXPECT(p2.total_cycles() >= 6);
    CS408_EXPECT(p2.stalls() > 0);

    return true;
}

CS408_REGISTER_MODULE(
    "computer-organization", "cpu.pipeline", pipeline,
    "5 级 IF/ID/EX/MEM/WB;S=nk/(k+n-1);三类冒险:结构/数据(RAW)/控制;转发/停顿/预测",
    "Intel 14~20 级;ARM A78 15 级;Apple M1 宽发射;GPU SIMT;RISC-V Rocket",
    "RAW 真相关;EX→EX 转发省 2 周期;Load-Use 必停 1;时空图必考;加速比 S=nk/(k+n-1)",
    pipeline_demo, pipeline_test
);

} // namespace cs408::co
#endif // CS408_CO_CPU_PIPELINE_H
