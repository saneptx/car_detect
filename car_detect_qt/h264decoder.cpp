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
//        qWarning() << "H264Decoder: avcodec_send_packet failed:" << ret;
//        av_packet_unref(_packet); // 失败时释放包
//        return false;
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
        int w = _frame->width;
        int h = _frame->height;
        int w2 = w / 2;
        // 注意：I422 格式的 U/V 高度仍为 h，不是 h/2
        int h_uv = h;
        int ySize_aligned = (w * h + 3) & ~3;
        // I422 U/V 尺寸：(W/2) * H
        int uvSize = w2 * h_uv;
        int uSize_aligned = (uvSize + 3) & ~3;
        int vSize_aligned = (uvSize + 3) & ~3;
        int totalSize = ySize_aligned + uSize_aligned + vSize_aligned;

        // 分配共享内存
        QSharedPointer<QByteArray> yuvData(new QByteArray());
        yuvData->resize(totalSize);
        uint8_t *dest = (uint8_t*)yuvData->data();

        // 内存拷贝: Y 分量
        for (int i = 0; i < h; ++i) {
            memcpy(dest + i * w, _frame->data[0] + i * _frame->linesize[0], w);
        }

        // 内存拷贝: U 分量 (放在第二个 chunk 的位置)
        // 使用 _frame->data[1] (U) 填充第三个平面
        uint8_t *destU_chunk = dest + ySize_aligned;
        for (int i = 0; i < h_uv; ++i) {
            // 每行拷贝 w2 字节
            memcpy(destU_chunk + i * w2, _frame->data[1] + i * _frame->linesize[1], w2);
        }

        // 内存拷贝: V 分量 (放在第三个 chunk 的位置)
        // 使用 _frame->data[2] (V) 填充第二个平面
        // 注意：起始位置使用对齐后的 ySize_aligned
        uint8_t *destV_chunk = dest + ySize_aligned + uSize_aligned;
        for (int i = 0; i < h_uv; ++i) {
            // 每行拷贝 w2 字节
            memcpy(destV_chunk + i * w2, _frame->data[2] + i * _frame->linesize[2], w2);
        }
        // 构造 YUVFrame 结构体
        YUVFrame frame;
        frame.data = yuvData;
        frame.width = w;
        frame.height = h;
        // 传递紧凑的 linesize
        frame.yLinesize = w;
        frame.uLinesize = w2;
        frame.vLinesize = w2;

        // 更新为对齐后的大小，以便 videoopenglwidget 计算正确的偏移
        frame.ySize = ySize_aligned;
        frame.uSize = uSize_aligned; // 对应 U chunk 的大小
        frame.vSize = vSize_aligned; // 对应 V chunk 的大小
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
