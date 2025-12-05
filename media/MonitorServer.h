#ifndef __MONITOR_SERVER_H__
#define __MONITOR_SERVER_H__

#include <set>
#include <string>
#include <mutex>
#include <thread>
#include <cstdint>
#include <sys/epoll.h>
#include <memory>
#include "UdpConnection.h"
#include "InetAddress.h"
#include <map>
extern "C"{
#include "ikcp.h"
}
// 简单的多路监控服务器（独立线程）：
// - 在一个 TCP 端口上接受 Qt 客户端

struct udpSession{
    InetAddress _videoUdpRtp;
    InetAddress _videoUdpRtcp;
    ikcpcb * ikcp;
    uint32_t conv;
    bool kcp_runing;
    std::thread _kcpThread;
    std::mutex _kcpMutex;
};

struct QtClient{
    int tcpFd;
    std::string _clientIp;      // 客户端IP
    map<std::string,std::unique_ptr<udpSession>> _sessionMap;
    std::string _buffer;
    int Cseq;
    // 新增：启用移动语义
    QtClient() = default; // 默认构造
    
    // 禁用拷贝（由 unique_ptr 隐式删除，但显式声明更清晰）
    QtClient(const QtClient&) = delete;
    QtClient& operator=(const QtClient&) = delete;

    // 启用移动
    QtClient(QtClient&&) = default; 
    QtClient& operator=(QtClient&&) = default;
};

class MonitorServer {
public:
    static MonitorServer &instance();

    // 启动监听线程（如果已启动则忽略）
    // ip 为服务器绑定 IP，port 为监听端口，例如 9000
    void start(const std::string &ip, unsigned short port);

    // 有新的 H264 NALU 到来时调用（线程安全）
    void onNaluTcp(const std::string &streamName,
                const uint8_t *data, size_t len);

    void onNaluUdp(std::string sessionId,const char *data, size_t len);
    void addCam(std::string sessionId,std::string stringName);
    void removeCam(std::string sessionId);
    
private:
    MonitorServer();
    ~MonitorServer();
    MonitorServer(const MonitorServer &) = delete;
    MonitorServer &operator=(const MonitorServer &) = delete;
    
    void acceptLoop();
    void createEpollFd();
    void addEpollReadFd(int fd);
    void delEpollReadFd(int fd);

    void parseRequest(const std::string& request, 
                        std::string& method, 
                        std::string& url,
                        std::map<std::string, std::string>& headers);
    void sendRespond(const std::map<std::string, std::string>& extraHeaders,const QtClient & client);
    ikcpcb* kcp_init(uint32_t conv,InetAddress *addr);
    static int kcp_output(const char *buf, int len, ikcpcb *kcp, void *user);
    uint32_t iclock() {
        using namespace std::chrono;
        return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }
    void kcp_update_thread(udpSession* session);
private:
    std::mutex _mtx;
    std::map<int,QtClient> _qtClients; // 客户端 socket fd
    int _epfd;
    int _listenFd{-1};
    bool _started{false};
    static int _udpServerRtpFd;
    static int _udpServerRtcpFd;
    vector<struct epoll_event> _evtList;
    map<std::string,std::string> _cams;
    InetAddress _server;
};

#endif
