#ifndef __RTSPSESSION_H__
#define __RTSPSESSION_H__

#include <iostream>
#include <string>
#include <chrono>
#include <map>

struct RtpChannel {
    int rtpSock = -1;          // RTP socket (UDP)
    int rtcpSock = -1;         // RTCP socket (UDP)
    uint16_t rtpPort = 0;      // 本地RTP端口
    uint16_t rtcpPort = 0;     // 本地RTCP端口
    uint16_t clientRtpPort = 0; // 客户端RTP端口
    uint16_t clientRtcpPort = 0;// 客户端RTCP端口
    std::string clientIp;      // 客户端IP
    std::string transport;     // "UDP", "TCP", "multicast"
};

class RtspSession{
public:
    explicit RtspSession(const std::string& sessionId);
    ~RtspSession();
    std::string getSessionId() const;
    void updateLastActive();
    bool isExpired(std::chrono::seconds timeout) const;
    void setClientIp(std::string clientIp);
    void setVideoChannel(uint16_t rtpPort,uint16_t rtcpPort,
        uint16_t clientRtpPort,uint16_t clientRtcpPort,std::string transport);
    void setAudioChannel();

private:
    std::string sessionId;
    std::chrono::steady_clock::time_point lastActive;//上次激活时间
    RtpChannel videoChannel;
    RtpChannel audioChannel;
    std::map<int,RtpChannel> tracks;
};

#endif