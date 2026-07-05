/**
 * @file csma_cd.h
 * @topic 网络 - 数据链路层 - CSMA/CD (以太网多路访问)
 *
 * @考点 408 大纲:计算机网络 > 数据链路层 > 介质访问控制
 *   - CSMA/CD = 载波监听多路访问/碰撞检测
 *   - 适用:总线型/Hub 半双工以太网 (无线不能用 CD)
 *   - 流程:
 *     1) 发前监听 (LBT):信道闲则发,忙则等待
 *     2) 边发边听:发送中检测碰撞
 *     3) 检测到碰撞 → 发 jam 信号 → 二进制指数退避
 *     4) 重传 (最多 16 次失败丢弃)
 *   - 最小帧长 = 2 × 传播时延 × 带宽 (确保检测到碰撞)
 *     = RTT × 带宽
 *   - 争用期 = 2τ (τ=单程传播时延)
 *   - 二进制指数退避:第 k 次冲突,从 [0, 2^k - 1] 选 r,等待 r × 2τ
 *     k ≤ 10 时 2^k,k > 10 时 2^10 = 1024
 *
 * @业务 工业应用
 *   - 经典以太网 (10Base5/10BaseT 半双工)
 *   - 现代全双工交换式以太网不用 CSMA/CD (无碰撞)
 *   - Wi-Fi 用 CSMA/CA (不能 CD)
 *   - EPON/GPON 用 TDMA (无碰撞)
 *   - 工业实时以太网 (EtherCAT) 避免冲突
 *
 * @陷阱 408 高频
 *   - 最小帧长 = RTT × 带宽 (10Mbps 以太网 = 64 字节)
 *   - 必须能检测到碰撞:帧发送完之前信号要能往返
 *   - 半双工才有 CSMA/CD,全双工无碰撞
 *   - 强化碰撞:检测后发 48 位 jam 信号
 *   - 退避算法:k=1 时随机 {0,1},k=2 时 {0,1,2,3},k=10 后封顶 1024
 *   - 无线网不能用 CD:发射功率 >> 接收功率,无法边发边听
 *   - 最长帧 1518 字节 (防止独占信道)
 */
#ifndef CS408_CN_DATA_LINK_CSMA_CD_H
#define CS408_CN_DATA_LINK_CSMA_CD_H

#include "common/types.h"
#include "common/utils.h"
#include <iostream>
#include <cmath>
#include <random>

namespace cs408::cn {

// 最小帧长 = RTT × 带宽 = 2 × τ × 带宽
// 单程时延 τ (秒), 带宽 (bps), 返回字节数
inline int min_frame_length(double prop_delay_s, double bandwidth_bps) {
    double rtt = 2 * prop_delay_s;
    double bits = rtt * bandwidth_bps;
    return static_cast<int>(std::ceil(bits / 8));
}

// 二进制指数退避:第 k 次冲突 (1-indexed),返回 [0, 2^k - 1] 范围内的随机数
// k 上限 10,k > 10 时取 2^10 = 1024
inline int backoff_range(int k) {
    int n = std::min(k, 10);
    return (1 << n);  // 范围 [0, 2^n - 1]
}

// 退避时间 = r × 2τ (争用期),r ∈ [0, 2^k - 1]
inline double backoff_time(int k, double propagation_delay, std::mt19937& rng) {
    int range = backoff_range(k);
    std::uniform_int_distribution<int> dist(0, range - 1);
    int r = dist(rng);
    return r * 2 * propagation_delay;
}

// 信道效率 (CSMA/CD):1/(1 + 2a)  其中 a = τ / t_f (传播/发送)
inline double csma_cd_efficiency(double prop_delay, double frame_time) {
    double a = prop_delay / frame_time;
    return 1.0 / (1.0 + 2 * a);
}

void csma_cd_demo() {
    section("CSMA/CD 流程");
    std::cout << "1) 发前监听 (LBT):闲则发,忙则等\n";
    std::cout << "2) 边发边听:检测碰撞\n";
    std::cout << "3) 检测到碰撞 → 发 jam 信号 (48 位)\n";
    std::cout << "4) 二进制指数退避,重传 (最多 16 次)\n";

    section("最小帧长 (关键!)");
    std::cout << "公式: L_min = RTT × 带宽 = 2τ × 带宽\n";
    std::cout << "  例:10BaseT,τ=25.6μs,带宽 10Mbps\n";
    std::cout << "  L_min = 2 × 25.6e-6 × 10e6 = 512 bits = 64 字节\n";
    int L = min_frame_length(25.6e-6, 10e6);
    std::cout << "  计算结果: " << L << " 字节\n";
    std::cout << "  → 以太网最小帧 64 字节就是这么来的\n";

    section("二进制指数退避");
    std::cout << "  第 k 次冲突:从 [0, 2^k - 1] 选 r,等 r × 2τ\n";
    std::cout << "  k 上限 10 (k>10 后范围封顶 1024)\n";
    std::cout << "  16 次失败 → 丢弃帧,上报错误\n\n";
    std::cout << "  k=1: 范围 [0,1]   → " << backoff_range(1) << " 个值\n";
    std::cout << "  k=2: 范围 [0,3]   → " << backoff_range(2) << " 个值\n";
    std::cout << "  k=3: 范围 [0,7]   → " << backoff_range(3) << " 个值\n";
    std::cout << "  k=10: 范围 [0,1023] → " << backoff_range(10) << " 个值\n";
    std::cout << "  k=12: 仍 [0,1023]   → " << backoff_range(12) << " 个值 (封顶)\n";

    section("CSMA/CD 信道效率");
    std::cout << "公式: η = 1/(1+2a), a = τ/t_f\n";
    // 10Base5: τ=25.6μs, 帧长 1500 字节 → t_f = 1500*8/10e6 = 1.2ms
    double tau = 25.6e-6, tf = 1500.0 * 8 / 10e6;
    std::cout << "  τ=25.6μs, 1500 字节帧, 10Mbps → t_f=" << tf * 1e3 << " ms\n";
    std::cout << "  a = " << tau / tf << ", η = " << csma_cd_efficiency(tau, tf) << "\n";
    std::cout << "  → 帧越长效率越高\n";

    section("无线网为什么不能用 CD?");
    std::cout << "  · 发射功率远大于接收功率,边发边听不出碰撞\n";
    std::cout << "  · 隐藏站/暴露站问题\n";
    std::cout << "  → Wi-Fi 用 CSMA/CA (碰撞避免)\n";

    section("现代以太网不用 CSMA/CD");
    std::cout << "  交换机全双工 → 每端口独占,无碰撞\n";
    std::cout << "  CSMA/CD 只在半双工 Hub 时代用\n";
}

bool csma_cd_test() {
    // 最小帧长:10BaseT, 25.6μs, 10Mbps → 64 字节
    CS408_EXPECT_EQ(min_frame_length(25.6e-6, 10e6), 64);

    // 二进制指数退避范围
    CS408_EXPECT_EQ(backoff_range(1), 2);     // {0,1}
    CS408_EXPECT_EQ(backoff_range(2), 4);     // {0,1,2,3}
    CS408_EXPECT_EQ(backoff_range(3), 8);     // {0,...,7}
    CS408_EXPECT_EQ(backoff_range(10), 1024);
    CS408_EXPECT_EQ(backoff_range(15), 1024); // 封顶

    // 信道效率
    // a=0.1 → η = 1/1.2 ≈ 0.833
    double eta = csma_cd_efficiency(1, 10);
    CS408_EXPECT(eta > 0.83 && eta < 0.84);

    return true;
}

CS408_REGISTER_MODULE(
    "computer-networks", "data-link.csma_cd", csma_cd,
    "CSMA/CD 边发边听;最小帧长=2τ×带宽=64B;二进制指数退避 2^k 封顶 1024;争用期=2τ",
    "经典以太网 10Base5/T 半双工;现代全双工无 CSMA/CD;Wi-Fi 用 CA;EPON TDMA;EtherCAT",
    "最小帧=RTT×带宽=64B;退避 2^k(k≤10) 16 次失败丢弃;无线不能用 CD 发射功率 >> 接收;强化碰撞 jam 48 位",
    csma_cd_demo, csma_cd_test
);

} // namespace cs408::cn
#endif // CS408_CN_DATA_LINK_CSMA_CD_H
