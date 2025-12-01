#ifndef __MONITOR_SERVER_H__
#define __MONITOR_SERVER_H__

#include <set>
#include <string>
#include <mutex>
#include <thread>
#include <cstdint>
#include <sys/epoll.h>
#include "UdpConnection.h"
#include "InetAddress.h"
// 简单的多路监控服务器（独立线程）：
// - 在一个 TCP 端口上接受 Qt 客户端
// - 将每条推流(session)的 H264 NALU 按自定义协议转发出去
//
// 协议(网络字节序):
// [uint16_t streamNameLen][streamName bytes]
// [uint32_t timestamp]
// [uint32_t frameLen][frame bytes]

struct udpPort{
    InetAddress _videoUdpRtp;
    InetAddress _videoUdpRtcp; 
};

struct QtClient{
    int tcpFd;
    std::string _clientIp;      // 客户端IP
    map<std::string,udpPort> _portMap;
    std::string _buffer;
    int Cseq;
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

    void onNaluUdp(std::string sessionId,const uint8_t *data, size_t len);
    void addCam(std::string sessionId,std::string stringName);
    void removeCam(std::string sessionId);
    
private:
    MonitorServer();
    ~MonitorServer() = default;
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
    void sendRespond(const std::map<std::string, std::string>& extraHeaders,const QtClient client);
private:
    std::mutex _mtx;
    std::map<int,QtClient> _qtClients; // 客户端 socket fd
    int _epfd;
    int _listenFd{-1};
    bool _started{false};
    int _udpServerRtpFd;
    int _udpServerRtcpFd;
    vector<struct epoll_event> _evtList;
    map<std::string,std::string> _cams;
    InetAddress _server;
};

#endif
