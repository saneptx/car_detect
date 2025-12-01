#ifndef H264DECODER_H
#define H264DECODER_H


#include <functional>
//#include <QImage>
#include <QSharedPointer>
#include <QMetaType>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

struct YUVFrame {
    // 原始数据缓存。使用 QSharedPointer 确保在信号槽传递时内存安全。
    // 解码线程负责创建和填充，主线程负责使用，引用计数为 0 时自动释放。
    QSharedPointer<QByteArray> data;

    int width = 0;
    int height = 0;
    // YUV 各分量步长（可能大于宽度，用于内存对齐）
    int yLinesize = 0;
    int uLinesize = 0;
    int vLinesize = 0;
    // YUV 各分量大小 (Linesize * Height)
    int ySize = 0;
    int uSize = 0;
    int vSize = 0;
};
Q_DECLARE_METATYPE(YUVFrame)
class H264Decoder {
public:
    H264Decoder();
    ~H264Decoder();

    // data: 一帧 H264（含 0x00000001 起始码）
    // onFrame: 每解出一帧就回调一个 QImage（RGB）
//    bool decode(const uint8_t *data, int len,
//                const std::function<void(const QImage &)> &onFrame);
    bool decode(const uint8_t *data, int len,
                const std::function<void(const YUVFrame &)> &onFrame);
    bool initCodec();
private:

    const AVCodec  *_codec       = nullptr;//解码器
    AVCodecContext *_codecCtx    = nullptr;//解码器上下文
    AVPacket       *_packet      = nullptr;//存放原始数据H.264
    AVFrame        *_frame       = nullptr;//存放解码后的数据YUV
    SwsContext     *_swsCtx      = nullptr;
//    int             _dstW        = 0;
//    int             _dstH        = 0;
    bool            _ok          = false;
};
#endif // H264DECODER_H
