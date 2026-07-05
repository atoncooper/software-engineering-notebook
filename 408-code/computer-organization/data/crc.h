/**
 * @file crc.h
 * @topic 计组 - 数据校验 - CRC 循环冗余校验 (模 2 除法)
 *
 * @考点 408 大纲:计算机组成原理 > 数据的表示 > 校验码
 *   - CRC 基于 GF(2) 多项式除法 (模 2 加 = 异或)
 *   - 发送方:信息 M 后补 r 个 0,除以生成多项式 G,余数 R 即为 CRC 码
 *   - 接收方:(M' + R) ÷ G 余 0 → 无错
 *   - 生成多项式位数 = r+1,补 0 数 = r (最高次幂)
 *   - 模 2 除法:不借位,上商规则看首位 (1 商 1,0 商 0)
 *
 * @业务 工业应用
 *   - 以太网帧 FCS (32 位 CRC-32)
 *   - ZIP/RAR 文件完整性
 *   - 磁盘扇区校验
 *   - USB/CAN 总线
 *   - PNG/APNG 数据块
 *
 * @陷阱 408 高频
 *   - 模 2 除法不借位,被除数首位 1 商 1,首位 0 商 0
 *   - 余数位数 = 生成多项式次数 (比 G 少 1 位)
 *   - 补 0 个数 = G 的位数 - 1
 *   - 生成多项式最高位和最低位必为 1
 *   - CRC 检错能力:能检所有奇数个错、双错、长度 ≤ r 的突发错
 *   - 奇偶校验只能检 1 位错;海明能纠 1 位;CRC 能检多错但不纠错
 */
#ifndef CS408_CO_DATA_CRC_H
#define CS408_CO_DATA_CRC_H

#include "common/types.h"
#include "common/utils.h"
#include <vector>
#include <string>
#include <iostream>

namespace cs408::co {

using Bits = std::vector<int>;  // 每位 0/1

inline Bits str_to_bits(const std::string& s) {
    Bits b; b.reserve(s.size());
    for (char c : s) b.push_back(c - '0');
    return b;
}

inline std::string bits_to_str(const Bits& b) {
    std::string s; s.reserve(b.size());
    for (int x : b) s.push_back(static_cast<char>('0' + x));
    return s;
}

inline void print_bits(const Bits& b) {
    std::cout << bits_to_str(b);
}

// 模 2 除法:dividend ÷ divisor,返回余数 (位数 = divisor.size()-1)
// 注意:dividend 不够除时返回原样低 n 位
inline Bits mod2_div(Bits dividend, const Bits& divisor) {
    int n = static_cast<int>(divisor.size());
    for (int i = 0; i + n <= static_cast<int>(dividend.size()); ++i) {
        if (dividend[i] == 1) {
            for (int j = 0; j < n; ++j) {
                dividend[i + j] ^= divisor[j];  // 模 2 减 = 异或
            }
        }
    }
    return Bits(dividend.end() - (n - 1), dividend.end());
}

// 发送方:计算 CRC 码
// M = 信息位,G = 生成多项式,返回 余数 R (r 位)
inline Bits crc_encode(const Bits& M, const Bits& G) {
    int r = static_cast<int>(G.size()) - 1;
    Bits augmented = M;
    augmented.insert(augmented.end(), r, 0);  // 补 r 个 0
    return mod2_div(augmented, G);
}

// 接收方:校验 (M+R) ÷ G 余 0 则无错
inline bool crc_check(const Bits& M, const Bits& R, const Bits& G) {
    Bits received = M;
    received.insert(received.end(), R.begin(), R.end());
    Bits rem = mod2_div(received, G);
    for (int x : rem) if (x != 0) return false;
    return true;
}

// 引入单比特错误
inline Bits flip_bit(Bits data, int pos) {
    data[pos] ^= 1;
    return data;
}

void crc_demo() {
    section("CRC 模 2 除法 (信息 101001,生成多项式 1101 = x^3+x^2+1)");
    Bits M = str_to_bits("101001");
    Bits G = str_to_bits("1101");
    std::cout << "信息 M     = "; print_bits(M); std::cout << "\n";
    std::cout << "生成多项式 = "; print_bits(G); std::cout << "  (r = " << G.size() - 1 << ")\n";

    Bits R = crc_encode(M, G);
    std::cout << "补 0 后    = "; print_bits(M); std::cout << "000\n";
    std::cout << "CRC 余数 R = "; print_bits(R); std::cout << "  (手算: 101001000÷1101 → 余 001)\n";
    std::cout << "发送码字   = "; print_bits(M); print_bits(R); std::cout << "\n";

    section("接收方校验 (无错)");
    bool ok = crc_check(M, R, G);
    std::cout << "校验结果: " << (ok ? "余 0,无错 ✓" : "有错 ✗") << "\n";

    section("接收方校验 (引入 1 位错误 → 第 3 位翻转)");
    Bits bad_M = flip_bit(M, 2);
    std::cout << "收到的 M'  = "; print_bits(bad_M); std::cout << "\n";
    bool ok2 = crc_check(bad_M, R, G);
    std::cout << "校验结果: " << (ok2 ? "无错" : "余非 0,有错 ✓") << "\n";

    section("CRC-32 例子 (以太网 FCS,生成多项式 0x04C11DB7)");
    Bits G32;
    uint32_t g = 0x04C11DB7u;
    for (int i = 31; i >= 0; --i) G32.push_back((g >> i) & 1);
    G32.insert(G32.begin(), 1);  // x^32 项
    std::cout << "CRC-32 生成多项式位数: " << G32.size() << "  (r=32)\n";
    Bits msg = str_to_bits("1101011111");
    Bits R32 = crc_encode(msg, G32);
    std::cout << "信息 1101011111 的 CRC-32 = "; print_bits(R32); std::cout << "\n";
}

bool crc_test() {
    // M=101001, G=1101 (x^3+x^2+1) → 余数 001
    // 多项式: M=x^5+x^3+1, M·x^3=x^8+x^6+x^3, 除以 x^3+x^2+1 余 1
    Bits M = str_to_bits("101001");
    Bits G = str_to_bits("1101");
    Bits R = crc_encode(M, G);
    CS408_EXPECT_EQ(bits_to_str(R), std::string("001"));

    // 接收方无错应通过
    CS408_EXPECT(crc_check(M, R, G));

    // 引入错误应不通过
    Bits bad = flip_bit(M, 0);
    CS408_EXPECT(!crc_check(bad, R, G));

    // M=1101, G=1011 (x^3+x+1) → 余数 001 (手算验证)
    Bits M2 = str_to_bits("1101");
    Bits G2 = str_to_bits("1011");
    Bits R2 = crc_encode(M2, G2);
    CS408_EXPECT_EQ(bits_to_str(R2), std::string("001"));

    // M=1110, G=1011 (x^3+x+1) → 余数 100
    // 多项式: M=x^3+x^2+x, M·x^3=x^6+x^5+x^4, ÷(x^3+x+1) 余 x^2
    Bits M3 = str_to_bits("1110");
    Bits R3 = crc_encode(M3, G2);
    CS408_EXPECT_EQ(bits_to_str(R3), std::string("100"));

    return true;
}

CS408_REGISTER_MODULE(
    "computer-organization", "data.crc", crc,
    "GF(2) 模 2 除法 (异或);补 r 个 0;余数 r 位;首位 1 商 1 否则商 0",
    "以太网 FCS;ZIP/RAR;磁盘扇区;USB/CAN;PNG",
    "模 2 不借位;余数位数=G位数-1;补0数=G位数-1;G 高低位必为1;CRC 检错不纠错",
    crc_demo, crc_test
);

} // namespace cs408::co
#endif // CS408_CO_DATA_CRC_H
