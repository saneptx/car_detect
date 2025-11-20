#include "decoderworker.h"

void DecoderWorker::onPacket(const QByteArray &ba)
{
    if (!_ok || ba.isEmpty())
        return;

    // 调用你现有的解码函数
    _decoder.decode(reinterpret_cast<const uint8_t*>(ba.constData()),
                    ba.size(),
                    [this](const QImage &img) {
                    if (img.isNull())
                        return;
                    // 注意：这个回调在“解码线程”中，被我们转成signal抛出去
                    emit frameReady(img);
                });
}
