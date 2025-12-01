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

MonitorServer &MonitorServer::instance() {
    static MonitorServer inst;
    return inst;
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
                        _qtClients[connfd] = qtClient;
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
                            std::vector<unsigned short> ports;
                            while (std::getline(iss, port, ' ')) {
                                ports.push_back(static_cast<unsigned short>(std::stoul(port)));
                            }
                            _qtClients[fd]._portMap[e.first]._videoUdpRtp = InetAddress(_qtClients[fd]._clientIp,ports[0]);
                            _qtClients[fd]._portMap[e.first]._videoUdpRtcp = InetAddress(_qtClients[fd]._clientIp,ports[1]);
                        }
                    } else if(method == "ADDCAM"){//收到qt端发送的添加端口信息，更新列表
                        LOG_DEBUG("Recieved Qt Client ADDCAM Respond");
                        std::lock_guard<std::mutex> lock(_mtx);
                        for(auto e:headers){
                            if(e.first=="cseq")
                                continue;
                            std::istringstream iss(e.second);
                            std::string port;
                            std::vector<unsigned short> ports;
                            while (std::getline(iss, port, ' ')) {
                                ports.push_back(static_cast<unsigned short>(std::stoul(port)));
                            }
                            _qtClients[fd]._portMap[e.first]._videoUdpRtp = InetAddress(_qtClients[fd]._clientIp,ports[0]);
                            _qtClients[fd]._portMap[e.first]._videoUdpRtcp = InetAddress(_qtClients[fd]._clientIp,ports[1]);
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
void MonitorServer::sendRespond(const std::map<std::string, std::string>& extraHeaders,QtClient client){
    std::ostringstream oss;
    int statusCode = 200;
    std::string statusText = "OK";
    // RTSP响应行
    oss << statusCode << " " << statusText << "\r\n";
    
    // CSeq头
    oss << "CSeq: " << ++client.Cseq << "\r\n";
    
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

void MonitorServer::onNaluUdp(std::string sessionId,const uint8_t *data, size_t len){
    for (auto &m : _qtClients) {
        ::sendto(_udpServerRtpFd,data,len,MSG_DONTWAIT,//非阻塞发送
            (struct sockaddr*)m.second._portMap[sessionId]._videoUdpRtp.getInetAddrPtr(),
            m.second._portMap[sessionId]._videoUdpRtp.getInetAddrLen());
        // LOG_DEBUG("Send to QtClient %d data",n);
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
    for(auto qtc : _qtClients){
        if(!qtc.second._portMap.count(sessionId)){//qt端没有添加该摄像头信息
            std::string addCamReq = "ADDCAM " + _server.toString() + "\r\n"
                                    "Cseq: " + std::to_string(++qtc.second.Cseq) + "\r\n"
                                    "SessionId: " + sessionId + "\r\n\r\n";
            ::send(qtc.second.tcpFd,addCamReq.c_str(),addCamReq.size(),0);
            LOG_DEBUG("MonitorServer Send ADDCAM Request.");
        }
    }
}

void MonitorServer::removeCam(std::string sessionId){
    std::lock_guard<std::mutex> lock(_mtx);
    _cams.erase(sessionId);
    for(auto qtc : _qtClients){
        if(qtc.second._portMap.count(sessionId)){//qt端有该摄像头信息
            std::string addCamReq = "DELCAM " + _server.toString() + "\r\n"
                                    "Cseq: " + std::to_string(qtc.second.Cseq) + "\r\n"
                                    "SessionId: " + sessionId + "\r\n\r\n";
            ::send(qtc.second.tcpFd,addCamReq.c_str(),addCamReq.size(),0);
            qtc.second._portMap.erase(sessionId);
        }
    }
}
