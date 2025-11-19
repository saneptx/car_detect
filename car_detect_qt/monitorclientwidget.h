#ifndef MONITORCLIENTWIDGET_H
#define MONITORCLIENTWIDGET_H

#include <QWidget>
#include <QtNetwork/QtNetwork>
#include <QGridLayout>
#include <QLabel>
#include <QMap>
#include <QMutex>
#include <QSharedPointer>
#include "h264decoder.h"
#include "h264rtpreassembler.h"


static RtpPacket parseRtp(const QByteArray &ba)
{
    RtpPacket pkt{};
    const uint8_t* p = (const uint8_t*)ba.constData();

    pkt.vpxcc      = p[0];
    pkt.mpt        = p[1];
    pkt.marker     = (pkt.mpt & 0x80) != 0;
    pkt.seq        = (p[2] << 8) | p[3];
    pkt.timestamp  = (p[4] << 24) | (p[5] << 16) | (p[6] << 8) | p[7];
    pkt.ssrc       = (p[8] << 24) | (p[9] << 16) | (p[10] << 8) | p[11];

    int headerLen = 12;
    pkt.payload    = p + headerLen;
    pkt.payloadLen = ba.size() - headerLen;

    return pkt;
}

class MonitorClientWidget: public QWidget{
    Q_OBJECT
public:
     explicit MonitorClientWidget(QWidget *parent = nullptr);
private slots:
    void onConnected();
    void onError(QAbstractSocket::SocketError);
    void onReadyRead();

private:
    void processBuffer();
    void handleRtpPacket(const QString &streamName,const QByteArray &packet);
    void handleFrame(const QString &streamName,const QByteArray &frame);

    QTcpSocket *_socket;
    QByteArray _buffer;
    QGridLayout *_grid;
    QMap<QString, H264Decoder*> _decoders;
    QMap<QString, QLabel*> _videoWidgets;
    QMap<QString, int> _pendingDecodes;
    QMap<QString, QSharedPointer<QMutex>> _decoderLocks;
    H264RtpReassembler _h264;

    static constexpr int kMaxPendingPerStream = 3;
    static constexpr int kLogIntervalFrames = 60;
};

#endif // MONITORCLIENTWIDGET_H
