#include "RtpH264Mp4Recorder.h"
#include "Logger.h"

#include <algorithm>
#include <stdexcept>
#include <cstring>

//------------------- 匿名命名空间：用于只初始化一次 FFmpeg 网络库 -------------------
namespace {
std::once_flag g_ffmpegInitFlag;
}

// RTP 时间基：1/90000 秒（标准视频时钟）
const AVRational RtpH264Mp4Recorder::kInputTimeBase = {1, 90000};

//=====================================================================
// 构造函数
//=====================================================================
RtpH264Mp4Recorder::RtpH264Mp4Recorder(std::string outputPath)
    : _outputPath(std::move(outputPath)) {
    // FFmpeg 全局网络初始化（仅一次）
    std::call_once(g_ffmpegInitFlag, []() {
        avformat_network_init();
    });
}

//=====================================================================
// 析构函数：确保关闭
//=====================================================================
RtpH264Mp4Recorder::~RtpH264Mp4Recorder() {
    close();
}

//=====================================================================
// 关闭文件并释放资源
//=====================================================================
void RtpH264Mp4Recorder::close() {
    std::lock_guard<std::mutex> lock(_mutex);

    if (_formatCtx) {
        // 写入MP4尾部信息（moov box）
        av_write_trailer(_formatCtx);

        // 关闭文件IO句柄
        if (_formatCtx->pb) {
            avio_closep(&_formatCtx->pb);
        }

        // 释放extradata（SPS/PPS）
        if (_videoStream && _videoStream->codecpar) {
            if (_videoStream->codecpar->extradata) {
                av_freep(&_videoStream->codecpar->extradata);
                _videoStream->codecpar->extradata_size = 0;
            }
        }

        // 释放封装上下文
        avformat_free_context(_formatCtx);
        _formatCtx = nullptr;
        _videoStream = nullptr;
    }

    // 状态重置
    _muxerReady = false;
    _headerWritten = false;
    _auBuffer.clear();
    _fragmentBuffer.clear();
    _currentAccessUnitHasIdr = false;
}

//=====================================================================
// 📘 功能：接收一个 RTP 包并解析其中的 H.264 数据
//=====================================================================
void RtpH264Mp4Recorder::handleRtpPacket(const uint8_t* data, size_t length) {
    if (!data || length < 12) { // RTP头最少12字节
        return;
    }

    std::lock_guard<std::mutex> lock(_mutex);

    // RTP 固定头部解析
    uint8_t vpxcc = data[0];
    uint8_t mpayload = data[1];

    // 检查版本号（2）
    if (((vpxcc >> 6) & 0x3) != 2) {
        return;
    }

    bool marker = (mpayload & 0x80) != 0; // marker 位：帧结束标志
    uint8_t payloadType = mpayload & 0x7F; // RTP负载类型（一般96）
    (void)payloadType;

    // 序列号
    uint16_t seq = (static_cast<uint16_t>(data[2]) << 8) | data[3];

    // 时间戳（90kHz）
    uint32_t timestamp = (static_cast<uint32_t>(data[4]) << 24) |
                         (static_cast<uint32_t>(data[5]) << 16) |
                         (static_cast<uint32_t>(data[6]) << 8) |
                         static_cast<uint32_t>(data[7]);

    // 检查是否重复包
    if (isDuplicateSeq(seq)) {
        LOG_DEBUG("Duplicate RTP sequence detected: %u, dropping packet", seq);
        return;
    }
    rememberSequence(seq); // 记录上一次序号

    // RTP CSRC与扩展头长度计算
    uint8_t cc = vpxcc & 0x0F;    // CSRC 数量
    bool extension = (vpxcc & 0x10) != 0;
    size_t offset = 12 + cc * 4;  // 跳过CSRC部分
    if (extension) {
        // 若有扩展头，进一步跳过
        if (length < offset + 4) return;
        uint16_t extLen = (static_cast<uint16_t>(data[offset + 2]) << 8) | data[offset + 3];
        offset += 4 + extLen * 4;
    }
    if (offset >= length) return;

    // 指向负载部分
    size_t payloadSize = length - offset;
    const uint8_t* payload = data + offset;
    if (payloadSize == 0) return;

    // 检查序列连续性
    if (_hasSeq) {
        if (static_cast<uint16_t>(_expectedSeq + 1) != seq) {
            LOG_WARN("RTP sequence discontinuity detected: expected %u, got %u", _expectedSeq + 1, seq);
        }
    }
    _expectedSeq = seq;
    _hasSeq = true;

    // H.264 NAL 类型
    uint8_t nalType = payload[0] & 0x1F;

    //===================== 各种NAL类型处理 =====================//
    if (nalType >= 1 && nalType <= 23) {
        // 单个NAL（普通帧）
        processNalUnit(payload, payloadSize);
    } else if (nalType == 24) { 
        // STAP-A: 聚合包，包含多个NAL
        size_t stapOffset = 1;
        while (stapOffset + 2 <= payloadSize) {
            uint16_t nalLen = (payload[stapOffset] << 8) | payload[stapOffset + 1];
            stapOffset += 2;
            if (stapOffset + nalLen > payloadSize) break;
            processNalUnit(payload + stapOffset, nalLen);
            stapOffset += nalLen;
        }
    } else if (nalType == 28) {
        // FU-A 分片包（Fragmented Unit）
        if (payloadSize < 2) return;
        uint8_t fuHeader = payload[1];
        bool fuStart = (fuHeader & 0x80) != 0; // 起始片
        bool fuEnd   = (fuHeader & 0x40) != 0; // 结束片
        uint8_t reconstructedNal = (payload[0] & 0xE0) | (fuHeader & 0x1F);
        const uint8_t* fuPayload = payload + 2;
        size_t fuPayloadSize = payloadSize - 2;

        // 起始分片时，创建新缓冲
        if (fuStart) {
            _fragmentBuffer.clear();
            _fragmentBuffer.push_back(reconstructedNal);
        }

        if (_fragmentBuffer.empty()) return;

        // 拼接分片数据
        _fragmentBuffer.insert(_fragmentBuffer.end(), fuPayload, fuPayload + fuPayloadSize);

        // 结束时组装成完整NAL
        if (fuEnd) {
            processNalUnit(_fragmentBuffer.data(), _fragmentBuffer.size());
            _fragmentBuffer.clear();
        }
    }

    // marker=1 表示该帧结束
    if (marker) {
        flushAccessUnit(timestamp);
    }
}

//=====================================================================
// 📘 功能：解析单个NAL单元并识别SPS/PPS
//=====================================================================
void RtpH264Mp4Recorder::processNalUnit(const uint8_t* data, size_t size) {
    if (!data || size == 0) return;

    uint8_t nalType = data[0] & 0x1F;

    // SPS
    if (nalType == 7) {
        updateParameterSets(data, size);
        if (parseSps(data, size, _spsInfo)) _hasSpsInfo = true;
    }
    // PPS
    else if (nalType == 8) {
        updateParameterSets(data, size);
    }

    // 添加NAL到当前AU缓冲
    appendNal(data, size);
}

//=====================================================================
// 将NAL附加到当前AU缓冲中
//=====================================================================
void RtpH264Mp4Recorder::appendNal(const uint8_t* data, size_t size) {
    // 先写4字节长度前缀（MP4封装需要）
    uint32_t nalSize = static_cast<uint32_t>(size);
    _auBuffer.push_back((nalSize >> 24) & 0xFF);
    _auBuffer.push_back((nalSize >> 16) & 0xFF);
    _auBuffer.push_back((nalSize >> 8) & 0xFF);
    _auBuffer.push_back(nalSize & 0xFF);

    // 再写入NAL内容
    _auBuffer.insert(_auBuffer.end(), data, data + size);

    // 若为IDR帧（关键帧），标记
    if (size > 0) {
        uint8_t nalType = data[0] & 0x1F;
        if (nalType == 5) _currentAccessUnitHasIdr = true;
    }
}

//=====================================================================
// 📘 功能：一帧结束时写入文件
//=====================================================================
void RtpH264Mp4Recorder::flushAccessUnit(uint32_t timestamp) {
    if (_auBuffer.empty()) return;
    if (!ensureMuxer()) { // 若未准备好MP4复用器
        _auBuffer.clear();
        _currentAccessUnitHasIdr = false;
        return;
    }

    // RTP时间戳标准化（防止回绕）
    uint64_t normalizedTs = normalizeTimestamp(timestamp);

    // 写入一帧
    writeFrame(normalizedTs);

    // 清理缓存
    _auBuffer.clear();
    _currentAccessUnitHasIdr = false;
}

//=====================================================================
// 若MP4复用器还没初始化，则在接收到SPS+PPS后初始化
//=====================================================================
bool RtpH264Mp4Recorder::ensureMuxer() {
    if (_muxerReady) return true;
    if (_hasSps && _hasPps) {
        _muxerReady = initMuxer();
    }
    return _muxerReady;
}

//=====================================================================
// 📘 功能：初始化FFmpeg输出MP4文件
//=====================================================================
bool RtpH264Mp4Recorder::initMuxer() {
    if (_formatCtx) return true;

    // 创建输出上下文
    if (avformat_alloc_output_context2(&_formatCtx, nullptr, nullptr, _outputPath.c_str()) < 0 || !_formatCtx) {
        LOG_ERROR("Failed to allocate output context for %s", _outputPath.c_str());
        return false;
    }

    // 新建视频流
    _videoStream = avformat_new_stream(_formatCtx, nullptr);
    if (!_videoStream) {
        LOG_ERROR("Failed to create video stream");
        return false;
    }
    _videoStream->time_base = kInputTimeBase;

    // 设置编码参数
    AVCodecParameters* codecpar = _videoStream->codecpar;
    codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    codecpar->codec_id = AV_CODEC_ID_H264;
    codecpar->codec_tag = 0;

    if (_hasSpsInfo) {
        codecpar->width = _spsInfo.width;
        codecpar->height = _spsInfo.height;
    }

    // 检查SPS/PPS有效性
    if (_sps.size() < 4 || _pps.empty()) {
        LOG_ERROR("Invalid SPS (%zu bytes) or PPS (%zu bytes) for MP4 muxer",
                  _sps.size(), _pps.size());
        return false;
    }

    // 构造 extradata (AVCDecoderConfigurationRecord)
    const size_t extradataSize = 6 + 2 + _sps.size() + 3 + _pps.size();
    codecpar->extradata = static_cast<uint8_t*>(av_mallocz(extradataSize));
    if (!codecpar->extradata) {
        LOG_ERROR("Failed to allocate extradata");
        return false;
    }
    codecpar->extradata_size = static_cast<int>(extradataSize);

    // 写入 AVC 配置头
    uint8_t* p = codecpar->extradata;
    const uint8_t* sps = _sps.data();
    const uint8_t* pps = _pps.data();
    *p++ = 1;
    *p++ = sps[1];
    *p++ = sps[2];
    *p++ = sps[3];
    *p++ = 0xFF;
    *p++ = 0xE1;
    *p++ = (_sps.size() >> 8);
    *p++ = (_sps.size() & 0xFF);
    std::memcpy(p, sps, _sps.size());
    p += _sps.size();
    *p++ = 1;
    *p++ = (_pps.size() >> 8);
    *p++ = (_pps.size() & 0xFF);
    std::memcpy(p, pps, _pps.size());

    // 打开输出文件
    if (!(_formatCtx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&_formatCtx->pb, _outputPath.c_str(), AVIO_FLAG_WRITE) < 0) {
            LOG_ERROR("Failed to open output file %s", _outputPath.c_str());
            return false;
        }
    }

    // 写文件头
    if (avformat_write_header(_formatCtx, nullptr) < 0) {
        LOG_ERROR("Failed to write format header for %s", _outputPath.c_str());
        return false;
    }

    _headerWritten = true;
    LOG_INFO("RTP recorder writing to %s", _outputPath.c_str());
    return true;
}

//=====================================================================
// 📘 功能：将组好的帧写入 MP4 文件
//=====================================================================
void RtpH264Mp4Recorder::writeFrame(uint64_t timestamp90k) {
    if (!_formatCtx || !_videoStream) return;
    if (_auBuffer.empty()) return;

    AVPacket packet;
    av_init_packet(&packet);
    packet.stream_index = _videoStream->index;

    // 分配并拷贝帧数据
    if (av_new_packet(&packet, static_cast<int>(_auBuffer.size())) < 0) {
        LOG_ERROR("Failed to allocate AVPacket of size %zu", _auBuffer.size());
        return;
    }
    std::memcpy(packet.data, _auBuffer.data(), _auBuffer.size());

    // 设置时间戳
    packet.pts = packet.dts = static_cast<int64_t>(timestamp90k);
    packet.flags = containsIdr() ? AV_PKT_FLAG_KEY : 0;
    packet.duration = 0;

    // 时间基转换
    av_packet_rescale_ts(&packet, kInputTimeBase, _videoStream->time_base);

    // 写入帧
    int ret = av_interleaved_write_frame(_formatCtx, &packet);
    if (ret < 0) {
        LOG_ERROR("Failed to write packet: %d", ret);
    }
    av_packet_unref(&packet);
}

//=====================================================================
// 判断当前帧是否包含关键帧
//=====================================================================
bool RtpH264Mp4Recorder::containsIdr() const {
    return _currentAccessUnitHasIdr;
}

//=====================================================================
// 更新SPS和PPS缓存
//=====================================================================
void RtpH264Mp4Recorder::updateParameterSets(const uint8_t* data, size_t size) {
    if (!data || size == 0) return;
    uint8_t nalType = data[0] & 0x1F;
    if (nalType == 7) {
        if (size >= 4) {
            _sps.assign(data, data + size);
            _hasSps = true;
        } else {
            LOG_WARN("Discarding malformed SPS of size %zu", size);
        }
    } else if (nalType == 8) {
        if (size > 0) {
            _pps.assign(data, data + size);
            _hasPps = true;
        } else {
            LOG_WARN("Discarding empty PPS");
        }
    }
}
//=====================================================================
// 📘 功能：去除 H.264 NAL 中的防竞争字节 (emulation prevention bytes)
//
// 在 H.264 码流中，如果出现字节序列 0x00 00 00、0x00 00 01 等，
// 会与起始码（start code）混淆，因此编码器在比特流中插入一个 0x03。
// 解码时必须去掉它，这样才能正确解析后续比特数据。
//=====================================================================
ByteBuffer RtpH264Mp4Recorder::removeEmulationBytes(const uint8_t* data, size_t size) {
    ByteBuffer rbsp;       // 结果缓冲
    rbsp.reserve(size);    // 预留空间（避免频繁扩容）

    for (size_t i = 0; i < size; ++i) {
        // 如果检测到 00 00 03 序列，就去掉 03
        if (i + 2 < size && data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x03) {
            rbsp.push_back(0x00);
            rbsp.push_back(0x00);
            i += 2;  // 跳过 “00 00 03”
            continue;
        }
        rbsp.push_back(data[i]);
    }
    return rbsp;
}

//=====================================================================
// 📘 功能：读取无符号 Exp-Golomb 编码数 (UE)
//
// H.264 码流中大量使用 Exp-Golomb 码存储整数：
//
// 格式： 0^(k个) 1 [后跟 k 比特数据]
// 数值： N = (2^k - 1) + 后k比特值
//
// 例如：
//   00010 -> k=2, 后两位 10 = 2 → 值 = 3
//=====================================================================
uint32_t RtpH264Mp4Recorder::readUE(const ByteBuffer& rbsp, size_t& bitOffset) {
    uint32_t zeros = 0;

    // 1. 数前导 0 的个数
    while (true) {
        if (bitOffset >= rbsp.size() * 8) return 0;
        if ((rbsp[bitOffset / 8] & (0x80 >> (bitOffset % 8))) != 0) break;
        ++zeros;
        ++bitOffset;
    }

    ++bitOffset; // 跳过那一个“1”

    // 2. 读取接下来的 k 位
    uint32_t value = 1;
    for (uint32_t i = 0; i < zeros; ++i) {
        if (bitOffset >= rbsp.size() * 8) break;
        value <<= 1;
        if ((rbsp[bitOffset / 8] & (0x80 >> (bitOffset % 8))) != 0)
            value |= 1;
        ++bitOffset;
    }
    return value - 1;
}

//=====================================================================
// 📘 功能：读取有符号 Exp-Golomb 编码数 (SE)
//
// 对应关系：
//   code_num: 0,1,2,3,4,... → 值: 0,1,-1,2,-2,...
//   算法： val = ceil(code_num/2)*(-1)^(code_num+1)
//=====================================================================
int32_t RtpH264Mp4Recorder::readSE(const ByteBuffer& rbsp, size_t& bitOffset) {
    uint32_t ueVal = readUE(rbsp, bitOffset);
    int32_t val = static_cast<int32_t>((ueVal + 1) / 2);
    if ((ueVal & 1) == 0) val = -val;
    return val;
}

//=====================================================================
// 📘 功能：解析 SPS（Sequence Parameter Set）
// 提取视频的分辨率（width/height）
//
// SPS 是 H.264 中最核心的配置NAL，包含：
//  - profile_idc、level_idc
//  - 分辨率、色度采样、裁剪窗口
//=====================================================================
bool RtpH264Mp4Recorder::parseSps(const uint8_t* data, size_t size, SpsInfo& info) {
    if (!data || size < 4) return false;

    const uint8_t* payload = data + 1; // 跳过NAL头
    size_t payloadSize = size - 1;

    // 1️⃣ 去除防竞争字节
    auto rbsp = removeEmulationBytes(payload, payloadSize);
    if (rbsp.size() < 4) return false;

    size_t bitOffset = 0;

    // 2️⃣ 读取固定头部信息
    bitOffset += 8; // profile_idc
    bitOffset += 8; // constraint_flags + reserved_zero
    bitOffset += 8; // level_idc
    readUE(rbsp, bitOffset); // seq_parameter_set_id

    uint32_t chroma_format_idc = 1; // 默认YUV 4:2:0格式

    // 3️⃣ 不同profile需要额外字段
    if (rbsp[0] == 100 || rbsp[0] == 110 || rbsp[0] == 122 ||
        rbsp[0] == 244 || rbsp[0] == 44 || rbsp[0] == 83 ||
        rbsp[0] == 86 || rbsp[0] == 118 || rbsp[0] == 128 ||
        rbsp[0] == 138 || rbsp[0] == 139 || rbsp[0] == 134) {

        // 色度采样格式
        chroma_format_idc = readUE(rbsp, bitOffset);
        if (chroma_format_idc == 3) ++bitOffset; // separate_colour_plane_flag

        readUE(rbsp, bitOffset); // bit_depth_luma_minus8
        readUE(rbsp, bitOffset); // bit_depth_chroma_minus8
        ++bitOffset;             // qpprime_y_zero_transform_bypass_flag

        // 是否存在缩放矩阵
        uint32_t seq_scaling_matrix_present_flag =
            (rbsp[bitOffset / 8] >> (7 - (bitOffset % 8))) & 0x01;
        ++bitOffset;

        // 如果存在，跳过所有缩放列表
        if (seq_scaling_matrix_present_flag) {
            int scalingCount = (chroma_format_idc != 3) ? 8 : 12;
            for (int i = 0; i < scalingCount; ++i) {
                uint32_t flag = (rbsp[bitOffset / 8] >> (7 - (bitOffset % 8))) & 0x01;
                ++bitOffset;
                if (flag) {
                    int64_t lastScale = 8;
                    int64_t nextScale = 8;
                    int loops = (i < 6) ? 16 : 64;
                    for (int j = 0; j < loops; ++j) {
                        if (bitOffset >= rbsp.size() * 8) break;
                        int32_t delta_scale = readSE(rbsp, bitOffset);
                        nextScale = (lastScale + delta_scale + 256) % 256;
                        lastScale = nextScale == 0 ? lastScale : nextScale;
                    }
                }
            }
        }
    }

    // 4️⃣ 继续解析时序参数
    readUE(rbsp, bitOffset); // log2_max_frame_num_minus4
    uint32_t pic_order_cnt_type = readUE(rbsp, bitOffset);

    if (pic_order_cnt_type == 0) {
        readUE(rbsp, bitOffset); // log2_max_pic_order_cnt_lsb_minus4
    } else if (pic_order_cnt_type == 1) {
        ++bitOffset; // delta_pic_order_always_zero_flag
        readSE(rbsp, bitOffset);
        readSE(rbsp, bitOffset);
        uint32_t num_ref_frames_in_pic_order_cnt_cycle = readUE(rbsp, bitOffset);
        for (uint32_t i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; ++i)
            readSE(rbsp, bitOffset);
    }

    readUE(rbsp, bitOffset); // max_num_ref_frames
    ++bitOffset;             // gaps_in_frame_num_value_allowed_flag

    // 5️⃣ 核心：解析分辨率
    uint32_t pic_width_in_mbs_minus1  = readUE(rbsp, bitOffset);
    uint32_t pic_height_in_map_units_minus1 = readUE(rbsp, bitOffset);
    uint32_t frame_mbs_only_flag = (rbsp[bitOffset / 8] >> (7 - (bitOffset % 8))) & 0x01;
    ++bitOffset;

    if (!frame_mbs_only_flag) ++bitOffset; // mb_adaptive_frame_field_flag
    ++bitOffset; // direct_8x8_inference_flag

    // 是否存在裁剪窗口
    uint32_t frame_cropping_flag = (rbsp[bitOffset / 8] >> (7 - (bitOffset % 8))) & 0x01;
    ++bitOffset;
    uint32_t frame_crop_left_offset = 0, frame_crop_right_offset = 0;
    uint32_t frame_crop_top_offset = 0, frame_crop_bottom_offset = 0;

    if (frame_cropping_flag) {
        frame_crop_left_offset = readUE(rbsp, bitOffset);
        frame_crop_right_offset = readUE(rbsp, bitOffset);
        frame_crop_top_offset = readUE(rbsp, bitOffset);
        frame_crop_bottom_offset = readUE(rbsp, bitOffset);
    }

    // 计算最终像素分辨率
    uint32_t width  = (pic_width_in_mbs_minus1 + 1) * 16;
    uint32_t height = (pic_height_in_map_units_minus1 + 1) * 16 * (frame_mbs_only_flag ? 1 : 2);

    uint32_t cropUnitX = 1;
    uint32_t cropUnitY = 2 - frame_mbs_only_flag;

    // 根据采样格式修正裁剪单位
    if (chroma_format_idc == 1) {
        cropUnitX = 2;
        cropUnitY *= 2;
    } else if (chroma_format_idc == 2) {
        cropUnitX = 2;
    } else if (chroma_format_idc == 3) {
        cropUnitY = 2 - frame_mbs_only_flag;
    }

    width  -= (frame_crop_left_offset + frame_crop_right_offset) * cropUnitX;
    height -= (frame_crop_top_offset + frame_crop_bottom_offset) * cropUnitY;

    // 保存结果
    info.width  = static_cast<int>(width);
    info.height = static_cast<int>(height);
    return true;
}

//=====================================================================
// 📘 功能：规范化 RTP 时间戳（处理回绕）
//
// RTP 时间戳是 32 位无符号数，会溢出回绕。
// 例如 0xFFFFFFF0 + 20 -> 0x00000014
//
// 通过 _wrapOffset 追踪回绕次数，确保时间戳单调递增。
//=====================================================================
uint64_t RtpH264Mp4Recorder::normalizeTimestamp(uint32_t timestamp) {
    const uint64_t wrapSpan = 1ULL << 32;

    if (!_hasTimestamp) {
        _hasTimestamp = true;
        _lastRtpTimestamp = timestamp;
        _wrapOffset = 0;
        _lastExtendedTimestamp = timestamp;
        return _lastExtendedTimestamp;
    }

    // 检测是否回绕
    if (timestamp < _lastRtpTimestamp &&
        static_cast<uint32_t>(_lastRtpTimestamp - timestamp) > 0x80000000U) {
        _wrapOffset += wrapSpan;
    }

    uint64_t extended = _wrapOffset + timestamp;

    // 确保单调递增
    if (extended <= _lastExtendedTimestamp) {
        extended = _lastExtendedTimestamp + 1;
    }

    _lastRtpTimestamp = timestamp;
    _lastExtendedTimestamp = extended;
    return extended;
}

//=====================================================================
// 检查 RTP 包是否重复
//=====================================================================
bool RtpH264Mp4Recorder::isDuplicateSeq(uint16_t seq) const {
    return _hasLastSeq && seq == _lastSeq;
}

//=====================================================================
// 记录最后一次 RTP 序号
//=====================================================================
void RtpH264Mp4Recorder::rememberSequence(uint16_t seq) {
    _lastSeq = seq;
    _hasLastSeq = true;
}