#include "reactor/MultiThreadEventLoop.h"
#include <iostream>
#include <signal.h>
#include "reactor/cpp11_compat.h"
#include "reactor/Logger.h"

std::unique_ptr<MultiThreadEventLoop> g_server;
// std::atomic_bool g_stopFlag{false};
void signalHandler(int sig) {
    std::cout << "Received signal " << sig << ", shutting down..." << std::endl;
 
    if (g_server) g_server->stop();
}
int main() {

    signal(SIGINT,  signalHandler);
    signal(SIGTERM, signalHandler);

    LOG_INFO("Starting Multi-Thread RTSP Server...");
    g_server = std::make_unique<MultiThreadEventLoop>("192.168.5.11", 8554, 4);

    try {
        // 启动（内部会主 loop 阻塞）
        g_server->start();
    } catch (...) {
        LOG_ERROR("Server start failed");
    }


    g_server.reset();

    return 0;
}