/**
 * @file hamming.h
 * @topic 计组 - 数据校验 - 海明码 (纠 1 位错)
 *
 * @考点 408 大纲:计算机组成原理 > 数据的表示 > 校验码
 *   - 公式:2^r ≥ k + r + 1 (k=数据位, r=校验位)
 *   - 校验位 P_i 放在 2^(i-1) 位置 (1, 2, 4, 8, ...)
 *   - 分组规则:位置 j 的数据位,其二进制表示中包含 2^(i-1) 的位,归入第 i 组
 *   - 偶校验:每组异或为 0;奇校验:每组异或为 1
 *   - 纠错:错误位置 S = Σ (S_i × 2^(i-1)),S=0 无错,S≠0 第 S 位错
 *   - 能纠 1 位错,检 2 位错 (需额外整体奇偶校验)
 *
 * @业务 工业应用
 *   - ECC 内存 (DDR5 on-die ECC, 服务器 ECC DIMM)
 *   - NAND Flash (每页 BCH/LDPC 纠错)
 *   - 二维码 / QR 码 (Reed-Solomon)
 *   - 卫星通信 (强纠错码)
 *   - RAID 6 (双重校验)
 *
 * @陷阱 408 高频
 *   - 公式 2^r ≥ k+r+1 是最小 r (例:k=4 → r=3)
 *   - 校验位位置固定在 1,2,4,8 (从 1 开始数!)
 *   - 编号从 1 开始 (不是 0)
 *   - S (syndrome) = 0 无错,非 0 指出错误位置 (十进制即位置号)
 *   - 海明距离 = 码字间不同位数,d 能检 d-1 错,纠 (d-1)/2 错
 *   - 海明码最小距离 3 → 纠 1 检 2
 */
#ifndef CS408_CO_DATA_HAMMING_H
#define CS408_CO_DATA_HAMMING_H

#include "common/types.h"
#include "common/utils.h"
#include <vector>
#include <string>
#include <iostream>
#include <cmath>

namespace cs408::co {

// 计算需要的校验位数 r (满足 2^r >= k + r + 1)
inline int hamming_r(int k) {
    int r = 0;
    while ((1 << r) < k + r + 1) ++r;
    return r;
}

// 判断位置 j 是否是校验位 (2 的幂)
inline bool is_parity_pos(int j) {
    return j > 0 && (j & (j - 1)) == 0;
}

// 海明码编码 (偶校验)
// 输入:k 位数据 (高位在前),输出:k+r 位海明码 (位置 1..k+r)
inline std::vector<int> hamming_encode(const std::vector<int>& data) {
    int k = static_cast<int>(data.size());
    int r = hamming_r(k);
    int n = k + r;
    std::vector<int> code(n + 1, 0);  // 1-indexed

    // 填数据位 (跳过 2 的幂位置)
    int di = 0;
    for (int j = 1; j <= n; ++j) {
        if (!is_parity_pos(j)) {
            code[j] = data[di++];
        }
    }

    // 计算每个校验位 P_i (i=1..r),位置 2^(i-1)
    for (int i = 1; i <= r; ++i) {
        int pos = 1 << (i - 1);
        int xor_sum = 0;
        // 遍历所有位置,如果该位置的二进制包含 2^(i-1) (且不是校验位本身),异或
        for (int j = 1; j <= n; ++j) {
            if (j == pos) continue;
            if (j & pos) xor_sum ^= code[j];
        }
        code[pos] = xor_sum;  // 偶校验:使该组异或为 0
    }

    // 去掉位置 0
    return std::vector<int>(code.begin() + 1, code.end());
}

// 海明码译码 + 纠错
// 返回:syndrome (0=无错, 非0=错误位置)
inline int hamming_decode(std::vector<int> code) {
    int n = static_cast<int>(code.size());
    // 插入位置 0 使其变 1-indexed
    code.insert(code.begin(), 0);

    int r = 0;
    while ((1 << r) <= n) ++r;

    int syndrome = 0;
    for (int i = 1; i <= r; ++i) {
        int pos = 1 << (i - 1);
        int xor_sum = 0;
        for (int j = 1; j <= n; ++j) {
            if (j & pos) xor_sum ^= code[j];
        }
        if (xor_sum) syndrome |= pos;
    }

    // 纠错
    if (syndrome != 0 && syndrome <= n) {
        code[syndrome] ^= 1;
    }
    return syndrome;
}

// 提取数据位 (去掉校验位)
inline std::vector<int> hamming_extract_data(const std::vector<int>& code) {
    std::vector<int> data;
    int n = static_cast<int>(code.size());
    for (int j = 1; j <= n; ++j) {
        if (!is_parity_pos(j)) data.push_back(code[j - 1]);
    }
    return data;
}

inline void print_code(const std::vector<int>& code) {
    int n = static_cast<int>(code.size());
    for (int j = 1; j <= n; ++j) {
        std::cout << code[j - 1];
        if (is_parity_pos(j)) std::cout << "(P)";
        else std::cout << "(D)";
        std::cout << " ";
    }
    std::cout << "\n";
}

void hamming_demo() {
    section("海明码编码 (4 位数据 → 7 位海明码)");
    std::vector<int> data = {1, 0, 1, 0};  // 数据 1010
    int k = static_cast<int>(data.size());
    int r = hamming_r(k);
    std::cout << "数据 k = " << k << "  校验 r = " << r
              << "  (验证 2^" << r << " = " << (1 << r)
              << " ≥ " << k + r + 1 << ")\n";
    std::cout << "原始数据: ";
    for (int x : data) std::cout << x;
    std::cout << "\n";

    auto code = hamming_encode(data);
    std::cout << "海明码 (位置 1..7): ";
    print_code(code);
    std::cout << "  校验位在位置 1, 2, 4 (2 的幂)\n";

    section("无错译码");
    int s = hamming_decode(code);
    std::cout << "Syndrome = " << s << "  " << (s == 0 ? "无错 ✓" : "有错") << "\n";

    section("引入 1 位错误 (位置 5 翻转)");
    auto bad = code;
    bad[4] ^= 1;  // 位置 5 (0-indexed 4)
    std::cout << "收到的: ";
    print_code(bad);
    int s2 = hamming_decode(bad);
    std::cout << "Syndrome = " << s2 << "  → 位置 " << s2 << " 错,已纠正\n";

    section("海明码距离分析");
    std::cout << "海明码最小距离 = 3 → 能纠 1 位错,检 2 位错\n";
    std::cout << "公式:d_min = d,能检 d-1 错,纠 (d-1)/2 错\n";

    section("校验位数表");
    std::cout << "k(数据位)  r(校验位)  n(总长)\n";
    for (int kk = 1; kk <= 16; ++kk) {
        int rr = hamming_r(kk);
        std::cout << "  " << kk << "        " << rr << "         " << kk + rr << "\n";
    }
}

bool hamming_test() {
    // k=4 → r=3 (2^3=8 ≥ 4+3+1=8)
    CS408_EXPECT_EQ(hamming_r(4), 3);
    // k=7 → r=4 (2^4=16 ≥ 7+4+1=12)
    CS408_EXPECT_EQ(hamming_r(7), 4);
    // k=16 → r=5
    CS408_EXPECT_EQ(hamming_r(16), 5);

    // 编码 1010 → 应得到 7 位海明码
    std::vector<int> data = {1, 0, 1, 0};
    auto code = hamming_encode(data);
    CS408_EXPECT_EQ(static_cast<int>(code.size()), 7);

    // 无错译码 syndrome = 0
    CS408_EXPECT_EQ(hamming_decode(code), 0);

    // 翻转位置 j, syndrome 应等于 j
    for (int j = 1; j <= 7; ++j) {
        auto bad = code;
        bad[j - 1] ^= 1;
        CS408_EXPECT_EQ(hamming_decode(bad), j);
    }

    // 数据提取
    std::vector<int> extracted = hamming_extract_data(code);
    CS408_EXPECT_EQ(extracted.size(), data.size());
    for (size_t i = 0; i < data.size(); ++i) {
        CS408_EXPECT_EQ(extracted[i], data[i]);
    }
    return true;
}

CS408_REGISTER_MODULE(
    "computer-organization", "data.hamming", hamming,
    "2^r≥k+r+1;校验位在 2^(i-1);分组=位置二进制含 2^(i-1);S=ΣS_i×2^(i-1);纠 1 检 2",
    "ECC 内存;NAND Flash BCH/LDPC;QR Reed-Solomon;卫星通信;RAID 6",
    "公式 2^r≥k+r+1 取最小 r;位置从 1 开始数;S=0 无错非 0 即错位;最小距离 3 纠 1 检 2",
    hamming_demo, hamming_test
);

} // namespace cs408::co
#endif // CS408_CO_DATA_HAMMING_H
