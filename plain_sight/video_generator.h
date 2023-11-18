#ifndef _INCLUDE_VIDEO_GENERATOR_H_
#define _INCLUDE_VIDEO_GENERATOR_H_

#include <vector>
#include <cstdint>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>

namespace net_zelcon::plain_sight {
    struct QRCodeFrame {
        std::vector<uint8_t> data;
        uint64_t width;
        uint64_t height;
    };
}


#endif // _INCLUDE_VIDEO_GENERATOR_H_