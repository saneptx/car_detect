#include "monitorclientwidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDebug>
#include <QtEndian>
#include <QNetworkProxy>
#include <QtConcurrent/QtConcurrentRun>
#include <QMutexLocker>
#include <QDateTime>
#include <QElapsedTimer>
#include <algorithm>

static const char *SERVER_IP   = "192.168.5.11";
static const quint16 SERVER_PORT = 9000;

MonitorClientWidget::MonitorClientWidget(QWidget *parent)
    : QWidget(parent),
      _socket(new QTcpSocket(this)),
      _grid(new QGridLayout)
{
    qDebug()<<"MonitorClientWidget()";
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
    // 程序启动后立刻连接固定 IP+端口
    _socket->connectToHost(QString::fromLatin1(SERVER_IP), SERVER_PORT);
    _h264.onFrameReady = [this](const QString &streamName,const QByteArray &frame) {
        // frame 是完整一帧 H.264（含 00 00 00 01 start code）
        handleFrame(streamName,frame);
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
        qDebug()<< streamName<<" "<<seq<<" "<<pktLen;
        _buffer.remove(0, offset + 4 + pktLen);
        handleRtpPacket(streamName,rtpPacket);
    }
}

void MonitorClientWidget::handleRtpPacket(const QString &streamName,const QByteArray &packet){
    RtpPacket pkt = parseRtp(packet);
    _h264.handleRtp(streamName,pkt);
}
void MonitorClientWidget::handleFrame(const QString &streamName,const QByteArray &frame)
{
    // 没有窗口/解码器就创建
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

        auto dec = new H264Decoder();
        if (!dec->isOk()) {
            qWarning() << "Decoder init failed for stream" << streamName;
        }
        _decoders[streamName] = dec;
        _decoderLocks[streamName] = QSharedPointer<QMutex>::create();
    }

    H264Decoder *dec = _decoders.value(streamName, nullptr);
    QLabel *label = _videoWidgets.value(streamName, nullptr);
    if (!dec || !label) return;

    if (_pendingDecodes.value(streamName, 0) >= kMaxPendingPerStream) {
        qWarning() << "Dropping frame for stream" << streamName << "due to decode backlog";
        return;
    }
    _pendingDecodes[streamName] = _pendingDecodes.value(streamName, 0) + 1;

    QByteArray frameCopy = frame;
    auto lock = _decoderLocks.value(streamName);
    if (lock.isNull()) {
        lock = QSharedPointer<QMutex>::create();
        _decoderLocks[streamName] = lock;
    }

    QtConcurrent::run([this, dec, label, frameCopy, lock, streamName]() {
        {
            QMutexLocker locker(lock.data());
            dec->decode(reinterpret_cast<const uint8_t*>(frameCopy.constData()),
                        frameCopy.size(),
                        [label](const QImage &img) {
                            if (img.isNull()) {
                                return;
                            }
                            QMetaObject::invokeMethod(label, [label, img]() {
                                QPixmap pix = QPixmap::fromImage(img)
                                                  .scaled(label->size(),
                                                          Qt::KeepAspectRatio,
                                                          Qt::SmoothTransformation);
                                label->setPixmap(pix);
                            }, Qt::QueuedConnection);
                        });
        }
        QMetaObject::invokeMethod(this, [this, streamName]() {
            int pending = _pendingDecodes.value(streamName, 0);
            pending = std::max(0, pending - 1);
            _pendingDecodes[streamName] = pending;
        }, Qt::QueuedConnection);
    });
}
