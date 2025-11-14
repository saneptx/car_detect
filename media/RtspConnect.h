#ifndef __RTSPCONNECT_H__
#define __RTSPCONNECT_H__

#include "TcpConnection.h"
#include "UdpConnection.h"
#include <memory>
#include <string>
#include <map>
#include "SessionManager.h"
#include <memory>

class TcpConnection;
class UdpConnection;
class EventLoop;
// class RtpH264Mp4Recorder;
class RtpH264Unpacker;


// RTSP会话状态
enum class RtspState {
    INIT,      // 初始状态
    READY,     // SETUP完成，准备播放
    PLAYING    // 正在播放
};

class RtspConnect : public std::enable_shared_from_this<RtspConnect> {
public:
    RtspConnect(std::shared_ptr<TcpConnection> tcpConn, EventLoop* loop);
    ~RtspConnect();

    // 处理RTSP请求
    void handleRequest(const std::string& request);
    
    // 获取会话状态
    RtspState getState() const { return _state; }
    
    void releaseSession();
    void onInterleavedFrame(uint8_t ch, const uint8_t* data, size_t len);
    
private:
    // 解析RTSP请求
    void parseRequest(const std::string& request, std::string& method, 
                     std::string& url, std::map<std::string, std::string>& headers);
    
    // 提取CSeq
    int extractCSeq(const std::map<std::string, std::string>& headers);
    
    // 处理OPITIONS请求
    void handleOpitions(const std::string& request,
        const std::map<std::string, std::string>& headers, int cseq);
    // 处理ANNOUNCE请求（推流场景）
    void handleAnnounce(const std::string& request,
                        const std::map<std::string, std::string>& headers, int cseq);
    
    // 处理SETUP请求
    void handleSetup(const std::string& url, 
                    const std::map<std::string, std::string>& headers, int cseq);
    

    // 处理RECORD请求（推流开始）
    void handleRecord(const std::string& url,
                      const std::map<std::string, std::string>& headers, int cseq);
    
    void handleTeardown(const std::string& url,
                        const std::map<std::string, std::string>& headers, int cseq);
    // 发送RTSP响应
    void sendResponse(int statusCode, const std::string& statusText, 
                     const std::map<std::string, std::string>& extraHeaders, int cseq);
    // 发送RTSP响应（包含可选body）
    void sendResponse(int statusCode, const std::string& statusText,
                     const std::map<std::string, std::string>& extraHeaders, int cseq,
                     const std::string& body);
    void handleTeardown();
    // 从Transport头中解析传输方式和端口
    // 返回: true=成功, false=失败
    // 输出: transportType="UDP"或"TCP"
    //      UDP模式: clientRtpPort, clientRtcpPort, serverRtpPort, serverRtcpPort
    //      TCP模式: rtpChannel, rtcpChannel (interleaved通道号)
    bool parseTransport(const std::string& transport, 
                       std::string& transportType,
                       uint16_t& clientRtpPort, uint16_t& clientRtcpPort,
                       uint16_t& serverRtpPort, uint16_t& serverRtcpPort,
                       uint8_t& rtpChannel, uint8_t& rtcpChannel);
    // 从完整请求中提取body（按Content-Length）
    std::string extractBody(const std::string& request, const std::map<std::string, std::string>& headers);
    
private:
    std::weak_ptr<TcpConnection> _tcpConn;
    EventLoop* _loop;
    RtspState _state;
    std::string _clientIp;
    std::string _serverIp;
    RtspSession _session;
    std::string _sdp; // 客户端通过ANNOUNCE提供的SDP
    std::unique_ptr<RtpH264Unpacker> _RtpUnpacker;
    static SessionManager _sessionManager;
};

#endif
