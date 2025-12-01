#ifndef H264RTPREASSEMBLER_H
#define H264RTPREASSEMBLER_H
#include <QByteArray>
#include <vector>
#include <functional>
#include <QMap>

struct RtpPacket {
    uint8_t  vpxcc;
    uint8_t  mpt;
    bool     marker;
    uint16_t seq;
    uint32_t timestamp;
    uint32_t ssrc;
    const uint8_t *payload;
    int payloadLen;
};

struct RtpUnit {
    uint16_t seq;
    QByteArray data;
};
class H264RtpReassembler
{
public:
    H264RtpReassembler();
    std::function<void(const QString &streamName,const QByteArray& frame)> onFrameReady;

    void handleRtp(const QString &streamName,const RtpPacket &pkt);

private:
    static void appendStartCode3(QByteArray &ba) {
        ba.append('\0');
        ba.append('\0');
        ba.append('\1');
    }
    static void appendStartCode4(QByteArray &ba) {
        ba.append('\0');
        ba.append('\0');
        ba.append('\0');
        ba.append('\1');
    }

    QByteArray _frame;               // 当前正在组装的一帧
//    std::vector<uint8_t> _fuData;    // 当前 FU-A 分片
    QMap<uint16_t, RtpUnit> fuBuffer;  // 保存该 FU-A 所有片段
    bool fuStarted = false;
};

#endif // H264RTPREASSEMBLER_H
