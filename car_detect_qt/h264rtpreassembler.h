#ifndef H264RTPREASSEMBLER_H
#define H264RTPREASSEMBLER_H
#include <QByteArray>
#include <vector>
#include <functional>
#include <QObject>
#include <QMap>


struct RtpPacket {
    uint8_t  vpxcc;             // 版本(2bit) + 填充(1bit) + 扩展(1bit) + CSRC计数(4bit)
    uint8_t  mpt;               // 标记位(1bit) + 负载类型(7bit)
    bool     marker;            // 标记位（从mpt提取）
    uint16_t seq;               // 序列号
    uint32_t timestamp;         // 时间戳
    uint32_t ssrc;              // 同步源标识符
    const uint8_t *payload;     // 负载数据指针
    int payloadLen;             // 负载长度
};
using RtpBuffer = QMap<quint16, QByteArray>;
inline RtpPacket parseRtp(const QByteArray &ba)
{
    RtpPacket pkt{};
    const uint8_t* p = reinterpret_cast<const uint8_t*>(ba.constData());

    pkt.vpxcc      = p[0];
    pkt.mpt        = p[1];
    pkt.marker     = (pkt.mpt & 0x80) != 0;
    pkt.seq        = static_cast<uint16_t>((p[2] << 8) | p[3]);
    pkt.timestamp  = (static_cast<uint32_t>(p[4]) << 24) |
                    (static_cast<uint32_t>(p[5]) << 16) |
                    (static_cast<uint32_t>(p[6]) << 8) |
                    static_cast<uint32_t>(p[7]);
    pkt.ssrc       = (static_cast<uint32_t>(p[8]) << 24) |
                    (static_cast<uint32_t>(p[9]) << 16) |
                    (static_cast<uint32_t>(p[10]) << 8) |
                    static_cast<uint32_t>(p[11]);

    int headerLen = 12;
    pkt.payload    = p + headerLen;
    pkt.payloadLen = ba.size() - headerLen;
    if (pkt.payloadLen < 0) {
        pkt.payloadLen = 0;
        pkt.payload = nullptr;
    }
    return pkt;
}
struct RtpUnit {
    uint16_t seq;   // 序列号
    QByteArray data;// 单元数据
};
class H264RtpReassembler: public QObject
{
    Q_OBJECT
signals:
    void onFrameReady(QString stringName,QByteArray frame);
public:
    explicit H264RtpReassembler(QObject *parent = nullptr);

    void handleRtp(const QString &streamName, const QByteArray &packet);
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
    void _processRtpNalu(const QString &streamName,const RtpPacket &pkt);
    void _checkAndSaveSpsPps(uint8_t nalType, const QByteArray &naluData);
    QByteArray _frame;               // 当前正在组装的一帧
    QMap<uint16_t, RtpUnit> _fuBuffer;  //  FU-A片段缓存
    bool _fuStarted = false;
    quint16 _lastSeq = 0;
    int       _bufferSizeThreshold = 100; // 初始缓存阈值
    QByteArray _spsNalu; // SPS NALU缓存
    QByteArray _ppsNalu;// PPS NALU缓存

    int _maxSeqGap = 3;              // 最大允许序列号间隔
    int _maxFuBufferSize = 1024;     // FU-A缓存最大单元数（防内存泄漏）
    quint16 _fuStartSeq = 0;         // 当前FU-A起始序列号（优化丢包检测）
    quint16 _fuExpectedSeq = 0;      // 期望的下一个FU-A序列号（优化丢包检测）
};

#endif // H264RTPREASSEMBLER_H
