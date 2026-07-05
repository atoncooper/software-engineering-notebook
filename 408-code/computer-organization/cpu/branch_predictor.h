/**
 * @file branch_predictor.h
 * @topic 计组 - CPU - 分支预测 (2 位饱和计数器 + BHT/BTB)
 *
 * @考点 408 大纲:计算机组成原理 > CPU > 指令流水线 > 控制冒险
 *   - 静态预测:固定预测 (总是跳转 / 总是不跳 / 编译器提示)
 *   - 动态预测:基于历史
 *     · 1 位预测:上次跳就预测跳 (简单,易受单次异常干扰,如循环退出)
 *     · 2 位饱和计数器:4 状态,需 2 次错误才改变预测 (循环好)
 *   - BHT (Branch History Table):存历史跳转结果
 *   - BTB (Branch Target Buffer):存目标地址
 *   - 2 级自适应 (gshare):全局历史 + 模式历史
 *
 * @业务 工业应用
 *   - Intel Core 系列分支预测器 (TAGE + Perceptron)
 *   - AMD Zen 分支预测 (感知器)
 *   - Apple Firestorm (深度分支预测)
 *   - ARM Neoverse
 *   - GPU 分支 divergence 处理
 *
 * @陷阱 408 高频
 *   - 2 位饱和计数器:状态 ST/WT/WN/SN,错 2 次才改预测
 *   - 循环 N 次:1 位错 2 次 (退出+进入),2 位错 1 次 (仅退出)
 *   - 分支预测错误代价 = 流水线深度个周期 (冲刷 IF/ID)
 *   - 准确率影响 CPI:CPI = 理想 + 错误率 × 惩罚
 *   - BTB 命中时立即知道目标地址,避免 IF 阶段空泡
 */
#ifndef CS408_CO_CPU_BRANCH_PREDICTOR_H
#define CS408_CO_CPU_BRANCH_PREDICTOR_H

#include "common/types.h"
#include "common/utils.h"
#include <vector>
#include <unordered_map>
#include <iostream>
#include <cstdint>

namespace cs408::co {

// 2 位饱和计数器状态
enum class PredState {
    STRONG_NOT = 0,  // 强不跳
    WEAK_NOT   = 1,  // 弱不跳
    WEAK_TAKEN = 2,  // 弱跳
    STRONG_TAKEN = 3 // 强跳
};

inline const char* pred_state_str(PredState s) {
    switch (s) {
        case PredState::STRONG_NOT:   return "SN";
        case PredState::WEAK_NOT:     return "WN";
        case PredState::WEAK_TAKEN:   return "WT";
        case PredState::STRONG_TAKEN: return "ST";
    }
    return "?";
}

// 1 位预测器
class OneBitPredictor {
public:
    bool predict(uint32_t pc) {
        auto it = table_.find(pc);
        if (it == table_.end()) return false;  // 默认不跳
        return it->second;
    }
    void update(uint32_t pc, bool taken) {
        table_[pc] = taken;
    }
private:
    std::unordered_map<uint32_t, bool> table_;
};

// 2 位饱和计数器
class TwoBitPredictor {
public:
    bool predict(uint32_t pc) {
        auto it = table_.find(pc);
        if (it == table_.end()) return false;  // 默认 SN
        return it->second == PredState::WEAK_TAKEN || it->second == PredState::STRONG_TAKEN;
    }
    void update(uint32_t pc, bool taken) {
        auto& s = table_[pc];
        if (taken) {
            switch (s) {
                case PredState::STRONG_NOT:   s = PredState::WEAK_NOT; break;
                case PredState::WEAK_NOT:     s = PredState::WEAK_TAKEN; break;
                case PredState::WEAK_TAKEN:   s = PredState::STRONG_TAKEN; break;
                case PredState::STRONG_TAKEN: break;
            }
        } else {
            switch (s) {
                case PredState::STRONG_TAKEN: s = PredState::WEAK_TAKEN; break;
                case PredState::WEAK_TAKEN:   s = PredState::WEAK_NOT; break;
                case PredState::WEAK_NOT:     s = PredState::STRONG_NOT; break;
                case PredState::STRONG_NOT:   break;
            }
        }
    }
    PredState state(uint32_t pc) {
        auto it = table_.find(pc);
        return it == table_.end() ? PredState::STRONG_NOT : it->second;
    }
private:
    std::unordered_map<uint32_t, PredState> table_;
};

// 测试预测器:对给定分支序列预测,返回准确率
struct PredictResult {
    int correct;
    int total;
    double accuracy;
};

inline PredictResult test_predictor(const std::vector<bool>& sequence, bool use_two_bit) {
    OneBitPredictor p1;
    TwoBitPredictor p2;
    int correct = 0;
    uint32_t pc = 0x1000;
    for (bool actual : sequence) {
        bool pred = use_two_bit ? p2.predict(pc) : p1.predict(pc);
        if (pred == actual) ++correct;
        if (use_two_bit) p2.update(pc, actual); else p1.update(pc, actual);
    }
    return {correct, static_cast<int>(sequence.size()),
            static_cast<double>(correct) / sequence.size()};
}

void branch_predictor_demo() {
    section("1 位 vs 2 位预测器 (循环 9 次跳,1 次不跳,共 3 轮)");
    // 模拟 3 轮循环,每轮 9 跳 + 1 退出
    std::vector<bool> seq;
    for (int r = 0; r < 3; ++r)
        for (int i = 0; i < 10; ++i)
            seq.push_back(i < 9);  // 9 跳 + 1 不跳

    auto r1 = test_predictor(seq, false);
    auto r2 = test_predictor(seq, true);
    std::cout << "1 位预测器: 正确 " << r1.correct << "/" << r1.total
              << " = " << r1.accuracy << "\n";
    std::cout << "  → 每轮错 2 次 (退出 + 进入) = " << (3 * 2) << " 次\n";
    std::cout << "2 位预测器: 正确 " << r2.correct << "/" << r2.total
              << " = " << r2.accuracy << "\n";
    std::cout << "  → 首轮错 3 次 (启动 2 + 退出 1), 后续每轮仅退出错 1 = "
              << (3 + 2 * 1) << " 次\n";

    section("2 位饱和计数器状态转移演示");
    TwoBitPredictor p;
    uint32_t pc = 0x2000;
    std::cout << "初始: " << pred_state_str(p.state(pc)) << " (默认 SN)\n";
    for (int i = 0; i < 4; ++i) {
        p.update(pc, true);
        std::cout << "跳一次 → " << pred_state_str(p.state(pc)) << "  预测: "
                  << (p.predict(pc) ? "跳" : "不跳") << "\n";
    }
    std::cout << "  → 已饱和到 ST,再跳不变\n";
    for (int i = 0; i < 4; ++i) {
        p.update(pc, false);
        std::cout << "不跳一次 → " << pred_state_str(p.state(pc))
                  << "  预测: " << (p.predict(pc) ? "跳" : "不跳") << "\n";
    }
    std::cout << "  → 错 2 次才改预测方向\n";

    section("分支预测错误对 CPI 的影响");
    double branch_rate = 0.2;       // 20% 是分支
    double mispredict_rate = 0.05;  // 5% 预测错误
    int penalty = 3;                // 错误惩罚 3 周期
    double base_cpi = 1.0;
    double cpi = base_cpi + branch_rate * mispredict_rate * penalty;
    std::cout << "分支比例 " << branch_rate << ", 预测错误率 " << mispredict_rate
              << ", 惩罚 " << penalty << " 周期\n";
    std::cout << "CPI = " << base_cpi << " + " << branch_rate << " × "
              << mispredict_rate << " × " << penalty << " = " << cpi << "\n";

    section("现代分支预测器层级");
    std::cout << "BHT (Branch History Table): 存历史跳转方向\n";
    std::cout << "BTB (Branch Target Buffer): 存目标地址 (避免 IF 等待)\n";
    std::cout << "2 级自适应 (gshare): 全局历史 XOR PC 索引\n";
    std::cout << "TAGE: 多个历史长度表,选最长匹配\n";
    std::cout << "Perceptron: 感知机学习复杂相关性\n";
}

bool branch_predictor_test() {
    // 1 位预测器:循环 (9 跳 + 1 不跳) × 1 轮 → 错 2 次 (进入 + 退出)
    std::vector<bool> loop;
    for (int i = 0; i < 10; ++i) loop.push_back(i < 9);
    auto r1 = test_predictor(loop, false);
    CS408_EXPECT_EQ(r1.correct, 8);  // 10 - 2 = 8

    // 2 位预测器:同样循环 → 错 3 次 (2 次启动 SN→WN→WT + 1 次退出)
    auto r2 = test_predictor(loop, true);
    CS408_EXPECT_EQ(r2.correct, 7);  // 10 - 3 = 7

    // 多轮循环:首轮 3 错 (SN→WN→WT 启动 + 退出),后续每轮仅 1 错 (退出)
    std::vector<bool> multi;
    for (int r = 0; r < 3; ++r)
        for (int i = 0; i < 10; ++i) multi.push_back(i < 9);
    auto r3 = test_predictor(multi, true);
    CS408_EXPECT_EQ(r3.correct, 25);  // 30 - 5 (3+1+1) = 25

    // 2 位饱和:连续 4 次跳应到 ST
    TwoBitPredictor p;
    uint32_t pc = 0x3000;
    for (int i = 0; i < 4; ++i) p.update(pc, true);
    CS408_EXPECT(p.state(pc) == PredState::STRONG_TAKEN);
    CS408_EXPECT(p.predict(pc) == true);

    // 单次不跳不足以改变预测
    p.update(pc, false);
    CS408_EXPECT(p.state(pc) == PredState::WEAK_TAKEN);
    CS408_EXPECT(p.predict(pc) == true);  // 仍预测跳

    return true;
}

CS408_REGISTER_MODULE(
    "computer-organization", "cpu.branch_predictor", branch_predictor,
    "1 位预测错 2 次/循环;2 位饱和 4 状态错 2 次才改;BHT 历史/BTB 目标;CPI 受错误率影响",
    "Intel TAGE+Perceptron;AMD Zen 感知器;Apple Firestorm;ARM Neoverse;GPU divergence",
    "2 位状态 SN/WN/WT/ST;循环 1 位错 2 次 2 位错 1 次;错误代价=流水线深度;BTB 避 IF 空泡",
    branch_predictor_demo, branch_predictor_test
);

} // namespace cs408::co
#endif // CS408_CO_CPU_BRANCH_PREDICTOR_H
