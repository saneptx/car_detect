#include "MonitorServer.h"
#include "Logger.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <iostream>
#include "ikcp.h"  

int MonitorServer::_udpServerRtpFd = -1;
int MonitorServer::_udpServerRtcpFd = -1;
static inline uint32_t kcp_getu32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

MonitorServer &MonitorServer::instance() {
    static MonitorServer inst;
    return inst;
}
MonitorServer::~MonitorServer(){
    if(_udpServerRtpFd >= 0) ::close(_udpServerRtpFd);
    if(_udpServerRtcpFd >= 0) ::close(_udpServerRtcpFd);
    if(_epfd >= 0) ::close(_epfd);
    if(_listenFd >= 0) ::close(_listenFd);
}
MonitorServer::MonitorServer():_evtList(1024){
    createEpollFd();
    _udpServerRtpFd = socket(AF_INET,SOCK_DGRAM,0);//绑定RTP over UDP 端口发送
    struct sockaddr_in serverAddr1;
    serverAddr1.sin_family = AF_INET;
    serverAddr1.sin_port = htons(8910);
    inet_aton("0.0.0.0",&serverAddr1.sin_addr);
    ::bind(_udpServerRtpFd,(struct sockaddr *)&serverAddr1,sizeof(serverAddr1));

    _udpServerRtcpFd = socket(AF_INET,SOCK_DGRAM,0);//绑定RTCP over UDP 端口发送
    struct sockaddr_in serverAddr2;
    serverAddr2.sin_family = AF_INET;
    serverAddr2.sin_port = htons(8911);
    inet_aton("0.0.0.0",&serverAddr2.sin_addr);
    ::bind(_udpServerRtpFd,(struct sockaddr *)&serverAddr2,sizeof(serverAddr2));
}
void MonitorServer::start(const std::string &ip, unsigned short port) {
    if (_started) return;
    _started = true;
    _server = InetAddress(ip,port);
    std::thread([this, ip, port]() {
        acceptLoop();
    }).detach();
}

void MonitorServer::acceptLoop() {
    int listenfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        LOG_ERROR("MonitorServer socket create failed: %s", strerror(errno));
        return;
    }
    
    int on = 1;
    ::setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    if (::bind(listenfd, (sockaddr*)_server.getInetAddrPtr(), _server.getInetAddrLen()) < 0) {
        LOG_ERROR("MonitorServer bind failed on %s:%d: %s", _server.ip(), _server.port(), strerror(errno));
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
    LOG_INFO("MonitorServer listening on %s", _server.toString());
    addEpollReadFd(_listenFd);
    addEpollReadFd(_udpServerRtpFd);
    while (true) {
        int readyNum = epoll_wait(_epfd,&*_evtList.begin(),_evtList.size(),3000);
        if(-1 == readyNum){
            LOG_ERROR("epoll_wait failed: %s", strerror(errno));
            return; 
        }else if(0 == readyNum){
            LOG_DEBUG("MonitorServer epoll_wait timeout, thread id: %zu", std::hash<std::thread::id>{}(std::this_thread::get_id()));
        }else{
            for(int i = 0; i < readyNum; i++){
                int fd = _evtList[i].data.fd;
                if(fd == _listenFd){//qt客户端连接
                    sockaddr_in cliAddr;
                    socklen_t len = sizeof(cliAddr);
                    int connfd = ::accept(listenfd, (sockaddr*)&cliAddr, &len);
                    if (connfd < 0) {
                        LOG_ERROR("MonitorServer accept failed: %s", strerror(errno));
                        continue;
                    }
                    QtClient qtClient{};
                    qtClient.tcpFd = connfd;
                    qtClient._clientIp = inet_ntoa(cliAddr.sin_addr);
                    {
                        std::lock_guard<std::mutex> lock(_mtx);
                        _qtClients[connfd] = std::move(qtClient);
                    }
                    addEpollReadFd(connfd);
                    LOG_INFO("MonitorServer new client fd=%d", connfd);
                }else if(_qtClients.find(fd) != _qtClients.end()){
                    char buf[256];
                    std::string method;
                    std::string url;
                    std::map<std::string, std::string> headers;
                    std::string recvRequest;
                    int n = ::recv(fd,buf,sizeof(buf),0);
                    {
                        std::lock_guard<std::mutex> lock(_mtx);
                        if(n==0){
                            LOG_DEBUG("Qt Client closed!");
                            delEpollReadFd(fd);
                            for(auto &s : _qtClients[fd]._sessionMap){
                                s.second->kcp_runing = false;
                                if(s.second->_kcpThread.joinable()){
                                    s.second->_kcpThread.join();
                                }
                                if(s.second->ikcp){
                                    ikcp_release(s.second->ikcp);
                                    s.second->ikcp = nullptr;
                                }
                            }
                            _qtClients.erase(fd);
                            break;
                        }
                        _qtClients[fd]._buffer.append(buf,n);
                        size_t end = _qtClients[fd]._buffer.find("\r\n\r\n");
                        recvRequest = _qtClients[fd]._buffer.substr(0,end);
                        _qtClients[fd]._buffer.erase(0,end + 4);
                        parseRequest(recvRequest,method,url,headers);
                    }
                    LOG_DEBUG("Recv QTClient Request:\n%s",recvRequest.c_str());
                    _qtClients[fd].Cseq++;
                    if(method == "SETUP"){
                        std::map<std::string, std::string> extraHeaders{};
                        extraHeaders["CamNum"] = std::to_string(_cams.size());
                        int i=0;
                        for(auto e:_cams){
                            extraHeaders[std::to_string(i)]=e.first;
                            i++;
                        }    
                        sendRespond(extraHeaders, _qtClients[fd]);
                    } else if(method == "MESSAGE"){
                        std::lock_guard<std::mutex> lock(_mtx);
                        for(auto e:headers){
                            if(e.first=="cseq")
                                continue;
                            std::istringstream iss(e.second);
                            std::string port;
                            std::vector<std::string> ports;
                            while (std::getline(iss, port, ' ')) {
                                ports.push_back(port);
                            }
                            auto rtpAddr = InetAddress(_qtClients[fd]._clientIp, static_cast<unsigned short>(std::stoul(ports[0])));
                            auto rtcpAddr = InetAddress(_qtClients[fd]._clientIp, static_cast<unsigned short>(std::stoul(ports[1])));
                            _qtClients[fd]._sessionMap[e.first] = std::make_unique<udpSession>();
                            _qtClients[fd]._sessionMap[e.first]->_videoUdpRtp = std::move(rtpAddr);
                            _qtClients[fd]._sessionMap[e.first]->_videoUdpRtcp = std::move(rtcpAddr);
                            _qtClients[fd]._sessionMap[e.first]->conv = std::move(std::stoul(ports[2]));
                            _qtClients[fd]._sessionMap[e.first]->ikcp = kcp_init(_qtClients[fd]._sessionMap[e.first]->conv,
                                                                        &_qtClients[fd]._sessionMap[e.first]->_videoUdpRtp);
                            _qtClients[fd]._sessionMap[e.first]->kcp_runing = true;
                            _qtClients[fd]._sessionMap[e.first]->_kcpThread = std::thread([this,fd,e]{
                                kcp_update_thread(_qtClients[fd]._sessionMap[e.first].get());
                            });
                        }
                    } else if(method == "ADDCAM"){//收到qt端发送的添加端口信息，更新列表
                        LOG_DEBUG("Recieved Qt Client ADDCAM Respond");
                        std::lock_guard<std::mutex> lock(_mtx);
                        for(auto e:headers){
                            if(e.first=="cseq")
                                continue;
                            std::istringstream iss(e.second);
                            std::string port;
                            std::vector<std::string> ports;
                            while (std::getline(iss, port, ' ')) {
                                ports.push_back(port);
                            }
                            auto rtpAddr = InetAddress(_qtClients[fd]._clientIp, static_cast<unsigned short>(std::stoul(ports[0])));
                            auto rtcpAddr = InetAddress(_qtClients[fd]._clientIp, static_cast<unsigned short>(std::stoul(ports[1])));
                            _qtClients[fd]._sessionMap[e.first] = std::make_unique<udpSession>();
                            _qtClients[fd]._sessionMap[e.first]->_videoUdpRtp = std::move(rtpAddr);
                            _qtClients[fd]._sessionMap[e.first]->_videoUdpRtcp = std::move(rtcpAddr);
                            _qtClients[fd]._sessionMap[e.first]->conv = std::move(std::stoul(ports[2]));
                            _qtClients[fd]._sessionMap[e.first]->ikcp = kcp_init(_qtClients[fd]._sessionMap[e.first]->conv,
                                                                        &_qtClients[fd]._sessionMap[e.first]->_videoUdpRtp);
                            _qtClients[fd]._sessionMap[e.first]->kcp_runing = true;
                            _qtClients[fd]._sessionMap[e.first]->_kcpThread = std::thread([this,fd,e]{
                                kcp_update_thread(_qtClients[fd]._sessionMap[e.first].get());
                            });
                        }
                    }
                }else if(fd == _udpServerRtpFd){
                    char udp_buffer[2048]; 
                    ssize_t n;
                    // 循环读取所有待处理的 UDP 包
                    while (true) {  
                        n= ::recv(_udpServerRtpFd, udp_buffer, sizeof(udp_buffer), MSG_DONTWAIT);
                        uint32_t conv = kcp_getu32((const uint8_t*)udp_buffer);
                        ikcpcb* kcp = nullptr;
                        std::lock_guard<std::mutex> lock(_mtx);
                        for(const auto& qt : _qtClients){
                            for(const auto& s : qt.second._sessionMap){
                                if(s.second->conv == conv){
                                    kcp = s.second->ikcp;
                                }
                            }
                            if(kcp) break;
                        }
                        if (n > 0) {
                            // 正常接收数据，喂给 KCP
                            ikcp_input(kcp, udp_buffer, (int)n);
                            // LOG_DEBUG("Received UDP packet (len=%zd) and fed to KCP.", n);
                        } else if (n == -1) {
                            // 区分 EAGAIN（无数据）和真错误
                            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                // 无数据，退出循环（无需打印错误）
                                break;
                            } else {
                                // 真错误，打印并处理
                                LOG_ERROR("UDP recv error: %s", strerror(errno));
                                break;
                            }
                        } else { // n == 0，UDP 无连接，n=0 无意义
                            break;
                        }
                    }
                }
            }
        }
    }
}
/*
request:
    METHOD URL\r\n
    Cseq: value\r\n
    Header2: value\r\n
    \r\n
*/
void MonitorServer::parseRequest(const std::string& request, 
                    std::string& method, 
                    std::string& url,
                    std::map<std::string, std::string>& headers){
    // 解析请求行
    std::istringstream iss(request);
    std::string line;
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
            std::string value = line.substr(colonPos + 2);
            
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
/*
    respond:
        200 OK\r\n
        Cseq: value\r\n
        \r\n
*/
void MonitorServer::sendRespond(const std::map<std::string, std::string>& extraHeaders,const QtClient& client){
    std::ostringstream oss;
    int statusCode = 200;
    std::string statusText = "OK";
    // RTSP响应行
    oss << statusCode << " " << statusText << "\r\n";
    
    // CSeq头
    oss << "CSeq: " << client.Cseq << "\r\n";
    
    // 额外头部
    for (const auto& pair : extraHeaders) {
        oss << pair.first << ": " << pair.second << "\r\n";
    }
    
    // 结束标志
    oss << "\r\n";
    
    std::string response = oss.str();
    LOG_DEBUG("Sending RTSP response:\n%s", response.c_str());
    
    ::send(client.tcpFd,response.c_str(),response.size(),0);
    // if (auto tcpConn = _tcpConn.lock()) {
    //     tcpConn->send(response);
    // } else {
    //     LOG_WARN("TCP connection expired when sending RTSP response");
    // }
}

void MonitorServer::onNaluTcp(const std::string &streamName,
                           const uint8_t *data, size_t len) {
    if (streamName.empty() || !data || len == 0) return;
    std::lock_guard<std::mutex> lock(_mtx);
    if (_qtClients.empty()) return;
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
    for (auto &m : _qtClients) {
        ssize_t n = ::send(m.first, buf.data(), buf.size(), MSG_NOSIGNAL|MSG_DONTWAIT);
        // LOG_DEBUG("send %d seq, %d data",(data[6]<<8|data[7]),n);
        if (n < 0) {
            LOG_WARN("MonitorServer send failed on fd %d: %s", m.first, strerror(errno));
            badFds.insert(m.first);
        }
    }
    for (int fd : badFds) {
        ::close(fd);
        _qtClients.erase(fd);
    }
}

void MonitorServer::onNaluUdp(std::string sessionId,const char *data, size_t len){
    // for (auto &m : _qtClients) {
    //     ::sendto(_udpServerRtpFd,data,len,MSG_DONTWAIT,//非阻塞发送
    //         (struct sockaddr*)m.second._sessionMap[sessionId]._videoUdpRtp.getInetAddrPtr(),
    //         m.second._sessionMap[sessionId]._videoUdpRtp.getInetAddrLen());
    //     // LOG_DEBUG("Send to QtClient %d data",n);
    // }
    for (auto &m : _qtClients) {
        if(m.second._sessionMap.count(sessionId)){
            std::lock_guard<std::mutex> lock(m.second._sessionMap[sessionId]->_kcpMutex);
            ikcp_send(m.second._sessionMap[sessionId]->ikcp,data,len);
        }
    }
}


void MonitorServer::createEpollFd(){
    _epfd = ::epoll_create1(0);
    if(_epfd < 0){
        LOG_ERROR("MonitorServer epoll_create failed: %s", strerror(errno));
    }
    LOG_DEBUG("Created MonitorServer Epoll fd: %d", _epfd);
}

void MonitorServer::addEpollReadFd(int fd){
    struct  epoll_event evt;
    evt.events = EPOLLIN|EPOLLET;
    evt.data.fd = fd;
    
    int ret = ::epoll_ctl(_epfd,EPOLL_CTL_ADD,fd,&evt);
    if(ret < 0){
        LOG_ERROR("MonitorServerepoll_ctl_add failed for fd %d: %s", fd, strerror(errno));
        return;
    }
    LOG_INFO("MonitorServer AddEpollReadFd fd=%d, loop thread id=%lu", fd, std::this_thread::get_id());
    
}

void MonitorServer::delEpollReadFd(int fd){
    struct  epoll_event evt;
    evt.data.fd = fd;
    
    int ret = ::epoll_ctl(_epfd,EPOLL_CTL_DEL,fd,&evt);
    if(ret < 0){
        LOG_ERROR("MonitorServer Epoll_ctl_del failed for fd %d: %s", fd, strerror(errno));
        return;
    }
    LOG_DEBUG("MonitorServer Removed fd %d from epoll read events", fd);
}

void MonitorServer::addCam(std::string sessionId,std::string stringName){//向qt客户端发送新增摄像头消息
    std::lock_guard<std::mutex> lock(_mtx);
    _cams[sessionId] = stringName;
    if(_qtClients.empty()){
        return;
    }
    for(auto &qtc : _qtClients){
        if(!qtc.second._sessionMap.count(sessionId)){//qt端没有添加该摄像头信息
            std::string addCamReq = "ADDCAM " + _server.toString() + "\r\n"
                                    "Cseq: " + std::to_string(++qtc.second.Cseq) + "\r\n"
                                    "SessionId: " + sessionId + "\r\n\r\n";
            ::send(qtc.second.tcpFd,addCamReq.c_str(),addCamReq.size(),0);
            LOG_DEBUG("MonitorServer Send ADDCAM Request.");
        }
    }
}

void MonitorServer::kcp_update_thread(udpSession *session){
    while (session->kcp_runing) {
        {
            std::lock_guard<std::mutex> lock(session->_kcpMutex);
            if(session->ikcp){
                ikcp_update(session->ikcp, iclock());
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
int MonitorServer::kcp_output(const char *buf, int len, ikcpcb *kcp, void *user) {
    auto addr = (InetAddress*)(user);
    return ::sendto(_udpServerRtpFd,buf,len,MSG_DONTWAIT,(struct sockaddr*)addr->getInetAddrPtr(),addr->getInetAddrLen());
}

ikcpcb* MonitorServer::kcp_init(uint32_t conv,InetAddress *addr){
    ikcpcb* kcp = ikcp_create(conv,addr);
    kcp->output = kcp_output;
    ikcp_nodelay(kcp, 1, 10, 2, 0);
    ikcp_wndsize(kcp, 128, 128);
    ikcp_setmtu(kcp, 1450);
    return kcp;
}

void MonitorServer::removeCam(std::string sessionId){
    std::lock_guard<std::mutex> lock(_mtx);
    _cams.erase(sessionId);
    for(auto& qtc : _qtClients){
        if(qtc.second._sessionMap.count(sessionId)){//qt端有该摄像头信息
            std::string addCamReq = "DELCAM " + _server.toString() + "\r\n"
                                    "Cseq: " + std::to_string(qtc.second.Cseq) + "\r\n"
                                    "SessionId: " + sessionId + "\r\n\r\n";
            ::send(qtc.second.tcpFd,addCamReq.c_str(),addCamReq.size(),0);
            qtc.second._sessionMap[sessionId]->kcp_runing = false;
            if(qtc.second._sessionMap[sessionId]->_kcpThread.joinable()){
                qtc.second._sessionMap[sessionId]->_kcpThread.join();
            }
            if(qtc.second._sessionMap[sessionId]->ikcp){
                ikcp_release(qtc.second._sessionMap[sessionId]->ikcp);
                qtc.second._sessionMap[sessionId]->ikcp = nullptr;
            }
            qtc.second._sessionMap.erase(sessionId);
        }
    }
}
