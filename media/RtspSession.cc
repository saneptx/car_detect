#include "RtspSession.h"

RtspSession::RtspSession(const std::string& sessionId)
    : sessionId(sessionId)
    , lastActive(std::chrono::steady_clock::now()) {
}

std::string RtspSession::getSessionId() const {
    return sessionId;
}

RtspSession::~RtspSession() {
}

void RtspSession::updateLastActive() {
    lastActive = std::chrono::steady_clock::now();
}

bool RtspSession::isExpired(std::chrono::seconds timeout) const {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - lastActive);
    return duration.count() > timeout.count();
}

void RtspSession::setClientIp(std::string clientIp){
    videoChannel.clientIp = clientIp;
    audioChannel.clientIp = clientIp;
}