/**
 * @file nyquist_shannon.h
 * @topic 网络 - 物理层 - 奈奎斯特定理与香农定理
 *
 * @考点 408 大纲:计算机网络 > 物理层 > 信道极限
 *   - 奈奎斯特定理 (无噪声信道):
 *     最大数据传输率 = 2W log2(V)  (bps)
 *     W = 带宽 (Hz), V = 离散信号状态数
 *   - 香农定理 (有噪声信道):
 *     信道容量 C = W log2(1 + S/N)  (bps)
 *     S/N 信噪比,常用 dB: dB = 10 log10(S/N)
 *   - 编码:NRZ/Manchester/4B/5B
 *   - 调制:AM/FM/PM/QAM
 *
 * @业务 工业应用
 *   - 5G NR 子载波带宽与调制阶数 (QPSK/16QAM/64QAM/256QAM)
 *   - Wi-Fi 6/7 (OFDM + 1024-QAM)
 *   - ADSL 频分复用
 *   - 光通信 (DWDM)
 *   - DoCSIS 同轴电缆
 *
 * @陷阱 408 高频
 *   - 奈奎斯特:无噪声,理论最大;香农:有噪声,实际极限
 *   - 实际最大速率 = min(奈奎斯特, 香农)
 *   - 信噪比 dB 转 比值:S/N = 10^(dB/10)
 *   - 曼彻斯特编码:波特率 = 2 × 比特率 (每比特 2 个信号)
 *   - 信道复用:FDM (频分)/ TDM (时分)/ WDM (波分)/ CDMA (码分)
 *   - 波特率 ≠ 比特率:比特率 = 波特率 × log2(V)
 */
#ifndef CS408_CN_PHYSICAL_NYQUIST_SHANNON_H
#define CS408_CN_PHYSICAL_NYQUIST_SHANNON_H

#include "common/types.h"
#include "common/utils.h"
#include <iostream>
#include <cmath>

namespace cs408::cn {

// 奈奎斯特定理:无噪声信道最大数据率
// R = 2W log2(V)
inline double nyquist(double bandwidth_hz, int levels) {
    return 2.0 * bandwidth_hz * std::log2(levels);
}

// 香农定理:有噪声信道容量
// C = W log2(1 + S/N)
inline double shannon(double bandwidth_hz, double snr_linear) {
    return bandwidth_hz * std::log2(1.0 + snr_linear);
}

// 信噪比 dB 转线性
inline double db_to_linear(double db) {
    return std::pow(10.0, db / 10.0);
}

// 曼彻斯特/差分曼彻斯特编码:每比特 2 个电平,波特率 = 2 × 比特率
inline double manchester_baud(double bit_rate) {
    return 2.0 * bit_rate;
}

// 比特率 = 波特率 × log2(V)
inline double bits_from_baud(double baud, int levels) {
    return baud * std::log2(levels);
}

void nyquist_shannon_demo() {
    section("奈奎斯特定理 (无噪声信道)");
    std::cout << "公式: R_max = 2W × log2(V)\n";
    std::cout << "  例:W=3kHz, V=4 (4 进制) → R = "
              << nyquist(3000, 4) << " bps\n";
    std::cout << "  例:W=4kHz, V=16 → R = "
              << nyquist(4000, 16) << " bps\n";

    section("香农定理 (有噪声信道)");
    std::cout << "公式: C = W × log2(1 + S/N)\n";
    double snr_db = 30;  // 30 dB
    double snr_lin = db_to_linear(snr_db);
    std::cout << "  信噪比 30 dB → S/N = " << snr_lin << "\n";
    std::cout << "  W=3kHz, S/N=30dB → C = "
              << shannon(3000, snr_lin) << " bps ≈ 30 kbps\n";
    std::cout << "  W=3kHz, S/N=20dB → C = "
              << shannon(3000, db_to_linear(20)) << " bps\n";

    section("实际最大速率 = min(奈奎斯特, 香农)");
    // 电话线:W=3000Hz, V=8, S/N=30dB
    double W = 3000;
    int V = 8;
    double snr = db_to_linear(30);
    double rn = nyquist(W, V);
    double rs = shannon(W, snr);
    std::cout << "  电话线 W=" << W << "Hz, V=" << V << ", S/N=30dB\n";
    std::cout << "  奈奎斯特: " << rn << " bps\n";
    std::cout << "  香农:     " << rs << " bps\n";
    std::cout << "  实际最大 = min = " << std::min(rn, rs) << " bps\n";

    section("波特率 vs 比特率");
    std::cout << "比特率 = 波特率 × log2(V)\n";
    std::cout << "  波特 1200, V=2 (NRZ):  " << bits_from_baud(1200, 2) << " bps\n";
    std::cout << "  波特 1200, V=16 (QAM-16): " << bits_from_baud(1200, 16) << " bps\n";

    section("曼彻斯特编码");
    std::cout << "  每比特用 2 个电平,波特率 = 2 × 比特率\n";
    std::cout << "  10Mbps 以太网波特率 = " << manchester_baud(10e6) / 1e6 << " Mbaud\n";

    section("信道复用对比");
    std::cout << "FDM 频分:模拟信号,带宽切分 (FM 广播,有线电视)\n";
    std::cout << "TDM 时分:数字信号,时间片轮转 (E1/T1,SDH)\n";
    std::cout << "WDM 波分:光纤,不同波长 (DWDM/CWDM)\n";
    std::cout << "CDMA 码分:不同正交码 (3G)\n";
}

bool nyquist_shannon_test() {
    // 奈奎斯特:2W log2(V)
    // W=4kHz, V=16 → 2*4000*4 = 32000
    CS408_EXPECT_EQ(nyquist(4000, 16), 32000.0);
    // W=3kHz, V=4 → 2*3000*2 = 12000
    CS408_EXPECT_EQ(nyquist(3000, 4), 12000.0);

    // 信噪比 dB 转线性
    // 30 dB → 1000
    CS408_EXPECT_EQ(db_to_linear(30), 1000.0);
    // 10 dB → 10
    CS408_EXPECT_EQ(db_to_linear(10), 10.0);

    // 香农:W=3kHz, S/N=1000 (30dB) → 3000*log2(1001) ≈ 29901 bps
    double c = shannon(3000, 1000);
    CS408_EXPECT(c > 29000 && c < 30000);

    // 波特转比特
    CS408_EXPECT_EQ(bits_from_baud(1000, 2), 1000.0);
    CS408_EXPECT_EQ(bits_from_baud(1000, 4), 2000.0);

    return true;
}

CS408_REGISTER_MODULE(
    "computer-networks", "physical.nyquist_shannon", nyquist_shannon,
    "奈奎斯特 R=2W log2(V) 无噪声;香农 C=W log2(1+S/N) 有噪声;实际=min(两式);dB=10 log10 S/N",
    "5G NR QAM;Wi-Fi 6/7 OFDM 1024-QAM;ADSL FDM;DWDM 光通信;DoCSIS",
    "奈氏无噪/香氏有噪;dB→线性 10^(dB/10);波特×log2(V)=比特;曼彻斯特波特=2×比特;复用 FDM/TDM/WDM/CDMA",
    nyquist_shannon_demo, nyquist_shannon_test
);

} // namespace cs408::cn
#endif // CS408_CN_PHYSICAL_NYQUIST_SHANNON_H
