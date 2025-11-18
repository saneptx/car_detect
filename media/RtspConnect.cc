#include "RtspConnect.h"
#include "TcpConnection.h"
#include "UdpConnection.h"
#include "Logger.h"
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include "RtpH264Unpacker.h"
#include "MonitorServer.h"

SessionManager RtspConnect::_sessionManager;

RtspConnect::RtspConnect(std::shared_ptr<TcpConnection> tcpConn, EventLoop* loop)
    : _tcpConn(tcpConn)
    , _loop(loop)
    , _state(RtspState::INIT)
    , _clientIp(tcpConn->getPeerAddr().ip())
    , _serverIp(tcpConn->getLocalAddr().ip()){

    _session.sessionId = _sessionManager.generateSessionId();
    _session.lastActive = std::chrono::steady_clock::now();
    _sessionManager.addSession(_session);
    _RtpUnpacker = std::make_unique<RtpH264Unpacker>();
    // 默认流名称先用会话ID，可在解析 URL 后覆盖
    _session.setStreamName(_session.sessionId);
    // 将解出的 H264 NALU 转发给监控服务器（供 Qt 客户端使用）
    _RtpUnpacker->setNaluCallback([this](const uint8_t *data, size_t len, uint32_t ts) {
        MonitorServer::instance().onNalu(_session.getStreamName(), data, len, ts);
    });
    LOG_INFO("RtspConnect created - Session: %s, Client: %s", 
             _session.sessionId.c_str(), _clientIp.c_str());
}

RtspConnect::~RtspConnect() {
    LOG_INFO("RtspConnect destroyed - Session: %s", _session.sessionId.c_str());
}

void RtspConnect::handleRequest(const std::string& request) {
    LOG_DEBUG("Handling RTSP request:\n%s", request.c_str());
    
    std::string method;
    std::string url;
    std::map<std::string, std::string> headers;
    
    parseRequest(request, method, url, headers);
    
    int cseq = extractCSeq(headers);
    
    // 根据方法分发处理
    if (method == "OPTIONS"){
        handleOpitions(request, headers, cseq);
    }else if (method == "ANNOUNCE") {
        handleAnnounce(request, headers, cseq);
    } else if (method == "SETUP") {
        handleSetup(url, headers, cseq);
    } else if (method == "RECORD") {
        handleRecord(url, headers, cseq);
    } else if (method == "TEARDOWN"){
        handleTeardown(url, headers, cseq);
    } else {
        LOG_WARN("Unknown RTSP method: %s", method.c_str());
        std::map<std::string, std::string> extraHeaders;
        sendResponse(501, "Not Implemented", extraHeaders, cseq);
    }
}

void RtspConnect::parseRequest(const std::string& request, 
                                std::string& method, 
                                std::string& url,
                                std::map<std::string, std::string>& headers) {
    std::istringstream iss(request);
    std::string line;
    
    // 解析请求行
    if (std::getline(iss, line)) {
        std::istringstream requestLine(line);
        requestLine >> method >> url;
        // 去除RTSP/1.0部分
    }
    
    // 解析头部
    while (std::getline(iss, line)) {
        if (line.empty() || line == "\r") break;
        
        size_t colonPos = line.find(':');
        if (colonPos != std::string::npos) {
            std::string key = line.substr(0, colonPos);
            std::string value = line.substr(colonPos + 1);
            
            // 去除前后空格
            key.erase(0, key.find_first_not_of(" \t\r\n"));
            key.erase(key.find_last_not_of(" \t\r\n") + 1);
            value.erase(0, value.find_first_not_of(" \t\r\n"));
            value.erase(value.find_last_not_of(" \t\r\n") + 1);
            
            // 转换为小写以便查找
            std::string keyLower = key;
            std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(), ::tolower);
            headers[keyLower] = value;
        }
    }
}

int RtspConnect::extractCSeq(const std::map<std::string, std::string>& headers) {
    auto it = headers.find("cseq");
    if (it != headers.end()) {
        return std::atoi(it->second.c_str());
    }
    return 0;
}

void RtspConnect::handleOpitions(const std::string& request,
    const std::map<std::string, std::string>& headers, int cseq) {
    LOG_INFO("Handling OPITIONS request, CSeq: %d", cseq);
    std::map<std::string, std::string> extraHeaders;
    extraHeaders["Public"] = "OPTIONS, ANNOUNCE, SETUP, TEARDOWN, RECORD";
    sendResponse(200, "OK", extraHeaders, cseq);
}

void RtspConnect::handleAnnounce(const std::string& request,
                                 const std::map<std::string, std::string>& headers, int cseq) {
    LOG_INFO("Handling ANNOUNCE request, CSeq: %d", cseq);
    // 读取SDP body
    _sdp = extractBody(request, headers);
    if (_sdp.empty()) {
        LOG_WARN("ANNOUNCE without SDP body or Content-Length=0");
    } else {
        LOG_DEBUG("Received SDP via ANNOUNCE:\n%s", _sdp.c_str());
    }
    // 从 ANNOUNCE 的第一行里提取 URL，设置流名称（已弃用）
    // {
    //     std::istringstream iss(request);
    //     std::string line;
    //     if (std::getline(iss, line)) {
    //         std::istringstream rl(line);
    //         std::string method, url, version;
    //         rl >> method >> url >> version;
    //         if (!url.empty()) {
    //             // 提取路径最后一段作为 streamName
    //             std::string path = url;
    //             auto pos = path.find("://");
    //             if (pos != std::string::npos) {
    //                 pos = path.find('/', pos + 3);
    //                 if (pos != std::string::npos) {
    //                     path = path.substr(pos + 1);
    //                 }
    //             } else if (!path.empty() && path[0] == '/') {
    //                 path = path.substr(1);
    //             }
    //             // 去掉 query
    //             auto qpos = path.find('?');
    //             if (qpos != std::string::npos) {
    //                 path = path.substr(0, qpos);
    //             }
    //             if (!path.empty()) {
    //                 _session.setStreamName(path);
    //                 LOG_INFO("Set stream name to '%s' for session %s",
    //                          path.c_str(), _session.sessionId.c_str());
    //             }
    //         }
    //     }
    // }

    std::map<std::string, std::string> extraHeaders;
    sendResponse(200, "OK", extraHeaders, cseq);
}

void RtspConnect::handleSetup(const std::string& url, 
                              const std::map<std::string, std::string>& headers, int cseq) {
    LOG_INFO("Handling SETUP request, URL: %s, CSeq: %d", url.c_str(), cseq);
    
    // 从Transport头中解析传输方式和端口
    auto transportIt = headers.find("transport");
    if (transportIt == headers.end()) {
        LOG_ERROR("SETUP request missing Transport header");
        std::map<std::string, std::string> extraHeaders;
        sendResponse(400, "Bad Request", extraHeaders, cseq);
        return;
    }
    
    std::string transportType;
    uint16_t clientRtpPort = 0, clientRtcpPort = 0;
    uint16_t serverRtpPort = 0, serverRtcpPort = 0;
    uint8_t rtpChannel = 0, rtcpChannel = 0;
    
    if (!parseTransport(transportIt->second, transportType, 
                       clientRtpPort, clientRtcpPort,
                       serverRtpPort, serverRtcpPort,
                       rtpChannel, rtcpChannel)) {
        LOG_ERROR("Failed to parse Transport header: %s", transportIt->second.c_str());
        std::map<std::string, std::string> extraHeaders;
        sendResponse(400, "Bad Request", extraHeaders, cseq);
        return;
    }
    _session.transportType = transportType;
    LOG_INFO("Transport type: %s", transportType.c_str());

    std::map<std::string, std::string> extraHeaders;
    std::ostringstream transportOss;
    if (transportType == "UDP") {
        // UDP模式处理
        //判断是否是视频
        if (url.find("trackID=0") != std::string::npos) {    
            if (serverRtpPort == 0) {//客户端未指定服务器端口
                serverRtpPort = _sessionManager.allocateUdpPorts();
                serverRtcpPort = serverRtpPort + 1;
            }
            _session.videoRtpConn = std::make_shared<UdpConnection>(_serverIp,serverRtpPort
                ,InetAddress(_clientIp,clientRtpPort),_loop);
            // _session.videoRtcpConn = std::make_shared<UdpConnection>(_serverIp,serverRtcpPort
            //     ,InetAddress(_clientIp,clientRtcpPort),_loop);
            _loop->addUdpConnection(_session.videoRtpConn);
            // _loop->addUdpConnection(_session.videoRtcpConn);
            _session.videoRtpConn->setMessageCallback([this](const UdpConnectionPtr &conn){
                uint8_t buffer[1500];
                while (true) {
                    int n = conn->recv(buffer, sizeof(buffer));
                    if (n <= 0) break;
                    _RtpUnpacker->handleRtpPacket(buffer,n);
                }
            });
        }else if(url.find("trackID=1") != std::string::npos){   
            if (serverRtpPort == 0) {
                serverRtpPort = _sessionManager.allocateUdpPorts() + 2;
                serverRtcpPort = serverRtpPort + 1;
            }
            _session.audioRtpConn = std::make_shared<UdpConnection>(_serverIp,serverRtpPort
                ,InetAddress(_clientIp,clientRtpPort),_loop);
            // _session.audioRtcpConn = std::make_shared<UdpConnection>(_serverIp,serverRtcpPort
            //     ,InetAddress(_clientIp,clientRtcpPort),_loop);
            _loop->addUdpConnection(_session.audioRtpConn);
            // _loop->addUdpConnection(_session.audioRtcpConn);
        }
        LOG_INFO("UDP mode - Client RTP ports: %d-%d, Server RTP ports: %d-%d", 
                 clientRtpPort, clientRtcpPort, serverRtpPort, serverRtcpPort);
        // 构建Transport响应
        transportOss << "RTP/AVP/UDP;unicast;client_port=" << clientRtpPort 
                     << "-" << clientRtcpPort 
                     << ";server_port=" << serverRtpPort 
                     << "-" << serverRtcpPort;
                     
    } else if (transportType == "TCP") {
        // TCP模式处理（interleaved）
        // TCP模式下，RTP/RTCP数据通过RTSP TCP连接传输
        // interleaved通道号用于区分RTP和RTCP数据包
        
        // 如果客户端没有指定interleaved通道，使用默认值
        if (rtpChannel == 0 && rtcpChannel == 0) {
            rtpChannel = 0;
            rtcpChannel = 1;
        }
        
        
        LOG_INFO("TCP mode - RTP channel: %d, RTCP channel: %d", 
                 rtpChannel, rtcpChannel);
        
        // 构建Transport响应
        transportOss << "RTP/AVP/TCP;unicast;interleaved=" 
                     << static_cast<int>(rtpChannel) 
                     << "-" << static_cast<int>(rtcpChannel);
    } else {
        LOG_ERROR("Unsupported transport type: %s", transportType.c_str());
        std::map<std::string, std::string> extraHeaders;
        sendResponse(461, "Unsupported Transport", extraHeaders, cseq);
        return;
    }
    
    _state = RtspState::READY;
    
    extraHeaders["Transport"] = transportOss.str();
    extraHeaders["Session"] = _session.sessionId + ";timeout=60";
    
    sendResponse(200, "OK", extraHeaders, cseq);
}


void RtspConnect::handleRecord(const std::string& url,
                               const std::map<std::string, std::string>& headers, int cseq) {
    LOG_INFO("Handling RECORD request, URL: %s, CSeq: %d", url.c_str(), cseq);

    // 检查Session ID
    auto sessionIt = headers.find("session");
    if (sessionIt == headers.end() || sessionIt->second.find(_session.sessionId) == std::string::npos) {
        LOG_WARN("RECORD request with invalid Session ID");
        std::map<std::string, std::string> extraHeaders;
        sendResponse(454, "Session Not Found", extraHeaders, cseq);
        return;
    }

    if (_state != RtspState::READY) {
        LOG_WARN("RECORD request in wrong state: %d", (int)_state);
        std::map<std::string, std::string> extraHeaders;
        sendResponse(455, "Method Not Valid in This State", extraHeaders, cseq);
        return;
    }

    _state = RtspState::PLAYING; // 标记为接收中

    std::map<std::string, std::string> extraHeaders;
    extraHeaders["Session"] = _session.sessionId;
    sendResponse(200, "OK", extraHeaders, cseq);
}

void RtspConnect::handleTeardown(const std::string& url,
                    const std::map<std::string, std::string>& headers, int cseq){
    LOG_INFO("Handling TEARDOWN request, URL: %s, CSeq: %d", url.c_str(), cseq);
    auto sessionIt = headers.find("session");
    if (sessionIt == headers.end() || sessionIt->second.find(_session.sessionId) == std::string::npos) {
        LOG_WARN("RECORD request with invalid Session ID");
        std::map<std::string, std::string> extraHeaders;
        sendResponse(454, "Session Not Found", extraHeaders, cseq);
        return;
    }
    releaseSession();
    
    _RtpUnpacker->flush();
    std::map<std::string, std::string> extraHeaders;
    sendResponse(200, "OK", extraHeaders, cseq);
}

void RtspConnect::sendResponse(int statusCode, const std::string& statusText,
                               const std::map<std::string, std::string>& extraHeaders, int cseq) {
    std::ostringstream oss;
    
    // RTSP响应行
    oss << "RTSP/1.0 " << statusCode << " " << statusText << "\r\n";
    
    // CSeq头
    oss << "CSeq: " << cseq << "\r\n";
    
    // Server头
    oss << "Server: RTSP Server/1.0\r\n";
    
    // 额外头部
    for (const auto& pair : extraHeaders) {
        oss << pair.first << ": " << pair.second << "\r\n";
    }
    
    // 结束标志
    oss << "\r\n";
    
    std::string response = oss.str();
    LOG_DEBUG("Sending RTSP response:\n%s", response.c_str());
    
    if (auto tcpConn = _tcpConn.lock()) {
        tcpConn->send(response);
    } else {
        LOG_WARN("TCP connection expired when sending RTSP response");
    }
}

void RtspConnect::sendResponse(int statusCode, const std::string& statusText,
                               const std::map<std::string, std::string>& extraHeaders, int cseq,
                               const std::string& body) {
    std::ostringstream oss;
    
    // RTSP响应行
    oss << "RTSP/1.0 " << statusCode << " " << statusText << "\r\n";
    
    // CSeq头
    oss << "CSeq: " << cseq << "\r\n";
    
    // Server头
    oss << "Server: RTSP Server/1.0\r\n";
    
    // 额外头部（复制一遍，并确保有Content-Length）
    bool hasContentLength = false;
    for (const auto& pair : extraHeaders) {
        if (pair.first == "Content-Length" || pair.first == "content-length") {
            hasContentLength = true;
        }
        oss << pair.first << ": " << pair.second << "\r\n";
    }
    if (!hasContentLength) {
        oss << "Content-Length: " << body.size() << "\r\n";
    }
    
    // 结束头部
    oss << "\r\n";
    
    // 拼接body
    oss << body;
    
    std::string response = oss.str();
    LOG_DEBUG("Sending RTSP response with body (len=%zu):\n%s", response.size(), response.c_str());
    
    if (auto tcpConn = _tcpConn.lock()) {
        tcpConn->send(response);
    } else {
        LOG_WARN("TCP connection expired when sending RTSP response with body");
    }
}

void RtspConnect::onInterleavedFrame(uint8_t ch, const uint8_t* data, size_t len) {
    if ((ch % 2) == 0) {
        //RTP
        _RtpUnpacker->handleRtpPacket(data,len);
    } else {
        // RTCP
        
    }
}

bool RtspConnect::parseTransport(const std::string& transport, 
                                 std::string& transportType,
                                 uint16_t& clientRtpPort, uint16_t& clientRtcpPort,
                                 uint16_t& serverRtpPort, uint16_t& serverRtcpPort,
                                 uint8_t& rtpChannel, uint8_t& rtcpChannel) {
    // 初始化输出参数
    transportType = "";
    clientRtpPort = 0;
    clientRtcpPort = 0;
    serverRtpPort = 0;
    serverRtcpPort = 0;
    rtpChannel = 0;
    rtcpChannel = 0;
    
    // 判断传输类型: RTP/AVP/UDP 或 RTP/AVP/TCP
    if (transport.find("RTP/AVP/UDP") != std::string::npos || 
        transport.find("RTP/AVP;UDP") != std::string::npos) {
        transportType = "UDP";
        
        // 解析UDP模式: client_port=5004-5005;server_port=6000-6001
        size_t pos = transport.find("client_port=");
        if (pos != std::string::npos) {
            pos += 12;  // "client_port="长度
            size_t dashPos = transport.find("-", pos);
            if (dashPos != std::string::npos) {
                std::string rtpPortStr = transport.substr(pos, dashPos - pos);
                std::string rtcpPortStr = transport.substr(dashPos + 1);
                
                // 去除可能的分号或空格
                size_t semicolonPos = rtcpPortStr.find(";");
                if (semicolonPos != std::string::npos) {
                    rtcpPortStr = rtcpPortStr.substr(0, semicolonPos);
                }
                // 去除空格
                rtpPortStr.erase(0, rtpPortStr.find_first_not_of(" \t"));
                rtpPortStr.erase(rtpPortStr.find_last_not_of(" \t") + 1);
                rtcpPortStr.erase(0, rtcpPortStr.find_first_not_of(" \t"));
                rtcpPortStr.erase(rtcpPortStr.find_last_not_of(" \t") + 1);
                
                clientRtpPort = static_cast<uint16_t>(std::atoi(rtpPortStr.c_str()));
                clientRtcpPort = static_cast<uint16_t>(std::atoi(rtcpPortStr.c_str()));
            }
        }
        
        // 解析server_port (可选)
        pos = transport.find("server_port=");
        if (pos != std::string::npos) {
            pos += 12;  // "server_port="长度
            size_t dashPos = transport.find("-", pos);
            if (dashPos != std::string::npos) {
                std::string rtpPortStr = transport.substr(pos, dashPos - pos);
                std::string rtcpPortStr = transport.substr(dashPos + 1);
                
                // 去除可能的分号或空格
                size_t semicolonPos = rtcpPortStr.find(";");
                if (semicolonPos != std::string::npos) {
                    rtcpPortStr = rtcpPortStr.substr(0, semicolonPos);
                }
                // 去除空格
                rtpPortStr.erase(0, rtpPortStr.find_first_not_of(" \t"));
                rtpPortStr.erase(rtpPortStr.find_last_not_of(" \t") + 1);
                rtcpPortStr.erase(0, rtcpPortStr.find_first_not_of(" \t"));
                rtcpPortStr.erase(rtcpPortStr.find_last_not_of(" \t") + 1);
                
                serverRtpPort = static_cast<uint16_t>(std::atoi(rtpPortStr.c_str()));
                serverRtcpPort = static_cast<uint16_t>(std::atoi(rtcpPortStr.c_str()));
            }
        }
        
        return (clientRtpPort > 0 && clientRtcpPort > 0);
        
    } else if (transport.find("RTP/AVP/TCP") != std::string::npos ||
               transport.find("RTP/AVP;TCP") != std::string::npos) {
        transportType = "TCP";
        
        // 解析TCP模式: interleaved=0-1
        size_t pos = transport.find("interleaved=");
        if (pos != std::string::npos) {
            pos += 12;  // "interleaved="长度
            size_t dashPos = transport.find("-", pos);
            if (dashPos != std::string::npos) {
                std::string rtpChStr = transport.substr(pos, dashPos - pos);
                std::string rtcpChStr = transport.substr(dashPos + 1);
                
                // 去除可能的分号或空格
                size_t semicolonPos = rtcpChStr.find(";");
                if (semicolonPos != std::string::npos) {
                    rtcpChStr = rtcpChStr.substr(0, semicolonPos);
                }
                // 去除空格
                rtpChStr.erase(0, rtpChStr.find_first_not_of(" \t"));
                rtpChStr.erase(rtpChStr.find_last_not_of(" \t") + 1);
                rtcpChStr.erase(0, rtcpChStr.find_first_not_of(" \t"));
                rtcpChStr.erase(rtcpChStr.find_last_not_of(" \t") + 1);
                
                rtpChannel = static_cast<uint8_t>(std::atoi(rtpChStr.c_str()));
                rtcpChannel = static_cast<uint8_t>(std::atoi(rtcpChStr.c_str()));
            }
        }
        
        return true;
    }
    
    return false;  // 未识别传输类型
}

std::string RtspConnect::extractBody(const std::string& request,
                                     const std::map<std::string, std::string>& headers) {
    size_t pos = request.find("\r\n\r\n");
    if (pos == std::string::npos) return std::string();
    pos += 4;
    size_t contentLength = 0;
    auto it = headers.find("content-length");
    if (it != headers.end()) {
        contentLength = static_cast<size_t>(std::atoi(it->second.c_str()));
    }
    if (contentLength == 0) {
        // 若未知长度，返回剩余全部作为尝试
        return request.substr(pos);
    }
    if (pos + contentLength > request.size()) {
        // 不完整
        return std::string();
    }
    return request.substr(pos, contentLength);
}

void RtspConnect::releaseSession(){
    if(_session.videoRtpConn != nullptr){
        _loop->removeUdpConnection(_session.videoRtpConn);
    }
    if(_session.videoRtcpConn != nullptr){
        _loop->removeUdpConnection(_session.videoRtcpConn);
    }
    if(_session.audioRtpConn != nullptr){
        _loop->removeUdpConnection(_session.audioRtpConn);
    }
    if(_session.audioRtcpConn != nullptr){
        _loop->removeUdpConnection(_session.audioRtcpConn);
    }
    // if (_recorder) {
    //     _recorder->close();
    //     _recorder.reset();
    // }
    _sessionManager.removeSession(_session.sessionId);
}
