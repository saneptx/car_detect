#include "MonitorServer.h"
#include "Logger.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <chrono>


MonitorServer &MonitorServer::instance() {
    static MonitorServer inst;
    return inst;
}

void MonitorServer::start(const std::string &ip, unsigned short port) {
    if (_started) return;
    _started = true;

    std::thread([this, ip, port]() {
        acceptLoop(ip, port);
    }).detach();
}

void MonitorServer::acceptLoop(const std::string &ip, unsigned short port) {
    int listenfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        LOG_ERROR("MonitorServer socket create failed: %s", strerror(errno));
        return;
    }

    int on = 1;
    ::setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());

    if (::bind(listenfd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("MonitorServer bind failed on %s:%d: %s", ip.c_str(), port, strerror(errno));
        ::close(listenfd);
        return;
    }

    if (::listen(listenfd, 8) < 0) {
        LOG_ERROR("MonitorServer listen failed: %s", strerror(errno));
        ::close(listenfd);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(_mtx);
        _listenFd = listenfd;
    }
    LOG_INFO("MonitorServer listening on %s:%d", ip.c_str(), port);

    while (true) {
        sockaddr_in cliAddr;
        socklen_t len = sizeof(cliAddr);
        int connfd = ::accept(listenfd, (sockaddr*)&cliAddr, &len);
        if (connfd < 0) {
            LOG_ERROR("MonitorServer accept failed: %s", strerror(errno));
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(_mtx);
            _clients.insert(connfd);
        }
        LOG_INFO("MonitorServer new client fd=%d", connfd);
    }
}

void MonitorServer::onNalu(const std::string &streamName,
                           const uint8_t *data, size_t len) {
    if (streamName.empty() || !data || len == 0) return;

    std::lock_guard<std::mutex> lock(_mtx);
    if (_clients.empty()) return;
    std::string buf;
    uint16_t nameLen = static_cast<uint16_t>(streamName.size());//长度为10
    uint16_t nameLenNet = htons(nameLen);
    buf.resize(sizeof(nameLenNet) + nameLen + len);
    size_t offset = 0;
    std::memcpy(&buf[offset],&nameLenNet,sizeof(nameLenNet));
    offset += sizeof(nameLenNet);
    std::memcpy(&buf[offset],streamName.data(),nameLen);
    offset += nameLen;
    std::memcpy(&buf[offset],data,len);
    // 发送给所有已连接客户端；若发送失败则移除该客户端
    std::set<int> badFds;
    for (int fd : _clients) {
        ssize_t n = ::send(fd, buf.data(), buf.size(), MSG_NOSIGNAL|MSG_DONTWAIT);
        LOG_DEBUG("send %d seq, %d data",(data[6]<<8|data[7]),n);
        if (n < 0) {
            LOG_WARN("MonitorServer send failed on fd %d: %s", fd, strerror(errno));
            badFds.insert(fd);
        }
    }
    for (int fd : badFds) {
        ::close(fd);
        _clients.erase(fd);
    }
}
