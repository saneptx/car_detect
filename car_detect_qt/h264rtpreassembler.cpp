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
//        qDebug()<<"not FU-A "<<nalType<<" "<<len;
        appendStartCode4(_frame);
        _frame.append((const char*)p, len);
    }
    // ------------------- FU-A 分片 ------------------------
    else if (nalType == 28 && len >= 2) {
        uint8_t fuIndicator = p[0];
        uint8_t fuHeader    = p[1];
        uint8_t start = fuHeader & 0x80;
        uint8_t end   = fuHeader & 0x40;
        uint8_t realType = fuHeader & 0x1F;
//        qDebug()<<"FU-A "<<nalType<<" "<<realType<<" "<<len;
        if (start) {
            _fuData.clear();
            uint8_t nalHeader = (fuIndicator & 0xE0) | realType;
            _fuData.push_back(nalHeader);
            _fuData.insert(_fuData.end(), p+2, p+len);
        }else if (end) {
            appendStartCode4(_frame);
            _frame.append((const char*)_fuData.data(), _fuData.size());
            _fuData.clear();
        }else {
            _fuData.insert(_fuData.end(), p+2, p+len);
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

//void H264RtpReassembler::handleRtp(const QString &streamName, const RtpPacket& pkt)
//{
//    const uint8_t* p = pkt.payload;
//    int len = pkt.payloadLen;
//    if (len < 1) return;

//    uint8_t nal = p[0] & 0x1F;

//    // ------------------ 单 NALU ------------------
//    if (nal > 0 && nal < 24) {
//        appendStartCode4(_frame);
//        _frame.append((const char*)p, len);
//    }

//    // ------------------ FU-A ------------------
//    else if (nal == 28 && len >= 2) {
//        uint8_t fuIndicator = p[0];
//        uint8_t fuHeader    = p[1];
//        bool start = fuHeader & 0x80;
//        bool end   = fuHeader & 0x40;
//        uint8_t realType = fuHeader & 0x1F;

//        if (start) {
//            fuBuffer.clear();
//            fuStarted = true;

//            QByteArray unit;
//            uint8_t nalHeader = (fuIndicator & 0xE0) | realType;
//            unit.push_back(nalHeader);
//            unit.append((const char*)p + 2, len - 2);

//            fuBuffer.insert(pkt.seq, { pkt.seq, unit });
//        }
//        else if (fuStarted) {
//            QByteArray unit;
//            unit.append((const char*)p + 2, len - 2);
//            fuBuffer.insert(pkt.seq, { pkt.seq, unit });

//            if (end) {
//                // 检查 seq 是否连续
//                auto keys = fuBuffer.keys();
//                for (int i = 1; i < keys.size(); ++i) {
//                    if ((keys[i-1] + 1) != keys[i]) {
//                        // 缺包，丢弃本 NALU
//                        fuBuffer.clear();
//                        fuStarted = false;
//                        return;
//                    }
//                }

//                // 拼接
//                appendStartCode4(_frame);
//                for (auto& kv : fuBuffer) {
//                    _frame.append(kv.data);
//                }
//                fuBuffer.clear();
//                fuStarted = false;
//            }
//        }
//    }

//    // ------------------ M=1 输出一帧 ------------------
//    if (pkt.marker && !_frame.isEmpty()) {
//        qDebug()<<"recive complete frame!";
//        onFrameReady(streamName, _frame);
//        _frame.clear();
//    }
//}
