#include "h264decoder.h"
#include <QDebug>

H264Decoder::H264Decoder()
{
//    avcodec_register_all();  // 新版 FFmpeg 可省略，但写上没坏处
    _ok = initCodec();
}

H264Decoder::~H264Decoder()
{
    // 顺序：先释放格式转换上下文，再释放帧/包，最后释放解码器上下文
    if (_swsCtx) {
        sws_freeContext(_swsCtx);
        _swsCtx = nullptr;
    }
    if (_frame) {
        av_frame_unref(_frame); // 先 unref 再 free
        av_frame_free(&_frame);
        _frame = nullptr;
    }
    if (_packet) {
        av_packet_unref(_packet); // 先 unref 再 free
        av_packet_free(&_packet);
        _packet = nullptr;
    }
    if (_codecCtx) {
        avcodec_close(_codecCtx); // 先 close 再 free
        avcodec_free_context(&_codecCtx);
        _codecCtx = nullptr;
    }
}

bool H264Decoder::initCodec()
{
    /*
     *  avcodec_find_decoder 用于查找一个能够解码 H.264 的 AVCodec
     *  返回的是 FFmpeg 内置的 h264 解码器（底层包含 libavcodec）
     *  如果返回空指针，说明你的 FFmpeg 缺少 H.264 支持（编译时没启用）
    */
    _codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!_codec) {
        qWarning() << "H264Decoder: codec not found";
        return false;
    }
    /*
     *  解码器上下文用来保存关于解码器的各种配置与状态
     *  如：分辨率、像素格式、线程等等
     *  解码整个视频都需要这个对象，一定要保留到销毁时释放
    */
    _codecCtx = avcodec_alloc_context3(_codec);
    if (!_codecCtx) {
        qWarning() << "H264Decoder: alloc context failed";
        return false;
    }

    /*
     *  可选设置
     *  H.264 解码默认可以开启多线程（比如 4 线程）
     *  设置 thread_count = 1 表示强制单线程解码
     *  在实时流（如 RTSP、RTP）中，单线程的延迟更低
    */
    _codecCtx->thread_count = 1;
    /*
    * 分配内部缓冲区
    * 准备 PPS、SPS 等解析器
    * 解码器真正进入工作状态
    * 后续才能调用 avcodec_send_packet() / avcodec_receive_frame()
    */
    if (avcodec_open2(_codecCtx, _codec, nullptr) < 0) {
        qWarning() << "H264Decoder: avcodec_open2 failed";
        return false;
    }
    /*
     * AVPacket
     * 存放压缩后的H.264 NALU 数据
     * 如 IDR、P 帧、SPS、PPS 等
     * 你每次接收到一帧 H.264 数据都放进 _packet
     *
     * AVFrame
     * 存放解码后的图像数据（YUV）
     * 解码器输出的是这个 _frame
     * 你一般会把它转换为 QImage 或 RGB 才能显示
    */
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

    // 1. 先释放之前的包资源（确保初始状态干净）
    av_packet_unref(_packet);
    // 2. 深拷贝数据到AVPacket（堆分配，由 packet 管理））
//    int ret = av_packet_from_data(_packet, const_cast<uint8_t*>(data), len);
    uint8_t *buf = (uint8_t *)av_malloc(len);
    memcpy(buf, data, len);
    int ret = av_packet_from_data(_packet, buf, len);
    if (ret < 0) {
        qWarning() << "H264Decoder: av_packet_from_data failed";
        return false;
    }

    // 3. 发送数据包（此时 packet 持有堆内存）
    /*解析 NALU，更新 SPS/PPS，视频帧进行解码*/
    ret = avcodec_send_packet(_codecCtx, _packet);
    if (ret < 0) {
        qWarning() << "H264Decoder: avcodec_send_packet failed:" << ret;
        av_packet_unref(_packet); // 失败时释放包
        return false;
    }

    while (true) {
        ret = avcodec_receive_frame(_codecCtx, _frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {//数据不够，需要更多 packet或解码器已 flush
            break;
        }
        if (ret < 0) {//错误
            qWarning() << "H264Decoder: avcodec_receive_frame failed:" << ret;
            av_frame_unref(_frame); // 接收失败，释放当前帧
            av_packet_unref(_packet); // 释放包内存
            return false;
        }

         // 第一次拿到有效宽高（或宽高变化）时，重新初始化sws上下文（适配SPS动态更新，自适应 SPS 宽高变化）
        if (_frame->width > 0 && _frame->height > 0 &&
               (_swsCtx == nullptr || _dstW != _frame->width || _dstH != _frame->height)) {

               if (_swsCtx) sws_freeContext(_swsCtx);
               _dstW = _frame->width;
               _dstH = _frame->height;
               // 基于解码器输出的实际格式（可能随SPS变化）初始化格式转换上下文
               _swsCtx = sws_getContext(
                           _frame->width, _frame->height,
                           (AVPixelFormat)_frame->format,
                           _dstW, _dstH, AV_PIX_FMT_RGB24,
                           SWS_BILINEAR, nullptr, nullptr, nullptr);
               if (!_swsCtx) {
                   qWarning() << "H264Decoder: sws_getContext failed";
                   av_frame_unref(_frame); // 释放当前帧
                   return false;
               }
        }

        if (!_swsCtx){
            av_frame_unref(_frame);// 跳过前必须释放帧
            continue;
        }

        QImage img(_dstW, _dstH, QImage::Format_RGB888);//创建 QImage（RGB24）
        if (img.isNull()) {
            av_frame_unref(_frame);
            continue;
        }
        /*
         * 把 _frame 的 YUV420P/YUV422P/... 转成 RGB24
         */
        uint8_t *dstData[4] = { img.bits(), nullptr, nullptr, nullptr };
        int dstLinesize[4] = { img.bytesPerLine(), 0, 0, 0 };

        sws_scale(_swsCtx,
                  _frame->data, _frame->linesize,
                  0, _frame->height,
                  dstData, dstLinesize);
        //执行回调
        if (onFrame) {
            onFrame(img);  // 隐式共享，无需 copy
        }
        // 4. 关键：每帧接收完成后，必须释放 AVFrame 资源（避免堆累积）
        av_frame_unref(_frame);
    }
    // 5. 发送成功后，释放 packet 持有的内存
    av_packet_unref(_packet);
    return true;
}
