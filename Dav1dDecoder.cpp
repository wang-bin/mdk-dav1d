/*
 * Copyright (c) 2021-2023 WangBin <wbsecg1 at gmail.com>
 * This file is part of MDK
 * MDK SDK: https://github.com/wang-bin/mdk-sdk
 * Free for opensource softwares or non-commercial use.
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 */
// properties: tile_threads=0(default)/N, frame_threads=0(default)/N
#include "mdk/VideoDecoder.h"
#if __has_include("dav1d/dav1d.h")
#include "dav1d/dav1d.h"
#include "mdk/MediaInfo.h"
#include "mdk/Packet.h"
#include "mdk/VideoFrame.h"
#include <cmath>
#include <iostream>
#include <thread>
#include "base/log.h"
using namespace std;

// api 6.x defines DAV1D_MAX_THREADS only
#if (DAV1D_API_VERSION_MAJOR + 0) > 5
#define DAV1D_MAX_FRAME_THREADS 256
#define DAV1D_MAX_TILE_THREADS 64
#define DAV1D_MAX_POSTFILTER_THREADS 256
#endif
MDK_NS_BEGIN

// bpc = 8, 10 no 12
static PixelFormat from(Dav1dPixelLayout dp, const Dav1dSequenceHeader* hdr)
{
    if (dp == DAV1D_PIXEL_LAYOUT_I444 && hdr->mtrx == DAV1D_MC_IDENTITY &&
        hdr->pri  == DAV1D_COLOR_PRI_BT709 && hdr->trc  == DAV1D_TRC_SRGB) {
        const PixelFormat gbrp[] = { PixelFormat::GBRP, PixelFormat::GBRP10LE, PixelFormat::GBRP12LE };
        return gbrp[hdr->hbd];
    }
    const PixelFormat gray[] = { PixelFormat::GRAY, PixelFormat::GRAY10LE, PixelFormat::GRAY12LE };
    const PixelFormat i420[] = { PixelFormat::YUV420P, PixelFormat::YUV420P10LE, PixelFormat::YUV420P12LE };
    const PixelFormat i422[] = { PixelFormat::YUV422P, PixelFormat::YUV422P10LE, PixelFormat::YUV422P12LE };
    const PixelFormat i444[] = { PixelFormat::YUV444P, PixelFormat::YUV444P10LE, PixelFormat::YUV444P12LE };
    switch (dp)
    {
    case DAV1D_PIXEL_LAYOUT_I400: return gray[hdr->hbd];
    case DAV1D_PIXEL_LAYOUT_I420: return i420[hdr->hbd];
    case DAV1D_PIXEL_LAYOUT_I422: return i422[hdr->hbd];
    case DAV1D_PIXEL_LAYOUT_I444: return i444[hdr->hbd];
    default:
        break;
    }
    return PixelFormat::Unknown;
}

struct Dav1dDataDeleter {
    void operator()(void* x) const {
        auto* data = static_cast<Dav1dData*>(x);
        dav1d_data_unref(data);
        delete data;
    }
};

struct Dav1dPictureDeleter {
    void operator()(void* x) const {
        auto* pic = static_cast<Dav1dPicture*>(x);
        dav1d_picture_unref(pic);
        delete pic;
    }
};

using Dav1dDataPtr = unique_ptr<Dav1dData, Dav1dDataDeleter>;
using Dav1dPicturePtr = unique_ptr<Dav1dPicture, Dav1dPictureDeleter>;


class PicturePlaneBuffer final : public Buffer2D {
    const Byte* const data_; // not writable, so const Byte*
    size_t size_;
    size_t stride_;
    shared_ptr<Dav1dPicture> bufs_;
public:
    PicturePlaneBuffer(const uint8_t* data, size_t size, int stride, shared_ptr<Dav1dPicture> ref)
        : data_(data), size_(size), stride_(stride), bufs_(ref)
    {}
    const Byte* constData() const override {return data_;}
    size_t size() const override { return size_;}
    size_t stride() const override { return stride_;}
};

static VideoFrame from(const shared_ptr<Dav1dPicture>& picref)
{
    const VideoFormat fmt = from(picref->p.layout, picref->seq_hdr);
    VideoFrame frame(picref->p.w, picref->p.h, fmt);
    for (int i = 0; i < fmt.planeCount(); ++i) {
        auto pitch = picref->stride[std::min<int>(i, 1)];
        const size_t bytes = pitch*fmt.height(picref->p.h, i);
        frame.addBuffer(make_shared<PicturePlaneBuffer>((const uint8_t*)picref->data[i], bytes, pitch, picref));
    }
    frame.setTimestamp(double(picref->m.timestamp)/FrameTimeScaleForInt);
    // optional because will set by FrameReader
    ColorSpace cs;
    cs.primaries = ColorSpace::Primary(picref->seq_hdr->pri);
    cs.transfer = ColorSpace::Transfer(picref->seq_hdr->trc);
    cs.matrix = ColorSpace::Matrix(picref->seq_hdr->mtrx);
    cs.range = picref->seq_hdr->color_range ? ColorSpace::Range::Full : ColorSpace::Range::Limited;
    frame.setColorSpace(cs, true);
    return frame;
}

class Dav1dDecoder final : public VideoDecoder
{
public:
    const char* name() const override {return "dav1d";}
    bool open() override;
    bool close() override {
        dav1d_close(&ctx_);
        onClose();
        return true;
    }
    bool flush() override;
    int decode(const Packet& pkt) override;
private:
    Dav1dDataPtr data_;
    Dav1dContext *ctx_ = nullptr;
};

bool Dav1dDecoder::open()
{
    const auto ver = dav1d_version();
    clog << LogLevel::Debug << fmt::to_string("dav1d api build version: %d.%d.%d, runtime abi version: %s", DAV1D_API_VERSION_MAJOR, DAV1D_API_VERSION_MINOR, DAV1D_API_VERSION_PATCH, ver ? ver : "?") << endl;
    if (!ver)
        return false;
    const auto& par = parameters();
    if (par.codec != "av1")
        return false;
    Dav1dSettings s;
    dav1d_default_settings(&s);
    s.logger = {nullptr, [](void* cookie, const char* fmt, va_list vl) {
        // v*printf/va_arg may modify(e.g. macOS) va_list. for (,...), always use va_start()/va_end() around v*printf. for ( va_list), use va_copy/va_end (since c99 or c++11) if va will be used more then once
        va_list tmp;
        va_copy(tmp, vl);
        std::string vamsg(std::vsnprintf(nullptr, 0, fmt, tmp) + 1, 0);
        std::vsnprintf(&vamsg[0], vamsg.size(), fmt, vl);
        va_end(tmp); // required
        if (vamsg.back() == '\n')
            vamsg.pop_back();
        clog << "dav1d: " << vamsg << endl;
    }};
    const int major = dav1d_version()[0] - '0';
    int threads = std::stoi(property("threads", "0"));
    if (major > 0) {
        auto s1 = (int*)&s;
        auto& n_threads = s1[0];
        n_threads = threads; // 0 is cpu cores
        //auto& max_frame_delay = s1[1]; // 1: low latency
    } else {
        auto s0 = (int*)&s;
        if (threads <= 0)
            threads = thread::hardware_concurrency();
        auto& n_tile_threads = s0[1];
        n_tile_threads = std::stoi(property("tile_threads", "0"));
        if (n_tile_threads <= 0)
            n_tile_threads = std::min<int>((int)floor(sqrt(threads)), DAV1D_MAX_TILE_THREADS);
        auto& n_frame_threads = s0[0];
        n_frame_threads = std::stoi(property("frame_threads", "0"));
        if (n_frame_threads <= 0)
            n_frame_threads = std::min<int>(ceil(threads/n_tile_threads), DAV1D_MAX_FRAME_THREADS);
        clog << fmt::to_string("frame threads: %d, tile threads: %d", n_frame_threads, n_tile_threads) << endl;
    }
    // dav1d_parse_sequence_header
    if (dav1d_open(&ctx_, &s) < 0) {
        return false;
    }
    data_.reset(new Dav1dData{});
    onOpen();
    return true;
}

bool Dav1dDecoder::flush()
{
    dav1d_flush(ctx_);
    data_.reset(new Dav1dData{});
    onFlush();
    return true;
}

int Dav1dDecoder::decode(const Packet& pkt)
{
    if (!data_->sz && !pkt.isEnd()) {
        auto pbuf = new BufferRef(pkt.buffer);
        if (dav1d_data_wrap(data_.get(), pkt.buffer->data(), pkt.buffer->size()
            , [](const uint8_t *buf, void *cookie){
                auto pbuf = static_cast<BufferRef*>(cookie);
                delete pbuf;
            }, pbuf) < 0) {
                delete pbuf;
                return -3;
            }
        data_->m.timestamp = int64_t(pkt.pts*FrameTimeScaleForInt);
    }
    auto ret = dav1d_send_data(ctx_, data_.get());
    if (ret < 0 && ret != -EAGAIN)
        return -1;
    do {
        Dav1dPicturePtr pic(new Dav1dPicture{});
        ret = dav1d_get_picture(ctx_, pic.get());
        if (ret == 0) {
            const shared_ptr<Dav1dPicture> picref(std::move(pic));
            frameDecoded(from(picref));
        }
    } while(ret == 0);
    if (ret < 0 && ret != -EAGAIN)
        return -2;
    return !pkt.isEnd() ? data_->sz : INT_MAX;
}

static void register_video_decoders_dav1d() {
    VideoDecoder::registerOnce("dav1d", []{return new Dav1dDecoder();});
}
MDK_NS_END
#else
static void register_video_decoders_dav1d() {}
#endif // __has_include("dav1d/dav1d.h")

// project name must be dav1d or mdk-dav1d
MDK_PLUGIN(dav1d) {
    using namespace MDK_NS;
    register_video_decoders_dav1d();
    return MDK_ABI_VERSION;
}