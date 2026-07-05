/**
 * @file subnet.h
 * @topic 网络 - 网络层 - 子网划分/CIDR/路由聚合/最长前缀匹配
 *
 * @考点 408 大纲:计算机网络 > 网络层 > IPv4 与子网划分
 *   - IPv4 地址:32 位,分网络号 + 主机号
 *   - 分类地址:A/B/C/D/E (A 1-126, B 128-191, C 192-223)
 *   - 子网划分:借主机位作子网号
 *     子网掩码 & IP = 网络地址
 *   - CIDR 无分类编址:a.b.c.d/n,n=前缀长度
 *   - 路由聚合 (超网):合并多个子网,前缀更短
 *   - 最长前缀匹配:路由表中选前缀最长的匹配项
 *   - 私有地址:10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16
 *   - 特殊地址:127.0.0.0/8 (环回), 169.254.0.0/16 (链路本地), 0.0.0.0/0 (默认)
 *
 * @业务 工业应用
 *   - AWS VPC 子网设计 (CIDR 规划)
 *   - BGP 路由聚合 (减少全球路由表)
 *   - K8s Pod 网络 (Calico/Flannel CIDR 分配)
 *   - CDN 任播 (多机房共用 IP)
 *   - NAT 网关 (私网→公网)
 *
 * @陷阱 408 高频
 *   - 主机号全 0 = 网络地址,全 1 = 广播地址 → 不可分配
 *   - 可用主机数 = 2^h - 2 (h=主机位数)
 *   - 子网数 = 2^s (s=借的子网位),有的考题要求 -2 (老规则)
 *   - 最长前缀匹配:多个匹配时选前缀最长的
 *   - 路由聚合后前缀更短,但子网必须连续 (前缀相同)
 *   - 192.168.1.0/24 与 192.168.2.0/24 不能直接聚合 (前 23 位不同)
 *     实际:192.168.0.0/22 才能含 192.168.0/1/2/3
 *   - 划分子网不增加网络数量?错!借位后子网数变多
 */
#ifndef CS408_CN_NETWORK_SUBNET_H
#define CS408_CN_NETWORK_SUBNET_H

#include "common/types.h"
#include "common/utils.h"
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <cstdint>
#include <algorithm>

namespace cs408::cn {

inline uint32_t ipv4_to_u32(const std::string& s) {
    int a, b, c, d;
    char dot;
    std::istringstream iss(s);
    iss >> a >> dot >> b >> dot >> c >> dot >> d;
    return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(c) << 8)  | static_cast<uint32_t>(d);
}

inline std::string u32_to_ipv4(uint32_t u) {
    return std::to_string((u >> 24) & 0xFF) + "." +
           std::to_string((u >> 16) & 0xFF) + "." +
           std::to_string((u >> 8)  & 0xFF) + "." +
           std::to_string(u & 0xFF);
}

inline uint32_t mask_from_prefix(int prefix) {
    return prefix == 0 ? 0u : ~((1u << (32 - prefix)) - 1);
}

inline int prefix_from_mask(uint32_t mask) {
    int n = 0;
    while ((mask & 0x80000000u) != 0) { ++n; mask <<= 1; }
    return n;
}

struct IPv4Network {
    uint32_t addr;     // 网络地址
    int       prefix;  // 前缀长度

    uint32_t mask()     const { return mask_from_prefix(prefix); }
    uint32_t broadcast()const { return addr | ~mask(); }
    uint32_t first_host()const { return addr + 1; }
    uint32_t last_host() const { return broadcast() - 1; }
    int      host_bits() const { return 32 - prefix; }
    long long usable_hosts() const {
        if (prefix >= 31) return (1LL << host_bits()); // /31 P2P, /32 单机
        return (1LL << host_bits()) - 2;
    }
    bool contains(uint32_t ip) const { return (ip & mask()) == addr; }
};

inline IPv4Network parse_cidr(const std::string& cidr) {
    auto slash = cidr.find('/');
    std::string ip = cidr.substr(0, slash);
    int prefix = std::stoi(cidr.substr(slash + 1));
    uint32_t a = ipv4_to_u32(ip);
    return {a & mask_from_prefix(prefix), prefix};
}

inline std::string format(const IPv4Network& n) {
    return u32_to_ipv4(n.addr) + "/" + std::to_string(n.prefix);
}

// 把一个 CIDR 块划分为 N 个等大子网 (借 log2(N) 位)
inline std::vector<IPv4Network> subnet_split(const IPv4Network& net, int subnets) {
    std::vector<IPv4Network> result;
    int bits_needed = 0;
    while ((1 << bits_needed) < subnets) ++bits_needed;
    int new_prefix = net.prefix + bits_needed;
    if (new_prefix > 32) return result;
    uint32_t block_size = 1u << (32 - new_prefix);
    for (int i = 0; i < subnets; ++i) {
        result.push_back({net.addr + i * block_size, new_prefix});
    }
    return result;
}

// 路由聚合 (超网构造):把连续子网合并,返回最短前缀
// 要求:子网连续,起始地址对齐
inline IPv4Network aggregate(const std::vector<IPv4Network>& nets) {
    if (nets.empty()) return {0, 0};
    int min_prefix = 32;
    for (auto& n : nets) min_prefix = std::min(min_prefix, n.prefix);
    uint32_t base = nets[0].addr;
    // 找最长公共前缀
    uint32_t cur_mask = mask_from_prefix(min_prefix);
    while (min_prefix > 0) {
        bool ok = true;
        for (auto& n : nets) {
            if ((n.addr & cur_mask) != (base & cur_mask)) { ok = false; break; }
        }
        if (ok) break;
        --min_prefix;
        cur_mask = mask_from_prefix(min_prefix);
    }
    return {base & cur_mask, min_prefix};
}

// 最长前缀匹配:在路由表中找最匹配的项
struct RouteEntry { IPv4Network net; std::string next_hop; };

inline const RouteEntry* longest_prefix_match(
    const std::vector<RouteEntry>& table, uint32_t dst) {
    const RouteEntry* best = nullptr;
    for (const auto& e : table) {
        if (e.net.contains(dst)) {
            if (!best || e.net.prefix > best->net.prefix) best = &e;
        }
    }
    return best;
}

void subnet_demo() {
    section("IPv4 地址解析");
    auto net = parse_cidr("192.168.1.0/24");
    std::cout << "网络: " << format(net) << "\n";
    std::cout << "掩码: " << u32_to_ipv4(net.mask()) << "\n";
    std::cout << "广播: " << u32_to_ipv4(net.broadcast()) << "\n";
    std::cout << "可用主机: " << net.usable_hosts() << " (2^8 - 2 = 254)\n";
    std::cout << "主机范围: " << u32_to_ipv4(net.first_host()) << " ~ "
              << u32_to_ipv4(net.last_host()) << "\n";

    section("子网划分 (192.168.1.0/24 → 4 子网)");
    auto subs = subnet_split(parse_cidr("192.168.1.0/24"), 4);
    for (const auto& s : subs) {
        std::cout << "  " << format(s) << "  掩码 "
                  << u32_to_ipv4(s.mask()) << "  主机 "
                  << s.usable_hosts() << "  范围 "
                  << u32_to_ipv4(s.first_host()) << "~"
                  << u32_to_ipv4(s.last_host()) << "\n";
    }

    section("路由聚合 (4 个 /24 → 1 个 /22)");
    std::vector<IPv4Network> nets = {
        parse_cidr("192.168.0.0/24"),
        parse_cidr("192.168.1.0/24"),
        parse_cidr("192.168.2.0/24"),
        parse_cidr("192.168.3.0/24"),
    };
    auto supernet = aggregate(nets);
    std::cout << "聚合后: " << format(supernet) << "\n";
    std::cout << "  → 4 个 C 类合并为 /22 超网\n";

    section("最长前缀匹配");
    std::vector<RouteEntry> table = {
        {parse_cidr("0.0.0.0/0"),     "默认网关"},
        {parse_cidr("192.168.0.0/22"), "核心路由器"},
        {parse_cidr("192.168.1.0/24"), "本地子网"},
        {parse_cidr("192.168.1.128/25"), "服务器区"},
    };
    for (const auto& dst : {"192.168.1.130", "192.168.1.5", "192.168.2.10", "8.8.8.8"}) {
        uint32_t ip = ipv4_to_u32(dst);
        const RouteEntry* r = longest_prefix_match(table, ip);
        std::cout << "  " << dst << " → "
                  << (r ? r->next_hop + " (via " + format(r->net) + ")" : "无匹配")
                  << "\n";
    }
    std::cout << "  → 192.168.1.130 选 /25 (最长前缀),8.8.8.8 选默认路由 /0\n";

    section("私有地址 & 特殊地址");
    std::cout << "私有: 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16\n";
    std::cout << "环回: 127.0.0.0/8 (127.0.0.1 测试)\n";
    std::cout << "链路本地: 169.254.0.0/16 (DHCP 失败自动配)\n";
    std::cout << "默认路由: 0.0.0.0/0\n";
}

bool subnet_test() {
    // IP 转换
    CS408_EXPECT_EQ(ipv4_to_u32("192.168.1.1"), 0xC0A80101u);
    CS408_EXPECT_EQ(u32_to_ipv4(0xC0A80101u), std::string("192.168.1.1"));

    // 掩码
    CS408_EXPECT_EQ(mask_from_prefix(24), 0xFFFFFF00u);
    CS408_EXPECT_EQ(mask_from_prefix(22), 0xFFFFFC00u);
    CS408_EXPECT_EQ(mask_from_prefix(0),  0u);

    // 主机数
    CS408_EXPECT_EQ(parse_cidr("192.168.1.0/24").usable_hosts(), 254);
    CS408_EXPECT_EQ(parse_cidr("10.0.0.0/8").usable_hosts(),
                    (1LL << 24) - 2);
    CS408_EXPECT_EQ(parse_cidr("192.168.1.0/30").usable_hosts(), 2);

    // 子网划分
    auto subs = subnet_split(parse_cidr("192.168.1.0/24"), 4);
    CS408_EXPECT_EQ(static_cast<int>(subs.size()), 4);
    CS408_EXPECT_EQ(subs[0].prefix, 26);
    CS408_EXPECT_EQ(subs[1].addr, subs[0].addr + 64);

    // 路由聚合
    std::vector<IPv4Network> nets = {
        parse_cidr("192.168.0.0/24"),
        parse_cidr("192.168.1.0/24"),
        parse_cidr("192.168.2.0/24"),
        parse_cidr("192.168.3.0/24"),
    };
    auto agg = aggregate(nets);
    CS408_EXPECT_EQ(agg.prefix, 22);

    // 最长前缀匹配
    std::vector<RouteEntry> table = {
        {parse_cidr("0.0.0.0/0"),     "default"},
        {parse_cidr("192.168.0.0/22"), "core"},
        {parse_cidr("192.168.1.0/24"), "lan"},
        {parse_cidr("192.168.1.128/25"), "srv"},
    };
    CS408_EXPECT_EQ(longest_prefix_match(table, ipv4_to_u32("192.168.1.130"))->next_hop,
                    std::string("srv"));
    CS408_EXPECT_EQ(longest_prefix_match(table, ipv4_to_u32("8.8.8.8"))->next_hop,
                    std::string("default"));

    return true;
}

CS408_REGISTER_MODULE(
    "computer-networks", "network.subnet", subnet,
    "IPv4 网络号+主机号;CIDR a.b.c.d/n;子网借位;路由聚合前缀变短;最长前缀匹配",
    "AWS VPC;BGP 聚合;K8s Pod CIDR;CDN 任播;NAT 网关",
    "主机全0=网络,全1=广播,可用=2^h-2;最长前缀匹配;聚合要连续对齐;私有10/172.16/192.168;127环回",
    subnet_demo, subnet_test
);

} // namespace cs408::cn
#endif // CS408_CN_NETWORK_SUBNET_H
