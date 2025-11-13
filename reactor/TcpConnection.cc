#include "TcpConnection.h"
#include <iostream>
#include <sstream>
#include <string.h>
#include "Logger.h"

using std::cout;
using std::endl;
using std::ostringstream;

static inline bool iequals_prefix(const std::string& s, const std::string& key) {
    if (s.size() < key.size()) return false;
    for (size_t i = 0; i < key.size(); ++i) {
        char a = (char)std::tolower((unsigned char)s[i]);
        char b = (char)std::tolower((unsigned char)key[i]);
        if (a != b) return false;
    }
    return true;
}

TcpConnection::TcpConnection(int fd,EventLoop* loop)
:_loop(loop)
,_sockIO(fd)
,_sock(fd)
,_localAddr(getLocalAddr())
,_peerAddr(getPeerAddr()){

    LOG_INFO("TcpConnection created - fd: %d, local: %s, peer: %s", 
             fd, _localAddr.toString().c_str(), _peerAddr.toString().c_str());
}

TcpConnection::~TcpConnection(){
    LOG_DEBUG("TcpConnection destructor - fd: %d", _sock.fd());
}

void TcpConnection::send(const string &msg){
    if (_sendBuffer.empty() && !_isWriting) {
        int written = _sockIO.writen(msg.c_str(), msg.size());
        if (written < (int)msg.size()) {
            // 没写完，缓存剩余部分
            _sendBuffer = msg.substr(written);
            _isWriting = true;
            _loop->addEpollWriteFd(getFd());
            // LOG_DEBUG("Partial write for fd %d: %d/%zu bytes, buffering remaining", 
            //          getFd(), written, msg.size());
        } else {
            // LOG_DEBUG("Complete write for fd %d: %zu bytes", getFd(), msg.size());
        }
    } else {
        // 缓冲区有数据，直接追加
        _sendBuffer += msg;
        if (!_isWriting) {
            _isWriting = true;
            _loop->addEpollWriteFd(getFd());
        }
        LOG_DEBUG("Appended to send buffer for fd %d: %zu bytes, total buffered: %zu", 
                 getFd(), msg.size(), _sendBuffer.size());
    }
}

void TcpConnection::sendInLoop(const string &msg){
    if(_loop){
        auto self = shared_from_this();
        _loop->runInLoop([self, msg](){
            self->send(msg);
        });
        // LOG_DEBUG("Queued sendInLoop for fd %d: %zu bytes", getFd(), msg.size());
    } else {
        LOG_ERROR("No loop available for sendInLoop on fd %d", getFd());
    }
}

string TcpConnection::recive(){
    char buff[1024*1024] = {0};
    _sockIO.readLine(buff,sizeof(buff));

    return string(buff);
}



std::string TcpConnection::reciveRtspRequest() {
    char temp[4096];
    int n = ::recv(_sock.fd(), temp, sizeof(temp), 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return "";
        LOG_ERROR("Error reading from fd %d: %s", getFd(), strerror(errno));
        return "";
    } else if (n == 0) {
        LOG_DEBUG("Connection closed by peer on fd %d", getFd());
        return "";
    }

    _recvBuffer.append(temp, n);

    // 尝试解析多条请求（可能批量到达）
    while (true) {
        // 找header结束位置
        size_t headerEnd = _recvBuffer.find("\r\n\r\n");
        if (headerEnd == std::string::npos)
            break;  // header还没收完，等待下次数据

        // 提取header
        std::string headerPart = _recvBuffer.substr(0, headerEnd + 4);

        // 查找 Content-Length
        size_t lenPos = headerPart.find("Content-Length:");
        size_t contentLen = 0;
        if (lenPos != std::string::npos) {
            lenPos += 15; // 跳过"Content-Length:"
            while (lenPos < headerPart.size() && isspace(headerPart[lenPos]))
                lenPos++;
            contentLen = std::stoul(headerPart.substr(lenPos));
        }

        // 计算完整包长度 = header + body
        size_t totalLen = headerEnd + 4 + contentLen;
        if (_recvBuffer.size() < totalLen)
            break;  // body还没收完，继续等

        // ✅ 提取完整请求
        std::string oneRequest = _recvBuffer.substr(0, totalLen);
        _recvBuffer.erase(0, totalLen);
        return oneRequest; // 返回一条完整请求
    }

    return "";
}


string TcpConnection::toString(){
    ostringstream oss;
    oss << _localAddr.toString() << " <-- " << _peerAddr.toString();
    return oss.str();
}

//获取本端网络地址信息
InetAddress TcpConnection::getLocalAddr(){
    struct sockaddr_in addr;
    socklen_t len = sizeof(struct sockaddr );
    //获取本端地址的函数getsockname
    int ret = getsockname(_sock.fd(), (struct sockaddr *)&addr, &len);
    if(-1 == ret)
    {
        LOG_ERROR("getsockname failed for fd %d: %s", _sock.fd(), strerror(errno));
    }

    return InetAddress(addr);
}

//获取对端的网络地址信息
InetAddress TcpConnection::getPeerAddr(){
    struct sockaddr_in addr;
    socklen_t len = sizeof(struct sockaddr );
    //获取对端地址的函数getpeername
    int ret = getpeername(_sock.fd(), (struct sockaddr *)&addr, &len);
    if(-1 == ret)
    {
        LOG_ERROR("getpeername failed for fd %d: %s", _sock.fd(), strerror(errno));
    }

    return InetAddress(addr);
}

bool TcpConnection::isClosed() const{
    char buf[10]={0};
    int ret = ::recv(_sock.fd(),buf,sizeof(buf),MSG_PEEK);
    
    return (0 == ret);
}

void TcpConnection::setRtspConnect(const std::shared_ptr<RtspConnect>& conn) { 
    _rtspConn = conn; 
    LOG_DEBUG("RTSP connection set for fd %d", getFd());
}
std::shared_ptr<RtspConnect> TcpConnection::getRtspConnect() { 
    return _rtspConn; 
}

// void TcpConnection::setNewConnectionCallback(const TcpConnectionCallback &cb){
//     _onNewConnectionCb = cb;
//     LOG_DEBUG("New connection callback set for fd %d", getFd());
// }
void TcpConnection::setMessageCallback(const TcpConnectionCallback &cb){
    _onMessageCb = cb;
    LOG_DEBUG("Message callback set for fd %d", getFd());
}
void TcpConnection::setCloseCallback(const TcpConnectionCallback &cb){
    _onCloseCb = cb;
    LOG_DEBUG("Close callback set for fd %d", getFd());
}


// void TcpConnection::handleNewConnectionCallback(){
//     if(_onNewConnectionCb){
//         LOG_DEBUG("Executing new connection callback for fd %d", getFd());
//         _onNewConnectionCb(shared_from_this());
//     }else{
//         LOG_WARN("New connection callback is null for fd %d", getFd());
//     }
// }
void TcpConnection::handleMessageCallback(){
    if(_onMessageCb){
        LOG_DEBUG("Executing message callback for fd %d", getFd());
        _onMessageCb(shared_from_this());
    }else{
        LOG_WARN("Message callback is null for fd %d", getFd());
    }
}
void TcpConnection::handleCloseCallback(){
    if(_onCloseCb){
        LOG_DEBUG("Executing close callback for fd %d", getFd());
        _onCloseCb(shared_from_this());
    }else{
        LOG_WARN("Close callback is null for fd %d", getFd());
    }
}

TimerId TcpConnection::addOneTimer(int delaySec, TimerCallback &&cb){
    LOG_DEBUG("Adding one-shot timer for fd %d: %d seconds", getFd(), delaySec);
    return _loop->addOneTimer(delaySec, std::move(cb));
}
TimerId TcpConnection::addPeriodicTimer(int delaySec, int intervalSec, TimerCallback &&cb){
    LOG_DEBUG("Adding periodic timer for fd %d: delay=%d, interval=%d", getFd(), delaySec, intervalSec);
    return _loop->addPeriodicTimer(delaySec, intervalSec, std::move(cb));
}
void TcpConnection::removeTimer(TimerId timerId){
    LOG_DEBUG("Removing timer %lu for fd %d", timerId, getFd());
    _loop->removeTimer(timerId);
}

void TcpConnection::handleWriteCallback() {
    if (_sendBuffer.empty()) {
        _isWriting = false;
        _loop->delEpollWriteFd(getFd());
        LOG_DEBUG("Write buffer empty for fd %d, removed write event", getFd());
        return;
    }
    int written = _sockIO.writen(_sendBuffer.c_str(), _sendBuffer.size());
    if (written < 0) {
        // 错误，关闭连接
        LOG_ERROR("Write error for fd %d: %s", getFd(), strerror(errno));
        handleCloseCallback();
        return;
    }
    _sendBuffer.erase(0, written);
    LOG_DEBUG("Wrote %d bytes from buffer for fd %d, remaining: %zu", 
             written, getFd(), _sendBuffer.size());
    if (_sendBuffer.empty()) {
        _isWriting = false;
        _loop->delEpollWriteFd(getFd());
        LOG_DEBUG("Write buffer completely sent for fd %d, removed write event", getFd());
    }
}