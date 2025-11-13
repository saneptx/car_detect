#ifndef RTP_H264_MP4_RECORDER_H
#define RTP_H264_MP4_RECORDER_H

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <mutex>
#include <array>

#ifdef __cplusplus
extern "C" {
#endif
#include <libavformat/avformat.h>   // FFmpeg 封装格式操作接口
#ifdef __cplusplus
}
#endif

//-------------------------类型定义--------------------------------
typedef std::vector<uint8_t> ByteBuffer;        // 存储字节数据的容器
typedef std::vector<ByteBuffer> ByteBufferList; // 存储多个 ByteBuffer（多帧）

//=================================================================
//  RtpH264Mp4Recorder：将H264的RTP包解析并写入MP4文件
//=================================================================
class RtpH264Mp4Recorder {
public:
    explicit RtpH264Mp4Recorder(std::string outputPath);
    ~RtpH264Mp4Recorder();

    // 处理单个 RTP 包（传入RTP数据指针与长度）
    void handleRtpPacket(const uint8_t* data, size_t length);

    // 关闭录制器并写入尾部信息
    void close();

private:
    //-------------------------内部结构体--------------------------------
    struct SpsInfo {
        int width = 0;   // 从SPS中解析出的视频宽度
        int height = 0;  // 从SPS中解析出的高度
    };

    //-------------------------私有函数--------------------------------
    void processNalUnit(const uint8_t* data, size_t size);  // 处理单个NAL单元
    void flushAccessUnit(uint32_t timestamp);               // 将一个完整帧写入文件
    bool ensureMuxer();                                     // 检查/初始化复用器
    bool initMuxer();                                       // 初始化FFmpeg复用器
    void writeFrame(uint64_t timestamp90k);                 // 写入一帧数据
    void appendNal(const uint8_t* data, size_t size);       // 添加一个NAL到当前帧缓存
    void updateParameterSets(const uint8_t* data, size_t size); // 更新SPS/PPS
    bool containsIdr() const;                               // 判断当前帧是否包含IDR帧
    static ByteBuffer removeEmulationBytes(const uint8_t* data, size_t size); // 去除防竞争字节
    static uint32_t readUE(const ByteBuffer& rbsp, size_t& bitOffset);        // 读无符号Exp-Golomb码
    static int32_t readSE(const ByteBuffer& rbsp, size_t& bitOffset);         // 读有符号Exp-Golomb码
    static bool parseSps(const uint8_t* data, size_t size, SpsInfo& info);    // 从SPS中解析视频宽高
    bool isDuplicateSeq(uint16_t seq) const;                // 检查重复RTP序号
    void rememberSequence(uint16_t seq);                    // 记录上一个RTP序号
    uint64_t normalizeTimestamp(uint32_t timestamp);        // 处理RTP时间戳回绕问题

private:
    //-------------------------成员变量--------------------------------
    std::string _outputPath;        // 输出文件路径
    std::mutex _mutex;              // 多线程锁保护
    bool _muxerReady = false;       // FFmpeg复用器是否已初始化
    bool _headerWritten = false;    // MP4头部是否已写
    bool _hasSps = false;           // 是否接收到SPS
    bool _hasPps = false;           // 是否接收到PPS

    AVFormatContext* _formatCtx = nullptr; // FFmpeg封装上下文
    AVStream* _videoStream = nullptr;      // 视频流指针

    ByteBuffer _sps;   // 存储SPS数据
    ByteBuffer _pps;   // 存储PPS数据
    SpsInfo _spsInfo;  // 视频分辨率信息
    bool _hasSpsInfo = false;

    ByteBuffer _auBuffer;       // 一个Access Unit（完整帧）的缓存
    ByteBuffer _fragmentBuffer; // 存放分片FU-A的临时缓冲
    bool _currentAccessUnitHasIdr = false; // 当前帧是否包含IDR帧

    uint16_t _expectedSeq = 0;   // 期望的下一个RTP序号
    bool _hasSeq = false;        // 是否已收到首包
    uint16_t _lastSeq = 0;       // 上一个序号
    bool _hasLastSeq = false;
    uint32_t _lastRtpTimestamp = 0;   // 上次的RTP时间戳
    uint64_t _wrapOffset = 0;         // RTP时间戳回绕偏移
    uint64_t _lastExtendedTimestamp = 0; // 扩展时间戳
    bool _hasTimestamp = false;

    static const AVRational kInputTimeBase; // 时间基 1/90000，对应RTP时钟
};

#endif