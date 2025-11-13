#ifndef H264_MP4_WRITER_H
#define H264_MP4_WRITER_H

#include <string>
#include <vector>
#include <mutex>

extern "C" {
#include <libavformat/avformat.h>
}

class H264Mp4Writer {
public:
    explicit H264Mp4Writer(const std::string& outputPath);
    ~H264Mp4Writer();

    bool writeFrame(const std::vector<uint8_t>& h264Frame, uint64_t timestamp90k);
    void close();

private:
    bool initMuxer(const std::vector<uint8_t>& sps, const std::vector<uint8_t>& pps);

private:
    std::string _outputPath;
    std::mutex _mutex;
    AVFormatContext* _fmtCtx = nullptr;
    AVStream* _videoStream = nullptr;
    bool _muxerReady = false;
    std::vector<uint8_t> _sps, _pps;
};

#endif
