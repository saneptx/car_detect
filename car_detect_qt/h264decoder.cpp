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
//    if (_swsCtx) {
//        sws_freeContext(_swsCtx);
//        _swsCtx = nullptr;
//    }
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
     * avcodec_find_decoder 用于查找一个能够解码 H.264 的 AVCodec
     * 返回的是 FFmpeg 内置的 h264 解码器（底层包含 libavcodec）
     * 如果返回空指针，说明你的 FFmpeg 缺少 H.264 支持（编译时没启用）
    */
    _codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!_codec) {
        qWarning() << "H264Decoder: codec not found";
        return false;
    }
    /*
     * 解码器上下文用来保存关于解码器的各种配置与状态
     * 如：分辨率、像素格式、线程等等
     * 解码整个视频都需要这个对象，一定要保留到销毁时释放
    */
    _codecCtx = avcodec_alloc_context3(_codec);
    if (!_codecCtx) {
        qWarning() << "H264Decoder: alloc context failed";
        return false;
    }

    /*
     * 可选设置
     * H.264 解码默认可以开启多线程（比如 4 线程）
     * 设置 thread_count = 1 表示强制单线程解码
     * 在实时流（如 RTSP、RTP）中，单线程的延迟更低
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
                         const std::function<void(const YUVFrame &)> &onFrame)
{
    if (!_ok || !data || len <= 0) return false;

    // 1. 先释放之前的包资源（确保初始状态干净）
    av_packet_unref(_packet);
    // 2. 深拷贝数据到AVPacket（堆分配，由 packet 管理））
    uint8_t *buf = (uint8_t *)av_malloc(len);
    if (!buf) {
       qWarning() << "H264Decoder: av_malloc failed";
       return false;
   }
    memcpy(buf, data, len);
    int ret = av_packet_from_data(_packet, buf, len);

    if (ret < 0) {
        qWarning() << "H264Decoder: av_packet_from_data failed";
        av_free(buf);
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
        //计算 YUV420P 各分量的真实尺寸
        int w = _frame->width;
        int h = _frame->height;
        int y_stride_src  = _frame->linesize[0];
        int u_stride_src  = _frame->linesize[1];
        int v_stride_src  = _frame->linesize[2];
        int uv_w = w / 2;
        int uv_h = h / 2;
        // 对齐后的总大小：行对齐宽度 × 高度
        int y_size = w * h;
        int u_size = uv_w * uv_h;
        int v_size = uv_w * uv_h;
        int total  = y_size + u_size + v_size;

        // 7. 分配内存并拷贝数据（优化逐行拷贝，减少循环）
        QSharedPointer<QByteArray> yuv_data(new QByteArray());
        yuv_data->resize(total);
        uint8_t* dst = (uint8_t*)yuv_data->data();

        uint8_t* dst_y = dst;
        uint8_t* dst_u = dst + y_size;
        uint8_t* dst_v = dst + y_size + u_size;

        // Y
        for (int i = 0; i < h; ++i)
            memcpy(dst_y + i*w, _frame->data[0] + i*y_stride_src, w);

        // U
        for (int i = 0; i < uv_h; ++i)
            memcpy(dst_u + i*uv_w, _frame->data[1] + i*u_stride_src, uv_w);

        // V
        for (int i = 0; i < uv_h; ++i)
            memcpy(dst_v + i*uv_w, _frame->data[2] + i*v_stride_src, uv_w);

        // 8. 构造 YUVFrame（补充真实的 linesize，便于渲染）
        YUVFrame frame;
        frame.data = yuv_data;
        frame.width = w;
        frame.height = h;
        frame.yLinesize = w;       // 有效行宽（渲染用）
        frame.uLinesize = uv_w;// 有效行宽
        frame.vLinesize = uv_w;
        frame.ySize = y_size;      // 对齐后的总大小
        frame.uSize = u_size;
        frame.vSize = v_size;

        //执行回调
        if (onFrame) {
            onFrame(frame);  // 隐式共享，无需 copy
        }
        // 4. 关键：每帧接收完成后，必须释放 AVFrame 资源（避免堆累积）
        av_frame_unref(_frame);
    }
    // 5. 发送成功后，释放 packet 持有的内存
    av_packet_unref(_packet);
    return true;
}
