#include "veyra/source/CaptureCompressedDecoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <cstring>
#include <format>

#include "veyra/Log.h"
#include "veyra/media/FFmpegVideoDecoder.h"

namespace veyra::source {

namespace {

AVCodecID captureAvCodecId(CaptureCodec codec)
{
    switch (codec) {
    case CaptureCodec::Mjpeg: return AV_CODEC_ID_MJPEG;
    case CaptureCodec::H264: return AV_CODEC_ID_H264;
    case CaptureCodec::Hevc: return AV_CODEC_ID_HEVC;
    case CaptureCodec::Av1: return AV_CODEC_ID_AV1;
    case CaptureCodec::Vp9: return AV_CODEC_ID_VP9;
    case CaptureCodec::None: break;
    }
    return AV_CODEC_ID_NONE;
}

// swscale colour coefficients for the bitstream's declared matrix; the same
// coefficients are used for both sides so no hidden matrix conversion is
// introduced, only the range change.
int swsColorspaceOf(AVColorSpace space, unsigned height)
{
    switch (space) {
    case AVCOL_SPC_BT709: return SWS_CS_ITU709;
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M: return SWS_CS_ITU601;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL: return SWS_CS_BT2020;
    default: break;
    }
    return height > 576 ? SWS_CS_ITU709 : SWS_CS_ITU601;
}

} // namespace

struct CaptureCompressedDecoder::Impl {
    CaptureCodec codec = CaptureCodec::None;
    unsigned width = 0, height = 0;
    media::FFmpegVideoDecoder decoder;
    AVCodecParameters* parameters = nullptr;
    SwsContext* sws = nullptr;
    bool hardwareRequested = false;
    bool hardware = false;
    bool hardwareChecked = false;
    bool waiting = false;
    bool depthWarningLogged = false;
    std::string error;
    uint64_t frames = 0, failures = 0, fallbacks = 0;

    ~Impl() { close(); }

    void close()
    {
        if (sws) { sws_freeContext(sws); sws = nullptr; }
        if (parameters) { avcodec_parameters_free(&parameters); parameters = nullptr; }
        decoder.close();
        codec = CaptureCodec::None;
        width = height = 0;
        hardwareRequested = hardware = hardwareChecked = waiting = false;
        depthWarningLogged = false;
        frames = failures = fallbacks = 0;
        error.clear();
    }

    bool openSoftwareDecoder()
    {
        if (parameters == nullptr) return false;
        return decoder.openSoftware(parameters, 1, 10000000, 1, true);
    }

    bool convertToNv12(const AVFrame& frame, AVFrame* target)
    {
        AVPixelFormat sourceFormat = AVPixelFormat(frame.format);
        if (sourceFormat == AV_PIX_FMT_YUVJ422P) sourceFormat = AV_PIX_FMT_YUV422P;
        else if (sourceFormat == AV_PIX_FMT_YUVJ420P) sourceFormat = AV_PIX_FMT_YUV420P;
        else if (sourceFormat == AV_PIX_FMT_YUVJ444P) sourceFormat = AV_PIX_FMT_YUV444P;
        if (const auto* description = av_pix_fmt_desc_get(sourceFormat)) {
            if (description->comp[0].depth > 8 && !depthWarningLogged) {
                depthWarningLogged = true;
                log::warn("capture-decode", std::format(
                    "codec={} bitstream is {}-bit; converting to 8-bit NV12 because the capture ingress contract is 8-bit SDR (a P010/HDR capture contract is not implemented)",
                    captureCodecKey(codec), description->comp[0].depth));
            }
        }
        sws = sws_getCachedContext(sws, frame.width, frame.height, sourceFormat,
            int(width), int(height), AV_PIX_FMT_NV12, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (sws == nullptr) {
            error = "swscale context creation failed";
            ++failures;
            return false;
        }
        const int* coefficients = sws_getCoefficients(swsColorspaceOf(frame.colorspace, unsigned(frame.height)));
        // MJPEG samples are full-range JPEG planes and the declared ingress
        // contract for that path is full-range NV12. Compressed video keeps
        // the bitstream's own range and lands on the standard limited NV12.
        const int sourceFullRange = (codec == CaptureCodec::Mjpeg || frame.color_range == AVCOL_RANGE_JPEG) ? 1 : 0;
        const int targetFullRange = codec == CaptureCodec::Mjpeg ? 1 : 0;
        sws_setColorspaceDetails(sws, coefficients, sourceFullRange, coefficients, targetFullRange, 0, 1 << 16, 1 << 16);
        uint8_t* destination[4] = { target->data[0], target->data[1], nullptr, nullptr };
        const int destinationStride[4] = { target->linesize[0], target->linesize[1], 0, 0 };
        if (sws_scale(sws, frame.data, frame.linesize, 0, frame.height, destination, destinationStride) != frame.height) {
            error = "swscale conversion failed";
            ++failures;
            return false;
        }
        return true;
    }
};

CaptureCompressedDecoder::CaptureCompressedDecoder() : p_(std::make_unique<Impl>()) {}
CaptureCompressedDecoder::~CaptureCompressedDecoder() = default;

bool CaptureCompressedDecoder::open(CaptureCodec codec, unsigned width, unsigned height,
    const uint8_t* extradata, size_t extradataBytes, ID3D12Device* device, ID3D12CommandQueue* queue)
{
    auto& p = *p_;
    p.close();
    if (codec == CaptureCodec::None || !width || !height) return false;
    p.codec = codec;
    p.width = width;
    p.height = height;
    p.parameters = avcodec_parameters_alloc();
    if (p.parameters == nullptr) {
        p.error = "avcodec_parameters_alloc failed";
        return false;
    }
    p.parameters->codec_type = AVMEDIA_TYPE_VIDEO;
    p.parameters->codec_id = captureAvCodecId(codec);
    p.parameters->width = int(width);
    p.parameters->height = int(height);
    if (extradata != nullptr && extradataBytes != 0) {
        p.parameters->extradata = static_cast<uint8_t*>(av_mallocz(extradataBytes + AV_INPUT_BUFFER_PADDING_SIZE));
        if (p.parameters->extradata == nullptr) {
            p.error = "extradata allocation failed";
            p.close();
            return false;
        }
        std::memcpy(p.parameters->extradata, extradata, extradataBytes);
        p.parameters->extradata_size = int(extradataBytes);
    }
    // MJPEG stays software: UVC drivers disagree about 4:2:0 vs 4:2:2 and the
    // hardware path only exists for 4:2:0 on one vendor. Everything else gets
    // the D3D12VA session on the shared Veyra device, with the software
    // decoder as the recorded fallback.
    if (codec != CaptureCodec::Mjpeg && device != nullptr && queue != nullptr) {
        p.hardwareRequested = true;
        if (p.decoder.openD3D12VA(p.parameters, 1, 10000000, device, queue, true)) {
            p.hardware = true;
        } else {
            log::warn("capture-decode", std::format(
                "D3D12VA open failed codec={}; falling back to the software decoder", captureCodecKey(codec)));
        }
    }
    if (!p.hardware && !p.openSoftwareDecoder()) {
        p.error = std::format("no usable decoder for codec={}", captureCodecKey(codec));
        log::error("capture-decode", p.error);
        p.close();
        return false;
    }
    log::info("capture-decode", std::format(
        "compressed decoder opened codec={} {}x{} backend={} extradataBytes={}",
        captureCodecKey(codec), width, height, backendName(), extradataBytes));
    return true;
}

bool CaptureCompressedDecoder::decode(const uint8_t* data, size_t bytes, int64_t pts100ns,
    AVFrame* nv12Target, AVFrame** out, bool& hardware)
{
    auto& p = *p_;
    *out = nullptr;
    hardware = false;
    p.waiting = false;
    if (!p.decoder.opened() || data == nullptr || bytes == 0 || nv12Target == nullptr) {
        p.error = "decode called without an open decoder or a writable target frame";
        ++p.failures;
        return false;
    }
    // Two attempts at most: the second one re-sends the same payload after a
    // hardware decoder whose first frame cannot be imported has been replaced
    // by the software decoder.
    for (int attempt = 0; attempt < 2; ++attempt) {
        AVPacket* packet = av_packet_alloc();
        if (packet == nullptr) {
            p.error = "av_packet_alloc failed";
            ++p.failures;
            return false;
        }
        if (av_new_packet(packet, int(bytes)) < 0) {
            av_packet_free(&packet);
            p.error = "av_new_packet failed";
            ++p.failures;
            return false;
        }
        std::memcpy(packet->data, data, bytes);
        packet->pts = pts100ns;
        packet->dts = pts100ns;
        const bool sent = p.decoder.sendPacket(packet);
        av_packet_free(&packet);
        if (!sent) {
            p.error = "decoder rejected the compressed payload";
            ++p.failures;
            return false;
        }
        const AVFrame* frame = p.decoder.receiveFrame();
        if (frame == nullptr) {
            if (p.decoder.receiveStatus() == media::DecodeReceiveStatus::Error) {
                p.error = "decoder receive failed";
                ++p.failures;
            } else {
                p.error.clear();
                p.waiting = true;
            }
            return false;
        }
        if (p.hardware && !p.hardwareChecked) {
            p.hardwareChecked = true;
            if (!p.decoder.hardwareFrameImportable()) {
                log::warn("capture-decode", std::format(
                    "codec={} hardware frame is not an importable NV12/P010 surface; switching to the software decoder",
                    captureCodecKey(p.codec)));
                p.decoder.close();
                if (!p.openSoftwareDecoder()) {
                    p.error = "software fallback after an unimportable hardware frame failed";
                    ++p.failures;
                    return false;
                }
                p.hardware = false;
                ++p.fallbacks;
                continue; // re-send this payload on the software decoder
            }
        }
        if (p.hardware) {
            *out = const_cast<AVFrame*>(frame);
            hardware = true;
            ++p.frames;
            p.error.clear();
            return true;
        }
        if (!p.convertToNv12(*frame, nv12Target)) return false;
        *out = nv12Target;
        hardware = false;
        ++p.frames;
        p.error.clear();
        return true;
    }
    p.error = "hardware-to-software fallback did not converge";
    ++p.failures;
    return false;
}

bool CaptureCompressedDecoder::opened() const { return p_->decoder.opened(); }
bool CaptureCompressedDecoder::hardwareActive() const { return p_->hardware; }
bool CaptureCompressedDecoder::waitingForInput() const { return p_->waiting; }
const char* CaptureCompressedDecoder::backendName() const
{
    if (p_->hardware) return "d3d12va";
    if (p_->hardwareRequested) return "software(after d3d12va)";
    return p_->decoder.opened() ? "software" : "none";
}
const std::string& CaptureCompressedDecoder::lastError() const { return p_->error; }
uint64_t CaptureCompressedDecoder::framesDecoded() const { return p_->frames; }
uint64_t CaptureCompressedDecoder::decodeFailures() const { return p_->failures; }
uint64_t CaptureCompressedDecoder::hardwareFallbacks() const { return p_->fallbacks; }
void CaptureCompressedDecoder::close() { p_->close(); }

} // namespace veyra::source
