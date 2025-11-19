#include "h264rtpreassembler.h"
#include <QDebug>
H264RtpReassembler::H264RtpReassembler()
{

}

void H264RtpReassembler::handleRtp(const QString &streamName,const RtpPacket &pkt) {
    if (pkt.payloadLen <= 0)
        return;

    const uint8_t *p = pkt.payload;
    int len = pkt.payloadLen;
    uint8_t nalType = p[0] & 0x1F;

    // ------------------- 单 NALU 包 -----------------------
    if (nalType > 0 && nalType < 24) {
        appendStartCode(_frame);
        _frame.append((const char*)p, len);
    }

    // ------------------- FU-A 分片 ------------------------
    else if (nalType == 28 && len >= 2) {
        uint8_t fuIndicator = p[0];
        uint8_t fuHeader    = p[1];
        uint8_t start = fuHeader & 0x80;
        uint8_t end   = fuHeader & 0x40;
        uint8_t realType = fuHeader & 0x1F;

        if (start) {
            _fuData.clear();
            uint8_t nalHeader = (fuIndicator & 0xE0) | realType;
            _fuData.push_back(nalHeader);
            _fuData.insert(_fuData.end(), p+2, p+len);
        } else {
            _fuData.insert(_fuData.end(), p+2, p+len);
        }

        if (end) {
            appendStartCode(_frame);
            _frame.append((const char*)_fuData.data(), _fuData.size());
            _fuData.clear();
        }
    }

    // ------------------- M 位标记一帧结束 -------------------
    if (pkt.marker) {   // M = 1 → 完整一帧
        if (!_frame.isEmpty()) {
            qDebug()<<"recive complete frame!";
            if (onFrameReady)
                onFrameReady(streamName,_frame);
            _frame.clear();
        }
    }
}
