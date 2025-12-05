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
#include "kcphandler.h"

struct transUdpPort{
    QUdpSocket * _udpRtpSocket;
    QUdpSocket * _udpRtcpSocket;
    uint32_t conv;
};

class MonitorClientWidget: public QWidget{
    Q_OBJECT
public:
     explicit MonitorClientWidget(QWidget *parent = nullptr);
    ~MonitorClientWidget();
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
    void sendAddCamRespond(QString sessionId);

private:
    void processBuffer();
//    void handleRtpPacket(const QString &streamName,const QByteArray &packet);
    void handleFrame(const QString &streamName,const QByteArray &frame);

    QTcpSocket *_socket;
    QMap<QString,transUdpPort> _camMap;
    QByteArray _buffer;
    QGridLayout *_grid;
    int _cseq = 0;
    // 每路一个 QLabel，用于显示画面
    QMap<QString, VideoOpenGLWidget *> _videoWidgets;
    FILE* f;
    // RTP → H264 一帧
    H264RtpReassembler _h264RtpReassmbler;

    // 多路解码管理器（内部有 DecoderWorker + QThread）
    MultiStreamDecoder _decoderMgr;
    uint32_t _conv = 1000;
    quint16 _lastSeq;
    QMutex _mtx;
};

#endif // MONITORCLIENTWIDGET_H
