#include "monitorclientwidget.h"
<<<<<<< ours
<<<<<<< ours
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
=======
=======
>>>>>>> theirs
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDebug>
#include <QtEndian>
#include <QNetworkProxy>
<<<<<<< ours
>>>>>>> theirs
=======
>>>>>>> theirs

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
    while (true) {
        // 先看是否有 nameLen
        if (_buffer.size() < 2)
            return;
        const uchar *data = reinterpret_cast<const uchar*>(_buffer.constData());
        quint16 nameLen = qFromBigEndian<quint16>(data);

        if (_buffer.size() < 2 + nameLen + 4 + 4)
            return;

        int offset = 2;
        QString streamName = QString::fromUtf8(_buffer.constData() + offset, nameLen);
        offset += nameLen;

        quint32 ts = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar*>(_buffer.constData() + offset));
        offset += 4;

        quint32 frameLen = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar*>(_buffer.constData() + offset));
        offset += 4;

        if (_buffer.size() < offset + 8 + int(frameLen))
            return;

        quint64 sendTimeUs = qFromBigEndian<quint64>(
            reinterpret_cast<const uchar*>(_buffer.constData() + offset));
        offset += 8;

        if (_buffer.size() < offset + int(frameLen))
            return;

        QByteArray frame = _buffer.mid(offset, frameLen);
        _buffer.remove(0, offset + frameLen);

        handleFrame(streamName, frame, ts, sendTimeUs);
    }
}

// 这里接到一帧 H264 (带 0x00000001)
// 先简单打印，后面你在这里接 FFmpeg 解码并显示
//void MonitorClientWidget::handleFrame(const QString &streamName,
//                                      const QByteArray &frame, quint32 ts)
//{
//    // 没有图像窗口就创建一个
//    if (!_videoWidgets.contains(streamName)) {
//        int idx = _videoWidgets.size();
//        int row = idx / 2;
//        int col = idx % 2;
//        auto label = new QLabel(this);
//        label->setFixedSize(320, 240);
//        label->setText(streamName);
//        label->setStyleSheet("background-color: black; color: white;");
//        _grid->addWidget(label, row, col);
//        _videoWidgets[streamName] = label;
//    }

//    // TODO: 在这里调用 H264Decoder 解码 frame，得到 QImage
//    // 目前先打个日志看数据在动
//    qDebug() << "frame from" << streamName
//             << "len=" << frame.size()
//             << "ts=" << ts;

//    // 解码后类似这样更新画面：
//    // QImage img = ...;
//    // auto label = _videoWidgets[streamName];
//    // label->setPixmap(QPixmap::fromImage(img).scaled(
//    //     label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
//}

void MonitorClientWidget::handleFrame(const QString &streamName,
                                      const QByteArray &frame, quint32 ts,
                                      quint64 sendTimeUs)
{
    // 没有窗口/解码器就创建
    if (!_videoWidgets.contains(streamName)) {
        int idx = _videoWidgets.size();
        int row = idx / 2;
        int col = idx % 2;

        auto label = new QLabel(this);
        label->setFixedSize(320, 240);
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet("background-color: black; color: white;");
        label->setText(streamName);
        _grid->addWidget(label, row, col);
        _videoWidgets[streamName] = label;

<<<<<<< ours
<<<<<<< ours
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

    quint64 nowUs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch() * 1000ULL;
    quint64 networkDelayUs = (nowUs > sendTimeUs) ? (nowUs - sendTimeUs) : 0;

    QtConcurrent::run([this, dec, label, frameCopy, lock, streamName, networkDelayUs]() {
        QElapsedTimer decodeTimer;
        decodeTimer.start();
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
        quint64 decodeUs = decodeTimer.nsecsElapsed() / 1000ULL;
        QMetaObject::invokeMethod(this, [this, streamName, networkDelayUs, decodeUs]() {
            int pending = _pendingDecodes.value(streamName, 0);
            pending = std::max(0, pending - 1);
            _pendingDecodes[streamName] = pending;

            auto stats = _stats.value(streamName);
            stats.frameCount++;
            stats.networkDelayAccumUs += networkDelayUs;
            stats.decodeDelayAccumUs += decodeUs;
            if (stats.frameCount >= kLogIntervalFrames) {
                double avgNetMs = (double)stats.networkDelayAccumUs / (double)stats.frameCount / 1000.0;
                double avgDecodeMs = (double)stats.decodeDelayAccumUs / (double)stats.frameCount / 1000.0;
                qInfo() << "Stream" << streamName
                        << "avg network delay" << avgNetMs << "ms, avg decode" << avgDecodeMs << "ms";
                stats.frameCount = 0;
                stats.networkDelayAccumUs = 0;
                stats.decodeDelayAccumUs = 0;
            }
            _stats[streamName] = stats;
        }, Qt::QueuedConnection);
    });

    Q_UNUSED(ts);
}
=======
=======
>>>>>>> theirs
        auto dec = new H264Decoder();
        if (!dec->isOk()) {
            qWarning() << "Decoder init failed for stream" << streamName;
        }
        _decoders[streamName] = dec;
    }

    H264Decoder *dec = _decoders.value(streamName, nullptr);
    QLabel *label = _videoWidgets.value(streamName, nullptr);
    if (!dec || !label) return;

    // 解码这一帧（目前在 GUI 线程里，先这样用）
    dec->decode(reinterpret_cast<const uint8_t*>(frame.constData()),
                frame.size(),
                [label](const QImage &img) {
                    // 回调在当前线程执行，这里直接更新即可
                    if (!img.isNull()) {
                        QPixmap pix = QPixmap::fromImage(img)
                                      .scaled(label->size(),
                                              Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation);
                        label->setPixmap(pix);
                    }
                });
    Q_UNUSED(ts);
}
<<<<<<< ours
>>>>>>> theirs
=======
>>>>>>> theirs
