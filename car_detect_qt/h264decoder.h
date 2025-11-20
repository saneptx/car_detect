#ifndef H264DECODER_H
#define H264DECODER_H


#include <functional>
#include <QImage>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

class H264Decoder {
public:
    H264Decoder();
    ~H264Decoder();

    // data: 一帧 H264（含 0x00000001 起始码）
    // onFrame: 每解出一帧就回调一个 QImage（RGB）
    bool decode(const uint8_t *data, int len,
                const std::function<void(const QImage &)> &onFrame);
    bool initCodec();
private:

    const AVCodec  *_codec       = nullptr;//解码器
    AVCodecContext *_codecCtx    = nullptr;//解码器上下文
    AVPacket       *_packet      = nullptr;//存放原始数据H.264
    AVFrame        *_frame       = nullptr;//存放解码后的数据YUV
    SwsContext     *_swsCtx      = nullptr;
    int             _dstW        = 0;
    int             _dstH        = 0;
    bool            _ok          = false;
};
#endif // H264DECODER_H
