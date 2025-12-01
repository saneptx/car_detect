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
#include "multistreamdecoder.h"


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

struct transUdpPort{
    QUdpSocket * _udpRtpSocket;
    QUdpSocket * _udpRtcpSocket;
};

class MonitorClientWidget: public QWidget{
    Q_OBJECT
public:
     explicit MonitorClientWidget(QWidget *parent = nullptr);
private slots:
    void onConnected();
    void onError(QAbstractSocket::SocketError);
    void onReadyRead();
    void parseRespond(const QString& request,
                      QString& statusCode,
                      QString& status,
                      QMap<QString, QString>& headers);
    void sendSetup();
    void sendMessage();

private:
    void processBuffer();
    void handleRtpPacket(const QString &streamName,const QByteArray &packet);
    void handleFrame(const QString &streamName,const QByteArray &frame);

    QTcpSocket *_socket;
    QMap<QString,transUdpPort> _camMap;
    QByteArray _buffer;
    QGridLayout *_grid;
    int _cseq = 0;
    // 每路一个 QLabel，用于显示画面
   QMap<QString, VideoOpenGLWidget *> _videoWidgets;

   // RTP → H264 一帧
   H264RtpReassembler _h264;

   // 多路解码管理器（内部有 DecoderWorker + QThread）
   MultiStreamDecoder _decoderMgr;

   quint16 _lastSeq;
};

#endif // MONITORCLIENTWIDGET_H
