/**
 * @file tcp_state.h
 * @topic 网络 - 传输层 - TCP 状态机 (11 状态) 与三次握手/四次挥手
 *
 * @考点 408 大纲:计算机网络 > 传输层 > TCP 连接管理
 *   - 11 个状态:
 *     CLOSED, LISTEN, SYN_SENT, SYN_RCVD, ESTABLISHED,
 *     FIN_WAIT_1, FIN_WAIT_2, CLOSING, TIME_WAIT,
 *     CLOSE_WAIT, LAST_ACK
 *   - 三次握手 (建立连接):
 *     1) Client → SYN → Server         (Client: SYN_SENT)
 *     2) Server → SYN+ACK → Client     (Server: SYN_RCVD)
 *     3) Client → ACK → Server         (双方: ESTABLISHED)
 *   - 四次挥手 (释放连接):
 *     1) Client → FIN → Server         (Client: FIN_WAIT_1, Server: CLOSE_WAIT)
 *     2) Server → ACK → Client         (Client: FIN_WAIT_2)
 *     3) Server → FIN → Client         (Server: LAST_ACK)
 *     4) Client → ACK → Server         (Client: TIME_WAIT → CLOSED, Server: CLOSED)
 *   - TIME_WAIT: 2MSL (最长报文段寿命),防止最后 ACK 丢失 + 旧连接报文残留
 *   - MS (最大段长):MSS = MTU - 40 (TCP/IP 头)
 *
 * @业务 工业应用
 *   - Linux netstat / ss 显示 TCP 状态
 *   - 服务器 TIME_WAIT 堆积调优 (tw_reuse, tw_recycle)
 *   - HTTP keep-alive 减少 ESTABLISHED 切换
 *   - QUIC (UDP 上重建可靠连接,无三次握手)
 *   - 防火墙状态检测
 *
 * @陷阱 408 高频
 *   - 三次握手原因:确认双方收发能力,防止旧 SYN 误建连
 *   - 四次挥手原因:全双工,两端独立关闭
 *   - TIME_WAIT = 2MSL,服务器无此状态
 *   - CLOSE_WAIT 是被动关闭方收到 FIN 后的状态,需应用 close()
 *   - SYN Flood 攻击:发大量 SYN 不完成第三次握手 → SYN 队列满
 *   - 同时打开:11 状态能解释,双方都进入 SYN_RCVD
 *   - 同时关闭:双方都 FIN → CLOSING → TIME_WAIT
 */
#ifndef CS408_CN_TRANSPORT_TCP_STATE_H
#define CS408_CN_TRANSPORT_TCP_STATE_H

#include "common/types.h"
#include "common/utils.h"
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <sstream>

namespace cs408::cn {

enum class TCPState {
    CLOSED, LISTEN, SYN_SENT, SYN_RCVD, ESTABLISHED,
    FIN_WAIT_1, FIN_WAIT_2, CLOSING, TIME_WAIT,
    CLOSE_WAIT, LAST_ACK
};

inline const char* state_str(TCPState s) {
    switch (s) {
        case TCPState::CLOSED:      return "CLOSED";
        case TCPState::LISTEN:      return "LISTEN";
        case TCPState::SYN_SENT:    return "SYN_SENT";
        case TCPState::SYN_RCVD:    return "SYN_RCVD";
        case TCPState::ESTABLISHED: return "ESTABLISHED";
        case TCPState::FIN_WAIT_1:  return "FIN_WAIT_1";
        case TCPState::FIN_WAIT_2:  return "FIN_WAIT_2";
        case TCPState::CLOSING:     return "CLOSING";
        case TCPState::TIME_WAIT:   return "TIME_WAIT";
        case TCPState::CLOSE_WAIT:  return "CLOSE_WAIT";
        case TCPState::LAST_ACK:    return "LAST_ACK";
    }
    return "?";
}

enum class Event {
    APP_OPEN, APP_CLOSE, APP_SEND,
    RECV_SYN, RECV_ACK, RECV_FIN, RECV_SYN_ACK,
    RECV_FIN_ACK, TIMEOUT_2MSL
};

inline const char* event_str(Event e) {
    switch (e) {
        case Event::APP_OPEN:      return "APP:open()";
        case Event::APP_CLOSE:     return "APP:close()";
        case Event::APP_SEND:      return "APP:send()";
        case Event::RECV_SYN:      return "RECV:SYN";
        case Event::RECV_ACK:      return "RECV:ACK";
        case Event::RECV_FIN:      return "RECV:FIN";
        case Event::RECV_SYN_ACK:  return "RECV:SYN+ACK";
        case Event::RECV_FIN_ACK:  return "RECV:FIN+ACK";
        case Event::TIMEOUT_2MSL:  return "TIMEOUT(2MSL)";
    }
    return "?";
}

// TCP 状态机 (服务端视角 + 客户端视角统一)
class TCPStateMachine {
public:
    TCPStateMachine(TCPState init = TCPState::CLOSED) : state_(init) {}

    // 处理事件,返回发送什么 + 状态变化描述
    std::string handle(Event e) {
        std::ostringstream oss;
        oss << "[" << state_str(state_) << "] --" << event_str(e) << "--> ";
        TCPState next = state_;
        std::string send;
        switch (state_) {
            case TCPState::CLOSED:
                if (e == Event::APP_OPEN)         { next = TCPState::LISTEN; send = "(passive)"; }
                else if (e == Event::APP_SEND)    { next = TCPState::SYN_SENT; send = "→ SYN"; }
                break;
            case TCPState::LISTEN:
                if (e == Event::RECV_SYN)         { next = TCPState::SYN_RCVD; send = "→ SYN+ACK"; }
                else if (e == Event::APP_SEND)    { next = TCPState::SYN_SENT; send = "→ SYN"; }
                break;
            case TCPState::SYN_SENT:
                if (e == Event::RECV_SYN)         { next = TCPState::SYN_RCVD; send = "→ SYN+ACK"; }
                else if (e == Event::RECV_SYN_ACK){ next = TCPState::ESTABLISHED; send = "→ ACK"; }
                break;
            case TCPState::SYN_RCVD:
                if (e == Event::RECV_ACK)         { next = TCPState::ESTABLISHED; send = "(none)"; }
                break;
            case TCPState::ESTABLISHED:
                if (e == Event::APP_CLOSE)        { next = TCPState::FIN_WAIT_1; send = "→ FIN"; }
                else if (e == Event::RECV_FIN)    { next = TCPState::CLOSE_WAIT; send = "→ ACK"; }
                break;
            case TCPState::FIN_WAIT_1:
                if (e == Event::RECV_ACK)         { next = TCPState::FIN_WAIT_2; send = "(none)"; }
                else if (e == Event::RECV_FIN)    { next = TCPState::CLOSING; send = "→ ACK"; }
                else if (e == Event::RECV_FIN_ACK){ next = TCPState::TIME_WAIT; send = "→ ACK"; }
                break;
            case TCPState::FIN_WAIT_2:
                if (e == Event::RECV_FIN)         { next = TCPState::TIME_WAIT; send = "→ ACK"; }
                break;
            case TCPState::CLOSING:
                if (e == Event::RECV_ACK)         { next = TCPState::TIME_WAIT; send = "(none)"; }
                break;
            case TCPState::TIME_WAIT:
                if (e == Event::TIMEOUT_2MSL)     { next = TCPState::CLOSED; send = "(closed)"; }
                break;
            case TCPState::CLOSE_WAIT:
                if (e == Event::APP_CLOSE)        { next = TCPState::LAST_ACK; send = "→ FIN"; }
                break;
            case TCPState::LAST_ACK:
                if (e == Event::RECV_ACK)         { next = TCPState::CLOSED; send = "(closed)"; }
                break;
        }
        state_ = next;
        oss << "[" << state_str(state_) << "]  " << send;
        return oss.str();
    }

    TCPState state() const { return state_; }

private:
    TCPState state_;
};

void tcp_state_demo() {
    section("TCP 三次握手 (Client + Server 视角)");
    TCPStateMachine client, server(TCPState::LISTEN);
    std::cout << "初始: Client=" << state_str(client.state())
              << ", Server=" << state_str(server.state()) << "\n";

    std::cout << "\n[1] Client 主动连接:\n  ";
    std::cout << client.handle(Event::APP_SEND) << "\n";
    std::cout << "\n[2] Server 收到 SYN:\n  ";
    std::cout << server.handle(Event::RECV_SYN) << "\n";
    std::cout << "\n[3] Client 收到 SYN+ACK:\n  ";
    std::cout << client.handle(Event::RECV_SYN_ACK) << "\n";
    std::cout << "\n[4] Server 收到 ACK:\n  ";
    std::cout << server.handle(Event::RECV_ACK) << "\n";
    std::cout << "\n  → 双方 ESTABLISHED,连接建立\n";

    section("TCP 四次挥手");
    std::cout << "[1] Client 主动关闭:\n  ";
    std::cout << client.handle(Event::APP_CLOSE) << "\n";
    std::cout << "\n[2] Server 收到 FIN:\n  ";
    std::cout << server.handle(Event::RECV_FIN) << "\n";
    std::cout << "\n[3] Client 收到 ACK:\n  ";
    std::cout << client.handle(Event::RECV_ACK) << "\n";
    std::cout << "\n[4] Server 应用关闭:\n  ";
    std::cout << server.handle(Event::APP_CLOSE) << "\n";
    std::cout << "\n[5] Client 收到 FIN:\n  ";
    std::cout << client.handle(Event::RECV_FIN) << "\n";
    std::cout << "\n[6] Server 收到 ACK:\n  ";
    std::cout << server.handle(Event::RECV_ACK) << "\n";
    std::cout << "\n[7] Client 等 2MSL:\n  ";
    std::cout << client.handle(Event::TIMEOUT_2MSL) << "\n";
    std::cout << "\n  → 双方 CLOSED,连接完全释放\n";

    section("TCP 11 状态机表");
    std::cout << "CLOSED ─APP_OPEN→ LISTEN ─RECV_SYN→ SYN_RCVD ─RECV_ACK→ ESTABLISHED\n";
    std::cout << "CLOSED ─APP_SEND→ SYN_SENT ─RECV_SYN_ACK→ ESTABLISHED\n";
    std::cout << "ESTABLISHED ─APP_CLOSE→ FIN_WAIT_1 ─RECV_ACK→ FIN_WAIT_2 ─RECV_FIN→ TIME_WAIT ─2MSL→ CLOSED\n";
    std::cout << "ESTABLISHED ─RECV_FIN→ CLOSE_WAIT ─APP_CLOSE→ LAST_ACK ─RECV_ACK→ CLOSED\n";
    std::cout << "FIN_WAIT_1 ─RECV_FIN→ CLOSING ─RECV_ACK→ TIME_WAIT (同时关闭)\n";

    section("TIME_WAIT 存在原因 (2MSL)");
    std::cout << "1) 保证最后 ACK 到达对方 (若丢失,对方重发 FIN)\n";
    std::cout << "2) 让旧连接的报文段消失 (防止误判为新连接数据)\n";
    std::cout << "   MSL (最长段寿命) 默认 2 分钟 → 2MSL = 4 分钟\n";
    std::cout << "   Linux 默认 60s\n";

    section("服务器 TIME_WAIT 调优");
    std::cout << "  短连接高频场景 → TIME_WAIT 堆积耗尽端口\n";
    std::cout << "  · tcp_tw_reuse=1: 允许复用 TIME_WAIT 连接 (客户端)\n";
    std::cout << "  · tcp_tw_recycle=1: 快速回收 (NAT 环境有问题,已废弃)\n";
    std::cout << "  · tcp_max_tw_buckets: 限制最大数量\n";
    std::cout << "  · 改用长连接 (HTTP keep-alive) 是根治\n";

    section("SYN Flood 攻击");
    std::cout << "  攻击者发大量 SYN,不完成第三次握手\n";
    std::cout << "  → 服务器 SYN 队列满,正常用户无法连接\n";
    std::cout << "  防御:SYN cookies (不分配资源,加密返回)\n";
}

bool tcp_state_test() {
    // 客户端三次握手
    TCPStateMachine c;
    CS408_EXPECT(c.state() == TCPState::CLOSED);
    c.handle(Event::APP_SEND);
    CS408_EXPECT(c.state() == TCPState::SYN_SENT);
    c.handle(Event::RECV_SYN_ACK);
    CS408_EXPECT(c.state() == TCPState::ESTABLISHED);

    // 服务器三次握手
    TCPStateMachine s(TCPState::LISTEN);
    s.handle(Event::RECV_SYN);
    CS408_EXPECT(s.state() == TCPState::SYN_RCVD);
    s.handle(Event::RECV_ACK);
    CS408_EXPECT(s.state() == TCPState::ESTABLISHED);

    // 客户端主动关闭
    c.handle(Event::APP_CLOSE);
    CS408_EXPECT(c.state() == TCPState::FIN_WAIT_1);
    c.handle(Event::RECV_ACK);
    CS408_EXPECT(c.state() == TCPState::FIN_WAIT_2);
    c.handle(Event::RECV_FIN);
    CS408_EXPECT(c.state() == TCPState::TIME_WAIT);
    c.handle(Event::TIMEOUT_2MSL);
    CS408_EXPECT(c.state() == TCPState::CLOSED);

    // 服务器被动关闭
    s.handle(Event::RECV_FIN);
    CS408_EXPECT(s.state() == TCPState::CLOSE_WAIT);
    s.handle(Event::APP_CLOSE);
    CS408_EXPECT(s.state() == TCPState::LAST_ACK);
    s.handle(Event::RECV_ACK);
    CS408_EXPECT(s.state() == TCPState::CLOSED);

    return true;
}

CS408_REGISTER_MODULE(
    "computer-networks", "transport.tcp_state", tcp_state,
    "11 状态:CLOSED/LISTEN/SYN_SENT/SYN_RCVD/ESTABLISHED/FIN_WAIT1/2/CLOSING/TIME_WAIT/CLOSE_WAIT/LAST_ACK",
    "Linux netstat/ss;TIME_WAIT 调优;HTTP keep-alive;QUIC 无握手;防火墙状态检测",
    "三次握手确认双方收发;TIME_WAIT=2MSL 防丢+防旧报文;CLOSE_WAIT 被动需 app close;SYN Flood",
    tcp_state_demo, tcp_state_test
);

} // namespace cs408::cn
#endif // CS408_CN_TRANSPORT_TCP_STATE_H
