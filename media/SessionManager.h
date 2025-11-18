#ifndef __SESSIONMANAGER_H_
#define __SESSIONMANAGER_H_

#include <map>
#include <mutex>
#include <string>
#include <atomic>
#include <memory>
#include "UdpConnection.h"
#include "Logger.h"


struct RtspSession{
    std::string sessionId;
    std::chrono::steady_clock::time_point lastActive;//上次激活时间
    std::string clientIp;      // 客户端IP
    std::shared_ptr<UdpConnection> videoRtpConn = nullptr;
    std::shared_ptr<UdpConnection> videoRtcpConn = nullptr;
    std::shared_ptr<UdpConnection> audioRtpConn = nullptr;
    std::shared_ptr<UdpConnection> audioRtcpConn = nullptr;
    std::string transportType;
    std::string stringName;
    void setStreamName(const std::string &name) { this->stringName = name; }
    const std::string &getStreamName() const { return this->stringName; }
};

class SessionManager{
public:
    SessionManager();
    ~SessionManager();
    void addSession(RtspSession session);
    void removeSession(const std::string& sessionId);
    RtspSession* getSession(const std::string& sessionId);
    uint16_t allocateUdpPorts(); 
    std::string generateSessionId();
    uint32_t getSessionCount(){
        return _sessionCounter;
    }
private:  
    std::map<std::string, RtspSession> _sessions;
    std::mutex _sessionMutex;
    std::mutex _portMutex; 
    std::atomic<int> nextUdpPort{10000};
    uint32_t _sessionCounter = 0;
};

#endif