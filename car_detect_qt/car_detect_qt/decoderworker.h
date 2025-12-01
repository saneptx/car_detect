#ifndef DECODERWORKER_H
#define DECODERWORKER_H

#include <QObject>
#include <QImage>
#include <QByteArray>
#include "h264decoder.h"

class DecoderWorker: public QObject
{
    Q_OBJECT
public:
    explicit DecoderWorker(QObject *parent = nullptr)
    : QObject(parent)
    {
        _ok = _decoder.initCodec();
    }
public slots:
    // 主线程把一帧H264数据发过来，这个槽在“解码线程”里执行
    void onPacket(const QByteArray &ba);

signals:
//    void frameReady(const QImage &img);
    void frameReady(const YUVFrame &frame);

private:
    H264Decoder _decoder;
    bool _ok = false;
};

#endif // DECODERWORKER_H
