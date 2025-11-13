#include "SessionManager.h"
#include <sstream>
#include <time.h>

SessionManager::SessionManager() {
}

SessionManager::~SessionManager() {
}

std::string SessionManager::generateSessionId() {
    _sessionCounter++;
    std::ostringstream oss;
    oss << std::hex << std::time(nullptr) << "-" << _sessionCounter;
    return oss.str();
}

void SessionManager::addSession(RtspSession session) {
    std::lock_guard<std::mutex> lock(_sessionMutex);
    _sessions[session.sessionId] = session;
    _sessionCounter++;
}

void SessionManager::removeSession(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(_sessionMutex);
    _sessions.erase(sessionId);
}

RtspSession* SessionManager::getSession(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(_sessionMutex);
    auto it = _sessions.find(sessionId);
    if (it != _sessions.end()) {
        return &it->second;
    }
    return nullptr;
}
uint16_t SessionManager::allocateUdpPorts(){
    std::lock_guard<std::mutex> lock(_portMutex);
    int basePort = nextUdpPort.fetch_add(2);
    LOG_DEBUG("Allocated UDP ports starting from: %d", basePort);
    return basePort;
}