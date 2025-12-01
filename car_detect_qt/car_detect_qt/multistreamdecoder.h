#ifndef MULTISTREAMDECODER_H
#define MULTISTREAMDECODER_H

#include <QObject>
#include <QHash>
#include <QString>
#include <QLabel>
#include <QThread>

#include "decoderworker.h"
#include "videoopenglwidget.h"

class MultiStreamDecoder: public QObject
{
    Q_OBJECT
public:
    explicit MultiStreamDecoder(QObject *parent = nullptr)
            : QObject(parent)
        {}

    ~MultiStreamDecoder();
    // 注册一条流，对应一个 QLabel
//    void addStream(const QString &name, QLabel *label);
    void addStream(const QString &name, VideoOpenGLWidget *widget);

    void removeStream(const QString &name);

    // 主线程收到一帧H264数据时调用
    void pushFrame(const QString &name, const QByteArray &frame);

private:
    struct StreamContext {
//        QLabel *label = nullptr;
        VideoOpenGLWidget *widget = nullptr;
        DecoderWorker *worker = nullptr;
        QThread *thread = nullptr;
    };

    QHash<QString, StreamContext> _streams;
};

#endif // MULTISTREAMDECODER_H
