#include "MonitorServer.h"
#include "Logger.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <chrono>

namespace {
inline uint64_t hostToNetwork64(uint64_t value) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint32_t high = htonl(static_cast<uint32_t>(value >> 32));
    uint32_t low  = htonl(static_cast<uint32_t>(value & 0xFFFFFFFFULL));
    return (static_cast<uint64_t>(low) << 32) | high;
#else
    return value;
#endif
}
}

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
                           const uint8_t *data, size_t len,
                           uint32_t timestamp) {
    if (streamName.empty() || !data || len == 0) return;

    std::lock_guard<std::mutex> lock(_mtx);
    if (_clients.empty()) return;

    uint16_t nameLen = static_cast<uint16_t>(streamName.size());
    uint32_t ts = timestamp;
    uint32_t frameLen = static_cast<uint32_t>(len);

    uint16_t nameLenNet = htons(nameLen);
    uint32_t tsNet = htonl(ts);
    uint32_t frameLenNet = htonl(frameLen);

    uint64_t sendTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::system_clock::now().time_since_epoch()).count();
    uint64_t sendTimeNet = hostToNetwork64(sendTimeUs);

    std::string buf;
    buf.resize(sizeof(nameLenNet) + nameLen + sizeof(tsNet) + sizeof(frameLenNet) + sizeof(sendTimeNet) + frameLen);
    size_t offset = 0;
    std::memcpy(&buf[offset], &nameLenNet, sizeof(nameLenNet));
    offset += sizeof(nameLenNet);
    std::memcpy(&buf[offset], streamName.data(), nameLen);
    offset += nameLen;
    std::memcpy(&buf[offset], &tsNet, sizeof(tsNet));
    offset += sizeof(tsNet);
    std::memcpy(&buf[offset], &frameLenNet, sizeof(frameLenNet));
    offset += sizeof(frameLenNet);
    std::memcpy(&buf[offset], &sendTimeNet, sizeof(sendTimeNet));
    offset += sizeof(sendTimeNet);
    std::memcpy(&buf[offset], data, frameLen);

    // 发送给所有已连接客户端；若发送失败则移除该客户端
    std::set<int> badFds;
    for (int fd : _clients) {
        ssize_t n = ::send(fd, buf.data(), buf.size(), MSG_NOSIGNAL|MSG_DONTWAIT);
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
