#include "monitorclientwidget.h"
//#include <QVBoxLayout>
//#include <QHBoxLayout>
//#include <QDebug>
//#include <QtEndian>
//#include <QNetworkProxy>
//#include <QtConcurrent/QtConcurrentRun>
//#include <QMutexLocker>
//#include <QDateTime>
//#include <QElapsedTimer>
//#include <algorithm>
#include <QVBoxLayout>
#include <QDebug>
#include <QtEndian>
#include <QNetworkProxy>

static const char *SERVER_IP   = "192.168.5.11";
static const quint16 SERVER_PORT = 9000;

MonitorClientWidget::MonitorClientWidget(QWidget *parent)
    : QWidget(parent),
      _socket(new QTcpSocket(this)),
      _grid(new QGridLayout),
      _decoderMgr(this)   // 让 MultiStreamDecoder 以本 widget 为 parent
{
    qDebug() << "MonitorClientWidget()";

    auto layout = new QVBoxLayout(this);
    layout->addLayout(_grid);

    connect(_socket, &QTcpSocket::connected,
            this, &MonitorClientWidget::onConnected);
    connect(_socket, &QTcpSocket::readyRead,
            this, &MonitorClientWidget::onReadyRead);
    connect(_socket,
            QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
            this, &MonitorClientWidget::onError);

    _socket->setProxy(QNetworkProxy::NoProxy);
    _socket->connectToHost(QString::fromLatin1(SERVER_IP), SERVER_PORT);

    // H264RtpReassembler 组好一"帧" H.264 时回调这里
    _h264.onFrameReady = [this](const QString &streamName, const QByteArray &frame) {
        // frame 是完整一帧 H.264（含 00 00 00 01 start code）
        handleFrame(streamName, frame);
        // 如果还想 dump 到文件调试，可以在这里写 test.h264
    };
}

void MonitorClientWidget::onConnected()
{
    qDebug() << "Connected to monitor server";
}

void MonitorClientWidget::onError(QAbstractSocket::SocketError err)
{
    qWarning() << "Socket error:" << err << _socket->errorString();
}

void MonitorClientWidget::onReadyRead()
{
    _buffer.append(_socket->readAll());
    processBuffer();
}

// 解析自定义协议
void MonitorClientWidget::processBuffer()
{
    while(true){
        //先查看有没有nameLen
        if (_buffer.size() < 2)
            return;
        quint16 nameLen = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(_buffer.constData()));//占10个字节
        int offset = 2;
        QString streamName = QString::fromUtf8(_buffer.constData() + offset, nameLen);
        offset += nameLen;//12
        quint16 pktLen = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(_buffer.constData()) + offset + 2);
        if(_buffer.size() < offset + 4 + pktLen){
            //数据没收全
            return;
        }
        QByteArray rtpPacket = _buffer.mid(offset + 4, pktLen);
        uint16_t seq = ((uint8_t)rtpPacket[2] << 8) | (uint8_t)rtpPacket[3];
//        qDebug()<< streamName<<" "<<seq<<" "<<pktLen;
        _buffer.remove(0, offset + 4 + pktLen);
        handleRtpPacket(streamName,rtpPacket);
    }
}

void MonitorClientWidget::handleRtpPacket(const QString &streamName,const QByteArray &packet){
    RtpPacket pkt = parseRtp(packet);
    _h264.handleRtp(streamName,pkt);
}

// 收到一整帧 H.264（Annex B，含起始码）
void MonitorClientWidget::handleFrame(const QString &streamName, const QByteArray &frame)
{
    // 如果该 stream 还没有 QLabel + 解码线程，就在这里创建
    if (!_videoWidgets.contains(streamName)) {
        int idx = _videoWidgets.size();
        int row = idx / 2;
        int col = idx % 2;

        auto label = new QLabel(this);
        label->setFixedSize(1920, 1080);
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet("background-color: black; color: white;");
        label->setText(streamName);

        _grid->addWidget(label, row, col);
        _videoWidgets[streamName] = label;

        // 为这个stream注册解码管线（线程+worker+H264Decoder）
        _decoderMgr.addStream(streamName, label);

        qDebug() << "Created label and decoder pipeline for stream" << streamName;
    }

    // 把这一帧 H264 扔给对应 stream 的解码线程
    _decoderMgr.pushFrame(streamName, frame);
}
