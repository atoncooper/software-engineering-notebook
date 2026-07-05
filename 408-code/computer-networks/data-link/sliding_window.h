/**
 * @file sliding_window.h
 * @topic 网络 - 数据链路层 - 滑动窗口协议 (停等/GBN/SR)
 *
 * @考点 408 大纲:计算机网络 > 数据链路层 > 流量控制与可靠传输
 *   - 停止-等待协议 (SW):
 *     窗口 = 1,发送方发 1 帧等 ACK
 *     信道利用率 U = t_f / (t_f + 2*t_p + t_a) ≈ t_f/(t_f+2RTT)
 *   - 后退 N 帧 (GBN):
 *     发送窗口 W ≤ 2^k - 1 (k=序号位数)
 *     累积 ACK,出错回退到出错帧重传所有后续
 *     接收窗口 = 1
 *   - 选择重传 (SR):
 *     发送窗口 + 接收窗口 ≤ 2^k
 *     通常 W_发 = W_收 = 2^(k-1)
 *     每帧独立 ACK,只重传出错帧
 *
 * @业务 工业应用
 *   - TCP 滑动窗口 (类似 SR 但用累积 ACK)
 *   - HDLC (GBN 思想)
 *   - Wi-Fi 802.11 ARQ (停等 + Block ACK)
 *   - 卫星链路 (大带宽时延乘积 → 大窗口)
 *   - 蓝牙 (停等变种)
 *
 * @陷阱 408 高频
 *   - 窗口大小 vs 序号位数关系必考:
 *     · SW:W=1,序号 1 位即可
 *     · GBN:W ≤ 2^k - 1
 *     · SR:W_发+W_收 ≤ 2^k,通常 W_发=W_收=2^(k-1)
 *   - 信道利用率 = t_f / (t_f + RTT + t_ack)
 *   - 链路最大吞吐 = 窗口大小 / RTT
 *   - 累积 ACK:GBN 用,确认 N 表示 N-1 都收到
 *   - SR 必须独立 ACK,不能用累积
 *   - 序号回绕:新老序号不能混淆 → 窗口限制
 */
#ifndef CS408_CN_DATA_LINK_SLIDING_WINDOW_H
#define CS408_CN_DATA_LINK_SLIDING_WINDOW_H

#include "common/types.h"
#include "common/utils.h"
#include <vector>
#include <iostream>
#include <deque>
#include <unordered_set>

namespace cs408::cn {

enum class Protocol { STOP_WAIT, GBN, SR };

struct LinkConfig {
    double t_frame;     // 发送一帧的时间
    double t_prop;      // 单程传播时延
    double t_ack;       // ACK 发送时间 (常忽略)
    int    seq_bits;    // 序号位数
};

// 信道利用率
inline double utilization(const LinkConfig& cfg, int window) {
    double t_total = cfg.t_frame + 2 * cfg.t_prop + cfg.t_ack;
    double t_useful = std::min(static_cast<double>(window) * cfg.t_frame, t_total);
    return t_useful / t_total;
}

// 最大窗口限制
inline int max_window(Protocol p, int seq_bits) {
    int n = 1 << seq_bits;
    switch (p) {
        case Protocol::STOP_WAIT: return 1;
        case Protocol::GBN:       return n - 1;
        case Protocol::SR:        return n / 2;
    }
    return 1;
}

// SR 协议模拟 (单个发送-接收对)
class SRSimulator {
public:
    SRSimulator(int seq_bits, int window)
        : seq_mod_(1 << seq_bits), window_(window), base_(0), next_(0) {}

    // 发送方:尝试发送窗口内的所有帧
    void send_all() {
        while (next_ < base_ + window_) {
            in_flight_.insert(next_ % seq_mod_);
            std::cout << "  发送帧 " << (next_ % seq_mod_) << "\n";
            ++next_;
        }
    }

    // 接收方:收到帧,返回是否接受 (在接收窗口内)
    bool receive(int seq) {
        int rcv_base = expected_;
        // 接收窗口 [rcv_base, rcv_base + window)
        int rel = (seq - rcv_base + seq_mod_) % seq_mod_;
        if (rel >= 0 && rel < window_) {
            received_.insert(seq);
            std::cout << "  接收帧 " << seq << " ✓ (在接收窗口)\n";
            // 滑动接收窗口
            while (received_.count(expected_ % seq_mod_)) {
                received_.erase(expected_ % seq_mod_);
                ++expected_;
            }
            return true;
        }
        std::cout << "  接收帧 " << seq << " ✗ (不在窗口,丢弃)\n";
        return false;
        // 实际应重传 ACK,这里简化
    }

    // 收到 ACK N (SR 独立 ACK)
    void ack(int n) {
        if (in_flight_.count(n)) {
            in_flight_.erase(n);
            std::cout << "  收到 ACK " << n << "\n";
            // 滑动发送窗口
            while (!in_flight_.count(base_ % seq_mod_) && base_ < next_) {
                ++base_;
                if (base_ >= next_) break;
            }
            // 简化:删除已确认的连续序号
            while (acked_.count(base_ % seq_mod_) || in_flight_.empty()) {
                if (base_ >= next_) break;
                ++base_;
            }
        }
    }

private:
    int seq_mod_;
    int window_;
    int base_;   // 发送窗口左沿 (绝对序号)
    int next_;   // 下一个待发 (绝对序号)
    int expected_ = 0;  // 接收窗口左沿
    std::unordered_set<int> in_flight_;
    std::unordered_set<int> received_;
    std::unordered_set<int> acked_;
};

void sliding_window_demo() {
    section("停止-等待协议 (SW)");
    LinkConfig sw{1.0, 5.0, 0.0, 1};
    std::cout << "窗口 = 1, t_frame = 1, RTT = 10\n";
    std::cout << "信道利用率 = " << utilization(sw, 1) << "\n";
    std::cout << "  → 仅 " << utilization(sw, 1) * 100 << "% 利用,链路浪费严重\n";

    section("后退 N 帧 (GBN) - 序号 3 位,窗口 = 7");
    LinkConfig gbn{1.0, 5.0, 0.0, 3};
    int w = max_window(Protocol::GBN, 3);
    std::cout << "GBN 最大窗口 = 2^k - 1 = " << w << "\n";
    std::cout << "信道利用率 (满窗口) = " << utilization(gbn, w) << "\n";
    std::cout << "  GBN 累积 ACK,出错回退 N 帧 (重传出错帧及之后所有)\n";

    section("选择重传 (SR) - 序号 3 位,窗口 = 4");
    int wsr = max_window(Protocol::SR, 3);
    std::cout << "SR 最大窗口 = 2^(k-1) = " << wsr << "\n";
    std::cout << "SR 独立 ACK,只重传出错帧\n";

    section("窗口限制推导 (关键!)");
    std::cout << "SW:  W = 1,序号 1 位即可 (但需区分新旧帧 → 至少 1 位)\n";
    std::cout << "GBN: W ≤ 2^k - 1  (留 1 个序号区分新旧窗口)\n";
    std::cout << "SR:  W_发 + W_收 ≤ 2^k  → W_发 = W_收 = 2^(k-1)\n";
    std::cout << "  反例:k=2,SR W=3:\n";
    std::cout << "  发 0,1,2;收 0,1,2 全 ACK 丢失;发 0 (回绕) → 接收方分不清是新 0 还是旧 0\n";

    section("信道利用率公式");
    std::cout << "U = W × t_f / (t_f + RTT + t_ack)\n";
    std::cout << "  例:t_f = 1ms, RTT = 20ms, W = 1\n";
    std::cout << "  U = 1 × 1 / (1 + 20 + 0) = " << 1.0 / 21 << "\n";
    std::cout << "  若 W = 20: U = 20 / 21 = " << 20.0 / 21 << "\n";
    std::cout << "  若 W = 30: U = min(30, 21)/21 = 1 (满载)\n";

    section("卫星链路 - 大带宽时延乘积 (BDP)");
    double bw = 1e6;       // 1 Mbps
    double rtt = 0.5;      // 500ms (卫星)
    double bdp = bw * rtt; // 500000 bits = 62.5 KB
    std::cout << "  带宽 1Mbps, RTT 500ms → BDP = " << bdp / 8 / 1024 << " KB\n";
    std::cout << "  → 窗口需 ≥ 63 KB 才能充分利用链路\n";
}

bool sliding_window_test() {
    // 窗口限制公式
    CS408_EXPECT_EQ(max_window(Protocol::STOP_WAIT, 1), 1);
    CS408_EXPECT_EQ(max_window(Protocol::GBN, 3), 7);     // 2^3 - 1 = 7
    CS408_EXPECT_EQ(max_window(Protocol::GBN, 4), 15);    // 2^4 - 1 = 15
    CS408_EXPECT_EQ(max_window(Protocol::SR, 3), 4);      // 2^(3-1) = 4
    CS408_EXPECT_EQ(max_window(Protocol::SR, 4), 8);      // 2^(4-1) = 8

    // 信道利用率
    LinkConfig cfg{1.0, 5.0, 0.0, 1};
    double u = utilization(cfg, 1);
    // t_total = 1 + 10 + 0 = 11, useful = 1, U = 1/11
    CS408_EXPECT(u > 0.09 && u < 0.10);

    // 大窗口应饱和到 1.0
    CS408_EXPECT(utilization(cfg, 100) <= 1.0);

    return true;
}

CS408_REGISTER_MODULE(
    "computer-networks", "data-link.sliding_window", sliding_window,
    "SW W=1;GBN W≤2^k-1 累积ACK回退;SR W_发+W_收≤2^k 独立ACK;U=W t_f/(t_f+RTT)",
    "TCP 滑动窗口;HDLC;Wi-Fi 802.11 Block ACK;卫星大 BDP 大窗口;蓝牙",
    "SW W=1;GBN 2^k-1;SR 2^(k-1);序号回绕限制窗口;利用率 W t_f/(t_f+RTT+t_ack);链路最大吞吐 W/RTT",
    sliding_window_demo, sliding_window_test
);

} // namespace cs408::cn
#endif // CS408_CN_DATA_LINK_SLIDING_WINDOW_H
