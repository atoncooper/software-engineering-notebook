/**
 * @file ieee754.h
 * @topic 计组 - 数据的表示 - IEEE 754 浮点数
 *
 * @考点 408 大纲:计算机组成原理 > 数据的表示 > 浮点数表示
 *   - 单精度 float: 1 符号 + 8 阶码 + 23 尾数 = 32 位
 *   - 双精度 double: 1 + 11 + 52 = 64 位
 *   - 阶码用移码表示,bias=127 (float) / 1023 (double)
 *   - 规格化:1.M (隐含的 1),真值 = (-1)^S × 1.M × 2^(E-bias)
 *   - 非规格化:E=0, M≠0,真值 = (-1)^S × 0.M × 2^(1-bias) (填充间隙)
 *   - 特殊值:E=255 (全1), M=0 → ±Inf; M≠0 → NaN
 *   - 舍入:就近偶数 (round to nearest even)
 *
 * @业务 工业应用
 *   - GPU 深度学习 (FP16/BF16/FP8)
 *   - 数值计算库 (BLAS/LAPACK)
 *   - 游戏物理引擎 (FP32)
 *   - JSON 解析 (字符串→浮点)
 *   - 数据库 decimal vs float 选择
 *
 * @陷阱 408 高频
 *   - 隐含的 1:规格化尾数实际 24 位 (1+23)
 *   - 非规格化阶码是 1-bias 不是 -bias
 *   - 0 的表示:全 0 (符号位 0/1 都算 +0/-0)
 *   - 阶码范围 1~254 (0 和 255 是特殊)
 *   - 精度:float 约 7 位十进制,double 约 15 位
 *   - 比较浮点不能用 ==,要 EPSILON
 */
#ifndef CS408_CO_DATA_IEEE754_H
#define CS408_CO_DATA_IEEE754_H

#include "common/types.h"
#include "common/utils.h"
#include <cstdint>
#include <cstring>
#include <iostream>
#include <cmath>
#include <limits>

namespace cs408::co {

union FloatBits {
    float    f;
    uint32_t u;
};

// 拆解 float 各字段
struct FloatParts {
    int    sign;      // 0 或 1
    uint32_t exp;     // 8 位阶码原始值 (0~255)
    uint32_t mantissa;// 23 位尾数
    bool   is_zero;
    bool   is_inf;
    bool   is_nan;
    bool   is_denormal;
};

inline FloatParts decompose_float(float f) {
    FloatBits fb; fb.f = f;
    FloatParts p{};
    p.sign     = (fb.u >> 31) & 0x1;
    p.exp      = (fb.u >> 23) & 0xFF;
    p.mantissa = fb.u & 0x7FFFFF;
    p.is_zero     = (p.exp == 0 && p.mantissa == 0);
    p.is_inf      = (p.exp == 0xFF && p.mantissa == 0);
    p.is_nan      = (p.exp == 0xFF && p.mantissa != 0);
    p.is_denormal = (p.exp == 0 && p.mantissa != 0);
    return p;
}

inline uint32_t float_to_bits(float f) {
    FloatBits fb; fb.f = f;
    return fb.u;
}

inline float bits_to_float(uint32_t u) {
    FloatBits fb; fb.u = u;
    return fb.f;
}

// 真实阶码值 (减 bias 127),非规格化用 1-bias
inline int real_exponent(const FloatParts& p) {
    if (p.is_denormal) return 1 - 127;
    if (p.is_zero)     return 0;
    return static_cast<int>(p.exp) - 127;
}

// 真实尾数值 (含隐含的 1)
inline double real_mantissa(const FloatParts& p) {
    double m = static_cast<double>(p.mantissa) / (1 << 23);
    if (!p.is_denormal && !p.is_zero) m += 1.0;
    return m;
}

// 完整解析输出
inline void print_float_anatomy(float f) {
    FloatParts p = decompose_float(f);
    uint32_t bits = float_to_bits(f);
    std::cout << "值 = " << f << "\n";
    std::cout << "位 = 0x" << std::hex << bits << std::dec << "  ";
    std::cout << "二进制: ";
    for (int i = 31; i >= 0; --i) {
        std::cout << ((bits >> i) & 1);
        if (i == 31 || i == 23) std::cout << " ";
    }
    std::cout << "\n";
    std::cout << "  符号 S = " << p.sign << " (" << (p.sign ? "负" : "正") << ")\n";
    std::cout << "  阶码 E = " << p.exp << "  (移码) → 真值 e = " << real_exponent(p) << "\n";
    std::cout << "  尾数 M = 0x" << std::hex << p.mantissa << std::dec
              << "  → 1.M = " << real_mantissa(p) << "\n";
    if (p.is_zero)     std::cout << "  [零]\n";
    if (p.is_denormal) std::cout << "  [非规格化] 真值 = (-1)^S × 0.M × 2^(1-127)\n";
    if (p.is_inf)      std::cout << "  [无穷]\n";
    if (p.is_nan)      std::cout << "  [非数]\n";
    std::cout << "  公式: (-1)^" << p.sign << " × " << real_mantissa(p)
              << " × 2^" << real_exponent(p)
              << " = " << f << "\n";
}

// 手工构造 float (验证用)
inline float make_float(int sign, int exp_raw, uint32_t mantissa) {
    uint32_t u = (static_cast<uint32_t>(sign & 1) << 31)
               | (static_cast<uint32_t>(exp_raw & 0xFF) << 23)
               | (mantissa & 0x7FFFFF);
    return bits_to_float(u);
}

// 经典例子:-12.75 → 0xC14C0000
// 12.75 = 1100.11 = 1.10011 × 2^3
// 阶码 = 3 + 127 = 130 = 10000010
// 尾数 = 10011000...0 (小数部分 10011 后补 0)
// 符号 = 1
// 拼接: 1 10000010 10011000000000000000000 = 0xC14C0000
void ieee754_demo() {
    section("IEEE 754 单精度解剖");
    print_float_anatomy(12.75f);
    std::cout << "\n";
    print_float_anatomy(-12.75f);
    std::cout << "\n  手工构造验证: make_float(1,130,0x4C0000) = "
              << make_float(1, 130, 0x4C0000) << "\n";

    section("特殊值");
    print_float_anatomy(0.0f);
    print_float_anatomy(-0.0f);
    print_float_anatomy(std::numeric_limits<float>::infinity());
    print_float_anatomy(-std::numeric_limits<float>::infinity());
    print_float_anatomy(std::numeric_limits<float>::quiet_NaN());

    section("非规格化数 (最小正规数 / 2)");
    float min_normal = bits_to_float(0x00800000);  // 最小正规数
    float denorm     = bits_to_float(0x00400000);  // 非规格化 (一半)
    std::cout << "最小正规数 = " << min_normal << "\n";
    std::cout << "非规格化  = " << denorm << "  (公式: 0.M × 2^(1-127))\n";

    section("精度陷阱:0.1 + 0.2 ≠ 0.3");
    float a = 0.1f, b = 0.2f, c = 0.3f;
    std::cout << "0.1f = 0x" << std::hex << float_to_bits(a) << std::dec << "\n";
    std::cout << "0.2f = 0x" << std::hex << float_to_bits(b) << std::dec << "\n";
    std::cout << "0.3f = 0x" << std::hex << float_to_bits(c) << std::dec << "\n";
    std::cout << "0.1f + 0.2f == 0.3f ? " << ((a + b) == c ? "true" : "false (意料之中)")
              << "\n  差值 = " << ((a + b) - c) << "\n";
}

bool ieee754_test() {
    // -12.75 的位模式必须是 0xC14C0000
    CS408_EXPECT_EQ(float_to_bits(-12.75f), 0xC14C0000u);
    // 1.0 的位模式是 0x3F800000 (E=127, M=0)
    CS408_EXPECT_EQ(float_to_bits(1.0f), 0x3F800000u);
    // 0.5 = 1.0 × 2^-1, E = 127-1 = 126 = 0x7E
    CS408_EXPECT_EQ(float_to_bits(0.5f), 0x3F000000u);
    // 符号位独立
    CS408_EXPECT_EQ(float_to_bits(-0.0f), 0x80000000u);
    // 手工构造
    CS408_EXPECT_EQ(make_float(0, 128, 0), 2.0f);   // 1.0 × 2^1 = 2
    CS408_EXPECT_EQ(make_float(0, 128, 0x400000), 3.0f); // 1.5 × 2^1 = 3
    // 非规格化
    FloatParts p = decompose_float(bits_to_float(0x00400000));
    CS408_EXPECT(p.is_denormal);
    // Inf / NaN
    CS408_EXPECT(decompose_float(std::numeric_limits<float>::infinity()).is_inf);
    CS408_EXPECT(decompose_float(std::numeric_limits<float>::quiet_NaN()).is_nan);
    return true;
}

CS408_REGISTER_MODULE(
    "computer-organization", "data.ieee754", ieee754,
    "float:1S+8E+23M,bias=127;隐含 1;规格化 1.M×2^(E-127);非规格化 0.M×2^(1-127);E=255 特殊",
    "GPU FP16/BF16/FP8;BLAS/LAPACK;游戏物理;JSON 解析;DB decimal",
    "隐含 1;非规格化阶码 1-bias;0 有 ±0;阶码 1~254;float 7 位精度;浮点不能 == 比较",
    ieee754_demo, ieee754_test
);

} // namespace cs408::co
#endif // CS408_CO_DATA_IEEE754_H
