#ifndef RTP_H264_UNPACKER_H
#define RTP_H264_UNPACKER_H

#include <cstdint>
#include <vector>
#include <map>
#include <fstream>
#include <mutex>
#include <iostream>

struct RtpPacket {
    uint16_t seq = 0;
    uint32_t timestamp = 0;
    bool marker = false;
    std::vector<uint8_t> payload;
};

class RtpH264Unpacker {
public:
    explicit RtpH264Unpacker(const std::string &outputFile = "output.h264");
    ~RtpH264Unpacker();

    // 解析并缓存一个RTP包
    void handleRtpPacket(const uint8_t *data, size_t len);

    // 输出缓冲中的剩余NALUs
    void flush();

private:
    void writeAnnexB(const std::vector<uint8_t> &nalu);
    void processPacket(const RtpPacket &pkt);
    void handleFuA(const RtpPacket &pkt);
    void outputNalu(uint8_t nalType, const std::vector<uint8_t> &data);

    std::ofstream _out;
    std::mutex _mtx;

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
};


#endif // RTP_H264_UNPACKER_H
