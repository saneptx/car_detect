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

    // 返回 false 表示严重错误（比如打开解码器失败）
    bool isOk() const { return _ok; }

    // data: 一帧 H264（含 0x00000001 起始码）
    // onFrame: 每解出一帧就回调一个 QImage（RGB）
    bool decode(const uint8_t *data, int len,
                const std::function<void(const QImage &)> &onFrame);

private:
    bool initCodec();

    const AVCodec        *_codec       = nullptr;
    AVCodecContext *_codecCtx    = nullptr;
    AVPacket       *_packet      = nullptr;
    AVFrame        *_frame       = nullptr;
    SwsContext     *_swsCtx      = nullptr;
    int             _dstW        = 0;
    int             _dstH        = 0;
    bool            _ok          = false;
};
#endif // H264DECODER_H
