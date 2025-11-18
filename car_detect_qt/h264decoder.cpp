#include "h264decoder.h"
#include <QDebug>

H264Decoder::H264Decoder()
{
//    avcodec_register_all();  // 新版 FFmpeg 可省略，但写上没坏处
    _ok = initCodec();
}

H264Decoder::~H264Decoder()
{
    if (_swsCtx) sws_freeContext(_swsCtx);
    if (_frame)  av_frame_free(&_frame);
    if (_packet) av_packet_free(&_packet);
    if (_codecCtx) avcodec_free_context(&_codecCtx);
}

bool H264Decoder::initCodec()
{
    _codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!_codec) {
        qWarning() << "H264Decoder: codec not found";
        return false;
    }

    _codecCtx = avcodec_alloc_context3(_codec);
    if (!_codecCtx) {
        qWarning() << "H264Decoder: alloc context failed";
        return false;
    }

    // 可选：设置一些参数（根据需要）
    _codecCtx->thread_count = 1;

    if (avcodec_open2(_codecCtx, _codec, nullptr) < 0) {
        qWarning() << "H264Decoder: avcodec_open2 failed";
        return false;
    }

    _packet = av_packet_alloc();
    _frame  = av_frame_alloc();
    if (!_packet || !_frame) {
        qWarning() << "H264Decoder: alloc packet/frame failed";
        return false;
    }

    return true;
}

bool H264Decoder::decode(const uint8_t *data, int len,
                         const std::function<void(const QImage &)> &onFrame)
{
    if (!_ok || !data || len <= 0) return false;

    av_packet_unref(_packet);
    _packet->data = const_cast<uint8_t*>(data);
    _packet->size = len;

    int ret = avcodec_send_packet(_codecCtx, _packet);
    if (ret < 0) {
        qWarning() << "H264Decoder: avcodec_send_packet failed:" << ret;
        return false;
    }

    while (true) {
        ret = avcodec_receive_frame(_codecCtx, _frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            qWarning() << "H264Decoder: avcodec_receive_frame failed:" << ret;
            return false;
        }

        // 第一次拿到有效宽高时，初始化 sws 和输出尺寸
        if (_frame->width > 0 && _frame->height > 0 &&
            (_swsCtx == nullptr ||
             _dstW != _frame->width || _dstH != _frame->height)) {

            if (_swsCtx) sws_freeContext(_swsCtx);
            _dstW = _frame->width;
            _dstH = _frame->height;
            _swsCtx = sws_getContext(
                        _frame->width, _frame->height,
                        (AVPixelFormat)_frame->format,
                        _dstW, _dstH, AV_PIX_FMT_RGB24,
                        SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!_swsCtx) {
                qWarning() << "H264Decoder: sws_getContext failed";
                return false;
            }
        }

        if (!_swsCtx) continue;

        QImage img(_dstW, _dstH, QImage::Format_RGB888);
        uint8_t *dstData[4] = { img.bits(), nullptr, nullptr, nullptr };
        int dstLinesize[4] = { img.bytesPerLine(), 0, 0, 0 };

        sws_scale(_swsCtx,
                  _frame->data, _frame->linesize,
                  0, _frame->height,
                  dstData, dstLinesize);

        if (onFrame) {
            onFrame(img.copy());  // copy 避免后续覆盖
        }
    }

    return true;
}
