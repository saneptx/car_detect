#ifndef RTP_H264_UNPACKER_H
#define RTP_H264_UNPACKER_H

#include <cstdint>
#include <vector>
#include <map>
#include <fstream>
#include <mutex>
#include <iostream>
#include <functional>

struct RtpPacket {
    uint16_t seq = 0;
    uint32_t timestamp = 0;
    bool marker = false;
    std::vector<uint8_t> payload;
};

// 回调：每组装出一帧 NALU 时通知外部
using H264NaluCallback = std::function<void(const uint8_t *data, size_t len, uint32_t timestamp)>;

class RtpH264Unpacker {
public:

    explicit RtpH264Unpacker();
    ~RtpH264Unpacker();

    // 解析并缓存一个RTP包
    void handleRtpPacket(const uint8_t *data, size_t len);

    // 输出缓冲中的剩余NALUs
    void flush();

    // 设置外部回调：可用于把 NALU 推给监控服务器/解码器
    void setNaluCallback(H264NaluCallback cb);

private:
    void writeAnnexB(const std::vector<uint8_t> &nalu);
    void processPacket(const RtpPacket &pkt);
    void handleFuA(const RtpPacket &pkt);
    void outputNalu(uint8_t nalType, const std::vector<uint8_t> &data, uint32_t timestamp);
    void drainBuffer(bool forceLossRecovery);

    // RTP 重排缓存
    static constexpr size_t MAX_BUFFER = 64;
    std::map<uint16_t, RtpPacket> _reorderBuf;
    uint16_t _expectedSeq = 0;
    bool _firstPacket = true;

    // FU-A 拼接状态
    bool _assembling = false;
    uint8_t _currentNalType = 0;
    uint32_t _currentTimestamp = 0;
    std::vector<uint8_t> _assemblingBuf;

    H264NaluCallback _callback;
};


#endif // RTP_H264_UNPACKER_H
