#include "MultiThreadEventLoop.h"
#include "../media/RtspConnect.h"
#include "UdpConnection.h"
#include "InetAddress.h"
#include <iostream>
#include <algorithm>
#include "cpp11_compat.h"
#include "Logger.h"

using std::cout;
using std::endl;



MultiThreadEventLoop::MultiThreadEventLoop(const std::string& ip, unsigned short port, size_t threadNum)
: _acceptor(ip, port)
, _mainLoop(_acceptor, true)
, _threadNum(threadNum)
, _threadPool(_threadNum, 10)
, _nextLoopIndex(0)
, _running(false) {
    
    LOG_INFO("MultiThreadEventLoop created - IP: %s, Port: %d, Threads: %zu", ip.c_str(), port, threadNum);
    
    // 创建子EventLoop
    for (size_t i = 0; i < _threadNum; ++i) {
        _subLoops.emplace_back(std::make_unique<EventLoop>(_acceptor, false));
        LOG_DEBUG("Created sub EventLoop %zu", i);
    }
}

MultiThreadEventLoop::~MultiThreadEventLoop() {
    LOG_DEBUG("MultiThreadEventLoop destructor called");
    // stop();
}

void MultiThreadEventLoop::start() {
    LOG_INFO("Starting MultiThreadEventLoop...");
    _running = true;
    
    // 启动工作线程，每个线程运行一个EventLoop
    for (size_t i = 0; i < _threadNum; ++i) {
        size_t loopIndex = i;  // 捕获索引
        _threadPool.addTask([this, loopIndex]() {
            LOG_INFO("Worker thread starting EventLoop %zu", loopIndex);
            if (loopIndex < _subLoops.size()) {
                EventLoop* loop = _subLoops[loopIndex].get();
                // 设置消息和关闭回调
                loop->setMessageCallback(
                    std::bind(&MultiThreadEventLoop::onMessage, this, std::placeholders::_1));
                loop->setCloseCallback(
                    std::bind(&MultiThreadEventLoop::onClose, this, std::placeholders::_1));
                loop->loop();
            }
            LOG_INFO("Worker thread EventLoop %zu stopped", loopIndex);
        });
        LOG_DEBUG("Added thread task %zu to thread pool", i);
    }
    _threadPool.start();
    LOG_INFO("Thread pool started with %zu threads", _threadNum);

    // 设置主EventLoop的回调
    _mainLoop.setNewConnectionCallback(
        std::bind(&MultiThreadEventLoop::onNewConnection, this, std::placeholders::_1));
    
    // 启动主EventLoop
    LOG_INFO("Starting main EventLoop...");
    _acceptor.ready();
    _mainLoop.loop();
}

void MultiThreadEventLoop::stop() {
    if (!_running) {
        LOG_DEBUG("MultiThreadEventLoop already stopped");
        return;
    }
    
    LOG_INFO("Stopping MultiThreadEventLoop...");
    _running = false;

    // 停止所有子EventLoop
    LOG_DEBUG("Stopping all sub EventLoops");
    for (auto& loop : _subLoops) {
        loop->unloop();
    }  


    LOG_DEBUG("Stopping main EventLoop");
    _mainLoop.unloop();
  


    LOG_DEBUG("Stopping thread pool");
    _threadPool.stop();


    _subLoops.clear();

    LOG_DEBUG("Stopping main EventLoop");
    
    LOG_INFO("MultiThreadEventLoop stopped successfully");
}

EventLoop* MultiThreadEventLoop::getNextLoop() {
    if (_subLoops.empty()) {
        LOG_WARN("No sub loops available, using main loop");
        return &_mainLoop;
    }
    
    // 轮询方式选择下一个EventLoop
    size_t index = _nextLoopIndex.fetch_add(1) % _subLoops.size();
    LOG_DEBUG("Selected sub EventLoop %zu for new connection", index);
    return _subLoops[index].get();
}

// void MultiThreadEventLoop::threadFunc() {
//     // 这个方法现在不再使用，线程启动逻辑已移到start()中的lambda表达式
//     // 保留此方法以防其他地方调用，但应该不会执行到这里
//     LOG_WARN("threadFunc() called but should not be reached");
// }

void MultiThreadEventLoop::onNewConnection(int connfd) {
    EventLoop* loop = getNextLoop();
    loop->runInLoop([connfd, loop, this]() {
        TcpConnectionPtr connPtr(new TcpConnection(connfd, loop));
        LOG_DEBUG("Created TcpConnection for fd: %d", connfd);
        loop->addTcpConnection(connPtr);
        connPtr->setMessageCallback(
            std::bind(&MultiThreadEventLoop::onMessage, this, std::placeholders::_1));
        connPtr->setCloseCallback(
            std::bind(&MultiThreadEventLoop::onClose, this, std::placeholders::_1));
        auto rtspConn = std::make_shared<RtspConnect>(connPtr, loop);
        connPtr->setRtspConnect(rtspConn);
        LOG_DEBUG("RTSP connection setup completed for fd: %d", connfd);
    });
}

void MultiThreadEventLoop::onMessage(const TcpConnectionPtr& connPtr) {
    LOG_DEBUG("Received message from connection: %s", connPtr->toString().c_str());
    
    // 接收RTSP请求
    std::string request = connPtr->reciveRtspRequest();
    if (request.empty()) {
        // 连接可能已关闭或数据不完整
        LOG_DEBUG("Empty RTSP request received, connection may be closed");
        return;
    }
    auto rtspConn = connPtr->getRtspConnect();
    if (rtspConn) {
        LOG_DEBUG("Handling RTSP request");
        rtspConn->handleRequest(request);
    } else {
        LOG_WARN("No RTSP connection found for: %s", connPtr->toString().c_str());
    }
}

void MultiThreadEventLoop::onClose(const TcpConnectionPtr& connPtr) {
    LOG_INFO("Connection closed: %s", connPtr->toString().c_str());
    auto rtspConn = connPtr->getRtspConnect();
    if (rtspConn) {
        LOG_DEBUG("RTSP session will be released for: %s", connPtr->toString().c_str());
        rtspConn->releaseSession();
    }
}
