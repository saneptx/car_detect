#include "RtpH264Unpacker.h"
#include "Logger.h"

RtpH264Unpacker::RtpH264Unpacker(const std::string &outputFile) {
    _out.open(outputFile, std::ios::binary);
    if (!_out) throw std::runtime_error("Failed to open output file");
    std::cout << "[Unpacker] Initialized, writing to " << outputFile << std::endl;
}

RtpH264Unpacker::~RtpH264Unpacker() {
    flush();
    if (_out.is_open()) _out.close();
}

void RtpH264Unpacker::handleRtpPacket(const uint8_t *data, size_t len) {
    if (len < 12) return; // RTP header too short
    std::lock_guard<std::mutex> lock(_mtx);
    // 解析RTP头
    uint16_t seq = (data[2] << 8) | data[3];
    uint32_t ts = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];
    bool marker = data[1] & 0x80;
    const uint8_t *payload = data + 12;
    size_t payloadLen = len - 12;

    // 检查重复
    if (_reorderBuf.count(seq)) {
        std::cout << "[Unpacker] Duplicate seq=" << seq << " dropped.\n";
        return;
    }

    // 缓存该包
    RtpPacket pkt{seq, ts, marker, std::vector<uint8_t>(payload, payload + payloadLen)};
    _reorderBuf[seq] = std::move(pkt);

    // 控制缓存大小
    if (_reorderBuf.size() > MAX_BUFFER) {
        uint16_t firstSeq = _reorderBuf.begin()->first;
        processPacket(_reorderBuf[firstSeq]);
        _reorderBuf.erase(firstSeq);
    }

    // 按序号连续输出
    if (_firstPacket) {
        _expectedSeq = seq;
        _firstPacket = false;
    }

    while (_reorderBuf.count(_expectedSeq)) {
        processPacket(_reorderBuf[_expectedSeq]);
        _reorderBuf.erase(_expectedSeq);
        _expectedSeq++;
    }
}

void RtpH264Unpacker::processPacket(const RtpPacket &pkt) {
    if (pkt.payload.empty()) return;

    uint8_t nal = pkt.payload[0];
    uint8_t nalType = nal & 0x1F;

    if (nalType >= 1 && nalType <= 23) {
        // 单包NALU
        outputNalu(nalType, pkt.payload);
    } else if (nalType == 28) {
        // FU-A 分片
        handleFuA(pkt);
    } else {
        std::cout << "[Unpacker] Unsupported NAL type: " << (int)nalType << "\n";
    }
}

void RtpH264Unpacker::handleFuA(const RtpPacket &pkt) {
    if (pkt.payload.size() < 2) return;

    uint8_t fuIndicator = pkt.payload[0];
    uint8_t fuHeader = pkt.payload[1];
    uint8_t start = fuHeader >> 7;
    uint8_t end = (fuHeader >> 6) & 0x01;
    uint8_t nalType = fuHeader & 0x1F;
    uint8_t reconstructedNal = (fuIndicator & 0xE0) | nalType;

    if (start) {
        _assembling = true;
        _currentTimestamp = pkt.timestamp;
        _currentNalType = nalType;
        _assemblingBuf.clear();
        _assemblingBuf.insert(_assemblingBuf.end(), {0x00, 0x00, 0x00, 0x01});
        _assemblingBuf.push_back(reconstructedNal);
        _assemblingBuf.insert(_assemblingBuf.end(), pkt.payload.begin() + 2, pkt.payload.end());
    } else if (_assembling && pkt.timestamp == _currentTimestamp) {
        _assemblingBuf.insert(_assemblingBuf.end(), pkt.payload.begin() + 2, pkt.payload.end());
        if (end) {
            _out.write(reinterpret_cast<const char*>(_assemblingBuf.data()), _assemblingBuf.size());
            _assembling = false;
        }
    } else {
        std::cout << "[Unpacker][WARN] FU-A fragment TS mismatch or missing start; dropped.\n";
        _assembling = false;
    }
}

void RtpH264Unpacker::outputNalu(uint8_t nalType, const std::vector<uint8_t> &data) {
    static const uint8_t startCode[4] = {0x00, 0x00, 0x00, 0x01};
    _out.write(reinterpret_cast<const char*>(startCode), 4);
    _out.write(reinterpret_cast<const char*>(data.data()), data.size());
}

void RtpH264Unpacker::flush() {
    std::lock_guard<std::mutex> lock(_mtx);
    for (auto &kv : _reorderBuf) {
        processPacket(kv.second);
    }
    _reorderBuf.clear();
}
