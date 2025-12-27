#include "decoderworker.h"

static bool registerYUVFrame() {
    // 注册 YUVFrame，使其可以在 queued connection 中使用
    return qRegisterMetaType<YUVFrame>("YUVFrame");
}
static bool registered = registerYUVFrame();

void DecoderWorker::onPacket(const QByteArray &ba)
{
    if (!_ok || ba.isEmpty())
        return;

    // 调用新的解码函数
    _decoder.decode(reinterpret_cast<const uint8_t*>(ba.constData()),
                    ba.size(),
                    [this](const YUVFrame &frame) { // 替换 QImage
                        if (frame.data.isNull())
                            return;
                        // 注意：这个回调在“解码线程”中，被我们转成signal抛出去
                        emit frameReady(frame);
//                        static FILE* f = fopen("debug.yuv", "wb");
//                        if (f) {
//                            fwrite(frame.data->data(), 1, frame.data->size(), f);
//                        }
                });
}
