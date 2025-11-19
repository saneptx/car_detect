#ifndef __MONITOR_SERVER_H__
#define __MONITOR_SERVER_H__

#include <set>
#include <string>
#include <mutex>
#include <thread>
#include <cstdint>

// 简单的多路监控服务器（独立线程）：
// - 在一个 TCP 端口上接受 Qt 客户端
// - 将每条推流(session)的 H264 NALU 按自定义协议转发出去
//
// 协议(网络字节序):
// [uint16_t streamNameLen][streamName bytes]
// [uint32_t timestamp]
// [uint32_t frameLen][frame bytes]
class MonitorServer {
public:
    static MonitorServer &instance();

    // 启动监听线程（如果已启动则忽略）
    // ip 为服务器绑定 IP，port 为监听端口，例如 9000
    void start(const std::string &ip, unsigned short port);

    // 有新的 H264 NALU 到来时调用（线程安全）
    void onNalu(const std::string &streamName,
                const uint8_t *data, size_t len);

private:
    MonitorServer() = default;
    ~MonitorServer() = default;
    MonitorServer(const MonitorServer &) = delete;
    MonitorServer &operator=(const MonitorServer &) = delete;

    void acceptLoop(const std::string &ip, unsigned short port);

private:
    std::mutex _mtx;
    std::set<int> _clients; // 客户端 socket fd
    int _listenFd{-1};
    bool _started{false};
};

#endif
