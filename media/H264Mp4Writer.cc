#include "H264Mp4Writer.h"
#include <cstring>
#include <iostream>

H264Mp4Writer::H264Mp4Writer(const std::string& path)
    : _outputPath(path) {
    avformat_network_init();
}

H264Mp4Writer::~H264Mp4Writer() {
    close();
}

bool H264Mp4Writer::initMuxer(const std::vector<uint8_t>& sps, const std::vector<uint8_t>& pps) {
    if (avformat_alloc_output_context2(&_fmtCtx, nullptr, nullptr, _outputPath.c_str()) < 0)
        return false;

    _videoStream = avformat_new_stream(_fmtCtx, nullptr);
    auto* codecpar = _videoStream->codecpar;
    codecpar->codec_id = AV_CODEC_ID_H264;
    codecpar->codec_type = AVMEDIA_TYPE_VIDEO;

    const size_t extradataSize = 6 + 2 + sps.size() + 3 + pps.size();
    codecpar->extradata = (uint8_t*)av_mallocz(extradataSize);
    codecpar->extradata_size = extradataSize;
    uint8_t* p = codecpar->extradata;
    *p++ = 1; *p++ = sps[1]; *p++ = sps[2]; *p++ = sps[3]; *p++ = 0xFF; *p++ = 0xE1;
    *p++ = sps.size() >> 8; *p++ = sps.size() & 0xFF;
    memcpy(p, sps.data(), sps.size()); p += sps.size();
    *p++ = 1; *p++ = pps.size() >> 8; *p++ = pps.size() & 0xFF;
    memcpy(p, pps.data(), pps.size());

    if (!( _fmtCtx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&_fmtCtx->pb, _outputPath.c_str(), AVIO_FLAG_WRITE) < 0)
            return false;
    }

    if (avformat_write_header(_fmtCtx, nullptr) < 0)
        return false;

    _muxerReady = true;
    std::cout << "MP4 initialized: " << _outputPath << std::endl;
    return true;
}

bool H264Mp4Writer::writeFrame(const std::vector<uint8_t>& h264Frame, uint64_t ts90k) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_muxerReady) {
        // 查找SPS/PPS
        for (size_t i = 0; i + 4 < h264Frame.size();) {
            uint32_t size = (h264Frame[i]<<24)|(h264Frame[i+1]<<16)|(h264Frame[i+2]<<8)|h264Frame[i+3];
            uint8_t nalType = h264Frame[i+4] & 0x1F;
            if (nalType == 7) _sps.assign(h264Frame.begin()+i+4, h264Frame.begin()+i+4+size);
            else if (nalType == 8) _pps.assign(h264Frame.begin()+i+4, h264Frame.begin()+i+4+size);
            i += 4 + size;
        }
        if (!_sps.empty() && !_pps.empty())
            initMuxer(_sps, _pps);
    }
    if (!_muxerReady) return false;

    AVPacket pkt;
    av_init_packet(&pkt);
    av_new_packet(&pkt, h264Frame.size());
    memcpy(pkt.data, h264Frame.data(), h264Frame.size());
    pkt.stream_index = _videoStream->index;
    pkt.pts = pkt.dts = ts90k;
    av_interleaved_write_frame(_fmtCtx, &pkt);
    av_packet_unref(&pkt);
    return true;
}

void H264Mp4Writer::close() {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_fmtCtx) {
        av_write_trailer(_fmtCtx);
        if (_fmtCtx->pb)
            avio_closep(&_fmtCtx->pb);
        avformat_free_context(_fmtCtx);
        _fmtCtx = nullptr;
        _videoStream = nullptr;
    }
}
