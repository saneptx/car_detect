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

TcpConnection::TcpConnection(EventLoop* loop, int fd)
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


// bool TcpConnection::tryExtractInterleaved(InterleavedFrame& out) {
    
//     if (_recvBuffer.empty() || _recvBuffer[0] != '$') return false;
//     if (_recvBuffer.size() < 4) return false; // 还没到 4 字节头

//     const uint8_t* p = reinterpret_cast<const uint8_t*>(_recvBuffer.data());
//     uint8_t channel = p[1];
//     uint16_t len = (uint16_t(p[2]) << 8) | uint16_t(p[3]);

//     // 可选：简单风控，防止异常巨长
//     if (len > 15000) { // 你也可以换成更合理的上限
//         LOG_WARN("Interleaved frame length too large: %u, dropping", (unsigned)len);
//         // 丢弃 '$' 以避免死循环
//         _recvBuffer.erase(0, 1);
//         return false;
//     }
//     if (_recvBuffer.size() < 4u + len) return false; // 体还没收全

//     out.channel = channel;
//     out.payload.assign(_recvBuffer.data() + 4, len);
//     _recvBuffer.erase(0, 4u + len);
//     return true;
// }

bool TcpConnection::tryExtractInterleaved(InterleavedFrame& out) {
    
    if (_recvBuffer.empty() || _recvBuffer[0] != '$') return false;
    if (_recvBuffer.size() < 4) return false; // 还没到 4 字节头

    const uint8_t* p = reinterpret_cast<const uint8_t*>(_recvBuffer.data());
    uint8_t channel = p[1];
    uint16_t len = (uint16_t(p[2]) << 8) | uint16_t(p[3]);

    // 可选：简单风控，防止异常巨长
    if (len > 15000) { // 你也可以换成更合理的上限
        LOG_WARN("Interleaved frame length too large: %u, dropping", (unsigned)len);
        // 丢弃 '$' 以避免死循环
        _recvBuffer.erase(0, 1);
        return false;
    }
    if (_recvBuffer.size() < 4u + len) return false; // 体还没收全

    out.channel = channel;
    out.payload.assign(_recvBuffer.data(), len + 4);
    _recvBuffer.erase(0, 4u + len);
    return true;
}

bool TcpConnection::tryExtractRtsp(std::string& out) {
    // 找 header 结束
    size_t headerEnd = _recvBuffer.find("\r\n\r\n");
    if (headerEnd == std::string::npos) return false;

    const std::string header = _recvBuffer.substr(0, headerEnd + 4);

    // 解析 Content-Length（大小写不敏感，只取该行第一个整数）
    size_t contentLength = 0;
    {
        // 粗暴逐行找
        size_t pos = 0;
        while (pos < header.size()) {
            size_t eol = header.find("\r\n", pos);
            std::string line = header.substr(pos, (eol == std::string::npos ? header.size() : eol) - pos);
            // 去掉前导空白
            size_t i = 0; while (i < line.size() && std::isspace((unsigned char)line[i])) ++i;
            std::string ltrim = line.substr(i);

            if (iequals_prefix(ltrim, "content-length:")) {
                size_t j = std::string("content-length:").size();
                // 跳空白
                while (j < ltrim.size() && std::isspace((unsigned char)ltrim[j])) ++j;
                // 读取数字
                size_t k = j;
                while (k < ltrim.size() && std::isdigit((unsigned char)ltrim[k])) ++k;
                try {
                    if (k > j) contentLength = static_cast<size_t>(std::stoul(ltrim.substr(j, k - j)));
                } catch (...) {
                    contentLength = 0;
                }
                break;
            }
            if (eol == std::string::npos) break;
            pos = eol + 2;
        }
    }
    size_t tpos = _recvBuffer.rfind("TEARDOWN");
    if(tpos != std::string::npos){//找到了TEARDOWN
        size_t total =  headerEnd - tpos + 4;
        out.assign(_recvBuffer.data() + tpos,total);
        _recvBuffer.erase();
    }else{
        size_t total = headerEnd + 4 + contentLength;
        if (_recvBuffer.size() < total) return false; // body 未收全
        out.assign(_recvBuffer.data(), total);
        _recvBuffer.erase(0, total);
    }       
    return true;
}

RecvItem TcpConnection::recvOneItem() {
    char temp[8192];
    int n = ::recv(_sock.fd(), temp, sizeof(temp), 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return {}; // 没有数据
        LOG_ERROR("Error reading from fd %d: %s", getFd(), strerror(errno));
        return {};
    } else if (n == 0) {
        LOG_DEBUG("Peer closed fd %d", getFd());
        return {};//连接关闭
    }
    _recvBuffer.append(temp, n);
    std::string req;
    InterleavedFrame fr;
    if(tryExtractRtsp(req)){
        RecvItem item;
        item.type = RecvItemType::RtspRequest;
        item.rtsp = std::move(req);
        return item;
    }else if (tryExtractInterleaved(fr)) {
        RecvItem item;
        item.type = RecvItemType::InterleavedFrame;
        item.frame = std::move(fr);
        return item;
    }else{
        std::string prefix = _recvBuffer.substr(0, std::min(_recvBuffer.size(), (size_t)50));
        for(char& c : prefix) if (!std::isprint((unsigned char)c)) c = '.';
        LOG_DEBUG("Buffer contains incomplete data or unknown format. Size: %zu. Prefix: %s", _recvBuffer.size(), prefix.c_str());
        return {};
    }
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
        // LOG_DEBUG("Executing message callback for fd %d", getFd());
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
