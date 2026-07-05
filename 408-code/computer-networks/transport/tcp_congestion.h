/**
 * @file tcp_congestion.h
 * @topic 网络 - 传输层 - TCP 拥塞控制 (慢启动/拥塞避免/快重传/快恢复)
 *
 * @考点 408 大纲:计算机网络 > 传输层 > TCP 拥塞控制
 *   - 4 个算法:慢启动 (SS) + 拥塞避免 (CA) + 快重传 (FR) + 快恢复 (FRR)
 *   - 拥塞窗口 cwnd,接收窗口 rwnd
 *     发送窗口 = min(cwnd, rwnd)
 *   - 慢启动:cwnd 从 1 指数增长 (每 RTT 翻倍),到 ssthresh 切换
 *   - 拥塞避免:cwnd 线性增长 (每 RTT +1)
 *   - 快重传:连续 3 个重复 ACK → 立即重传丢失段
 *   - 快恢复:ssthresh = cwnd/2, cwnd = ssthresh (不回到 1)
 *   - 超时事件:ssthresh = cwnd/2, cwnd = 1 (回到慢启动)
 *   - ssthresh 初值通常 16 或更高
 *
 * @业务 工业应用
 *   - Linux TCP Reno/Cubic/BBR
 *   - BBR (Google):基于带宽和 RTT 建模,不依赖丢包
 *   - 数据中心 TCP (DCTCP) 用 ECN 反馈
 *   - QUIC 拥塞控制 (BBR/Cubic)
 *   - 5G/LTE 移动网络 (高 RTT 变化)
 *
 * @陷阱 408 高频
 *   - 慢启动指数增长 (1,2,4,8...),不是"慢"
 *   - ssthresh 是分界,到它就转线性
 *   - 超时:cwnd=1 重新慢启动;ssthresh=cwnd/2
 *   - 3 次重复 ACK:快重传 + 快恢复,cwnd = ssthresh (不回 1)
 *   - RTT 估算:SRTT = (1-α)SRTT + α×R;RTO = SRTT + 4×RTTVAR
 *   - 拥塞窗口 vs 接收窗口:发送窗口 = min(cwnd, rwnd)
 *   - 加性增,乘性减 (AIMD):线性增 cwnd,超时减半
 */
#ifndef CS408_CN_TRANSPORT_TCP_CONGESTION_H
#define CS408_CN_TRANSPORT_TCP_CONGESTION_H

#include "common/types.h"
#include "common/utils.h"
#include <iostream>
#include <vector>
#include <string>

namespace cs408::cn {

enum class CCState { SLOW_START, CONGESTION_AVOIDANCE, FAST_RECOVERY };

inline const char* cc_state_str(CCState s) {
    switch (s) {
        case CCState::SLOW_START:          return "慢启动";
        case CCState::CONGESTION_AVOIDANCE:return "拥塞避免";
        case CCState::FAST_RECOVERY:       return "快恢复";
    }
    return "?";
}

// TCP Reno 拥塞控制模拟
class TCPCongestionControl {
public:
    TCPCongestionControl(int initial_ssthresh = 16)
        : cwnd_(1), ssthresh_(initial_ssthresh), state_(CCState::SLOW_START),
          dup_acks_(0), history_() {
        record(0, "init");
    }

    // 每个 RTT 接收一批 ACK
    void on_new_ack() {
        dup_acks_ = 0;
        switch (state_) {
            case CCState::SLOW_START:
                cwnd_ *= 2;  // 指数增长
                if (cwnd_ >= ssthresh_) {
                    state_ = CCState::CONGESTION_AVOIDANCE;
                    record(cwnd_, "SS→CA (cwnd≥ssthresh)");
                } else {
                    record(cwnd_, "SS 翻倍");
                }
                break;
            case CCState::CONGESTION_AVOIDANCE:
                cwnd_ += 1;  // 线性增长
                record(cwnd_, "CA +1");
                break;
            case CCState::FAST_RECOVERY:
                cwnd_ = ssthresh_;  // 收到新 ACK,退出快恢复
                state_ = CCState::CONGESTION_AVOIDANCE;
                record(cwnd_, "FR→CA");
                break;
        }
    }

    void on_dup_ack() {
        ++dup_acks_;
        if (state_ == CCState::FAST_RECOVERY) {
            cwnd_ += 1;  // 快恢复期,每个 dup ACK 让 cwnd+1 (窗口膨胀)
            record(cwnd_, "FR 膨胀 +1");
            return;
        }
        if (dup_acks_ == 3) {
            // 快重传 + 快恢复
            ssthresh_ = cwnd_ / 2;
            cwnd_ = ssthresh_;  // Reno:不回到 1
            state_ = CCState::FAST_RECOVERY;
            record(cwnd_, "3 dup ACK → 快重传+快恢复");
        } else {
            record(cwnd_, "dup ACK #" + std::to_string(dup_acks_));
        }
    }

    void on_timeout() {
        ssthresh_ = cwnd_ / 2;
        cwnd_ = 1;  // 回到慢启动
        state_ = CCState::SLOW_START;
        dup_acks_ = 0;
        record(cwnd_, "超时 → SS, cwnd=1");
    }

    int cwnd() const { return cwnd_; }
    int ssthresh() const { return ssthresh_; }
    CCState state() const { return state_; }

    void print_history() const {
        std::cout << "RTT\tcwnd\tssthresh\t状态\t事件\n";
        for (size_t i = 0; i < history_.size(); ++i) {
            std::cout << i << "\t" << history_[i].cwnd << "\t"
                      << history_[i].ssthresh << "\t\t"
                      << cc_state_str(history_[i].state) << "\t"
                      << history_[i].event << "\n";
        }
    }

private:
    void record(int cwnd, const std::string& event) {
        history_.push_back({cwnd_, ssthresh_, state_, event});
    }
    int cwnd_;
    int ssthresh_;
    CCState state_;
    int dup_acks_;
    struct Step { int cwnd; int ssthresh; CCState state; std::string event; };
    std::vector<Step> history_;
};

void tcp_congestion_demo() {
    section("TCP 拥塞控制 4 算法");
    std::cout << "1) 慢启动 (SS):cwnd 从 1 指数增长 (1→2→4→8...)\n";
    std::cout << "2) 拥塞避免 (CA):cwnd 线性增长 (+1/RTT)\n";
    std::cout << "3) 快重传 (FR):3 次重复 ACK → 立即重传\n";
    std::cout << "4) 快恢复 (FRR):ssthresh=cwnd/2, cwnd=ssthresh (不回 1)\n\n";

    std::cout << "AIMD:加性增 (线性 +1), 乘性减 (超时减半)\n";
    std::cout << "发送窗口 = min(cwnd, rwnd)\n\n";

    section("场景:慢启动 → 拥塞避免 → 3 dup ACK 快恢复 → 超时");
    TCPCongestionControl cc(8);
    // 慢启动阶段 (1→2→4→8)
    cc.on_new_ack();  // 1→2
    cc.on_new_ack();  // 2→4
    cc.on_new_ack();  // 4→8 (到达 ssthresh,切到 CA)
    // 拥塞避免阶段 (8→9→10→...)
    cc.on_new_ack();  // 8→9
    cc.on_new_ack();  // 9→10
    // 3 次重复 ACK → 快重传 + 快恢复
    cc.on_dup_ack();  // dup1
    cc.on_dup_ack();  // dup2
    cc.on_dup_ack();  // dup3 → 快恢复
    // 超时
    cc.on_timeout();
    cc.print_history();

    section("拥塞窗口演变图 (经典锯齿)");
    std::cout << "    cwnd\n";
    std::cout << "     ^\n";
    std::cout << "     |     /\\      /\\      /\\\n";
    std::cout << "     |    /  \\    /  \\    /  \\   (快恢复后线性增)\n";
    std::cout << "     |   /    \\  /    \\  /    \\ \n";
    std::cout << "     |  /      \\/      \\/      \\\n";
    std::cout << "     | / 指数增长 (SS)\n";
    std::cout << "     |/__________________________> RTT\n";
    std::cout << "  超时: cwnd=1, ssthresh=cwnd/2\n";
    std::cout << "  3 dup ACK: cwnd=ssthresh (不回 1)\n";

    section("Reno vs Tahoe 对比");
    std::cout << "Tahoe (1988): 3 dup ACK → cwnd=1 (回慢启动)\n";
    std::cout << "Reno  (1990): 3 dup ACK → cwnd=ssthresh (快恢复)\n";
    std::cout << "NewReno/Cubic/BBR 进一步改进\n";

    section("BBR (Google 2016) 新思路");
    std::cout << "  不依赖丢包判断拥塞\n";
    std::cout << "  测量瓶颈带宽 BtlBw 和 RTT\n";
    std::cout << "  cwnd = BtlBw × minRTT (BDP)\n";
    std::cout << "  适合高带宽长 RTT 链路 (5G/卫星)\n";

    section("RTT 估算 (Jacobson 算法)");
    std::cout << "  SRTT = (1-α) × SRTT + α × R           (α=1/8)\n";
    std::cout << "  RTTVAR = (1-β) × RTTVAR + β × |SRTT-R| (β=1/4)\n";
    std::cout << "  RTO = SRTT + 4 × RTTVAR\n";
}

bool tcp_congestion_test() {
    TCPCongestionControl cc(8);
    // 初始 cwnd=1
    CS408_EXPECT_EQ(cc.cwnd(), 1);
    CS408_EXPECT(cc.state() == CCState::SLOW_START);

    // 慢启动:1→2→4→8 (到 ssthresh)
    cc.on_new_ack(); CS408_EXPECT_EQ(cc.cwnd(), 2);
    cc.on_new_ack(); CS408_EXPECT_EQ(cc.cwnd(), 4);
    cc.on_new_ack(); CS408_EXPECT_EQ(cc.cwnd(), 8);
    CS408_EXPECT(cc.state() == CCState::CONGESTION_AVOIDANCE);

    // 拥塞避免:线性 +1
    cc.on_new_ack(); CS408_EXPECT_EQ(cc.cwnd(), 9);
    cc.on_new_ack(); CS408_EXPECT_EQ(cc.cwnd(), 10);

    // 3 dup ACK → 快恢复:ssthresh = 10/2 = 5, cwnd = 5
    cc.on_dup_ack();
    cc.on_dup_ack();
    cc.on_dup_ack();
    CS408_EXPECT_EQ(cc.ssthresh(), 5);
    CS408_EXPECT_EQ(cc.cwnd(), 5);
    CS408_EXPECT(cc.state() == CCState::FAST_RECOVERY);

    // 超时 → cwnd=1, ssthresh=5/2=2
    cc.on_timeout();
    CS408_EXPECT_EQ(cc.cwnd(), 1);
    CS408_EXPECT_EQ(cc.ssthresh(), 2);
    CS408_EXPECT(cc.state() == CCState::SLOW_START);

    return true;
}

CS408_REGISTER_MODULE(
    "computer-networks", "transport.tcp_congestion", tcp_congestion,
    "慢启动指数/拥塞避免线性/3dupACK快重传/快恢复 ssthresh=cwnd/2;超时 cwnd=1;AIMD",
    "Linux Reno/Cubic/BBR;DCTCP ECN;QUIC BBR;5G 高 RTT;数据中心低时延",
    "慢启动不是慢是指数;到 ssthresh 转线性;3 dup ACK 不回 1,超时回 1;AIMD 加性增乘性减",
    tcp_congestion_demo, tcp_congestion_test
);

} // namespace cs408::cn
#endif // CS408_CN_TRANSPORT_TCP_CONGESTION_H
