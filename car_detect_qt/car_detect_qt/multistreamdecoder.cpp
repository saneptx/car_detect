#include "multistreamdecoder.h"

MultiStreamDecoder:: ~MultiStreamDecoder()
{
    // 清理线程
    for (auto &ctx : _streams) {
        ctx.thread->quit();
        ctx.thread->wait();
        ctx.thread->deleteLater();
    }
    _streams.clear();
}
//void MultiStreamDecoder::addStream(const QString &name, QLabel *label)
//{
//    if (_streams.contains(name))
//        return;

//    StreamContext ctx;
//    ctx.label = label;
//    ctx.worker = new DecoderWorker;
//    ctx.thread = new QThread;

//    ctx.worker->moveToThread(ctx.thread);
//    ctx.thread->start();

//    // 解码完成后的信号，回到主线程更新UI
//    connect(ctx.worker, &DecoderWorker::frameReady,
//            label, [label](const QImage &img) {
//        if (img.isNull())
//            return;
//        // 主线程里执行这个 lambda
//        QPixmap pix = QPixmap::fromImage(img)
//                      .scaled(label->size(),
//                              Qt::KeepAspectRatio,
//                              Qt::FastTransformation);
//        label->setPixmap(pix);
//    }, Qt::QueuedConnection); // 显式指定也可以，不指定跨线程默认也是 Queued

//    _streams.insert(name, ctx);
//}

void MultiStreamDecoder::addStream(const QString &name, VideoOpenGLWidget *widget)
{
    if (_streams.contains(name))
        return;

    StreamContext ctx;
    ctx.widget = widget;
    ctx.worker = new DecoderWorker;
    ctx.thread = new QThread;
    ctx.worker->moveToThread(ctx.thread);
    ctx.thread->start();

    // 解码完成后的信号，回到主线程更新UI
    connect(ctx.worker, &DecoderWorker::frameReady,
            widget, &VideoOpenGLWidget::updateFrame, // 【核心改动 2: 直接连接到新的槽函数】
            Qt::QueuedConnection);

    _streams.insert(name, ctx);
}

void MultiStreamDecoder::removeStream(const QString &name)
{
    if (!_streams.contains(name))
        return;
    auto ctx = _streams.take(name);
    ctx.thread->quit();
    ctx.thread->wait();
    ctx.thread->deleteLater();
    // worker 由线程退出时 deleteLater 或外部管理，这里简单起见不重复 delete
}

// 主线程收到一帧H264数据时调用
void MultiStreamDecoder::pushFrame(const QString &name, const QByteArray &frame)
{
    auto it = _streams.find(name);
    if (it == _streams.end())
        return;

    // 拷贝一份，防止外部 buffer 生命周期问题
    QByteArray copy = frame;

    // 跨线程调用worker的槽函数（自动Queued）
    QMetaObject::invokeMethod(
        it->worker,
        "onPacket",
        Qt::QueuedConnection,
        Q_ARG(QByteArray, copy)
    );
}
