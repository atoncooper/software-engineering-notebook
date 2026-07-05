/**
 * @file utils.h
 * @brief 打印、二进制格式化、范围生成等通用工具
 *
 * @考点 二进制位级观察是计组/网络核心技能 (IEEE754/CRC/IP 首部)
 * @业务 真实系统日志常用十六进制 dump (如 Wireshark 报文)
 * @陷阱 bit_width 与字节对齐:32 位地址按 8 位分组打印
 */
#ifndef CS408_COMMON_UTILS_H
#define CS408_COMMON_UTILS_H

#include "types.h"
#include <bitset>
#include <sstream>
#include <iomanip>
#include <string>

namespace cs408 {

inline void section(const std::string& title) {
    std::cout << "\n======== " << title << " ========\n";
}

inline void info(const std::string& msg) {
    std::cout << "[INFO] " << msg << "\n";
}

// 二进制字符串 (32 位地址 / IEEE754 等)
inline std::string bin(uint32_t v, int width = 32) {
    std::string s;
    for (int i = width - 1; i >= 0; --i) {
        s += ((v >> i) & 1) ? '1' : '0';
        if (i > 0 && i % 4 == 0) s += ' ';
    }
    return s;
}

inline std::string bin16(uint16_t v) { return bin(v, 16); }
inline std::string bin8 (uint8_t  v) { return bin(v, 8);  }

// 十六进制
inline std::string hex(uint32_t v, int width = 8) {
    std::ostringstream os;
    os << "0x" << std::setfill('0') << std::setw(width) << std::hex << v;
    return os.str();
}

// IP 地址格式化 (网络字节序)
inline std::string ipv4(uint32_t ip) {
    return std::to_string((ip >> 24) & 0xFF) + "." +
           std::to_string((ip >> 16) & 0xFF) + "." +
           std::to_string((ip >> 8)  & 0xFF) + "." +
           std::to_string( ip        & 0xFF);
}

// MAC 地址格式化 (48 位)
inline std::string mac(uint64_t m) {
    std::ostringstream os;
    os << std::hex << std::setfill('0');
    for (int i = 5; i >= 0; --i) {
        if (i < 5) os << ":";
        os << std::setw(2) << (int)((m >> (i * 8)) & 0xFF);
    }
    return os.str();
}

template <typename T>
inline void print_vec(const std::vector<T>& v, const std::string& label = "") {
    if (!label.empty()) std::cout << label << ": ";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) std::cout << ", ";
        std::cout << v[i];
    }
    std::cout << "\n";
}

} // namespace cs408

#endif // CS408_COMMON_UTILS_H
