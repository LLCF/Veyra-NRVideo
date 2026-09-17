#include "veyra/media/FFmpegVideoDecoder.h"

#include <d3d11.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_2.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/hwcontext_d3d12va.h>
}

#include <format>
#include <algorithm>

#include "veyra/Log.h"

namespace veyra::media {

namespace {

using Microsoft::WRL::ComPtr;

std::string hrText(HRESULT hr)
{
    return std::format("0x{:08X}", static_cast<uint32_t>(hr));
}

// get_format: only accept D3D12 (Playbook 13.2 get_format contract).
enum AVPixelFormat SelectD3D12Format(struct AVCodecContext* /*ctx*/, const enum AVPixelFormat* pixFmts)
{
    for (const enum AVPixelFormat* p = pixFmts; *p != AV_PIX_FMT_NONE; ++p) {
        if (*p == AV_PIX_FMT_D3D12) {
            return *p;
        }
    }
    return AV_PIX_FMT_NONE;
}

// get_format for the D3D11VA path: only D3D11 surfaces are importable.
enum AVPixelFormat SelectD3D11Format(struct AVCodecContext* /*ctx*/, const enum AVPixelFormat* pixFmts)
{
    for (const enum AVPixelFormat* p = pixFmts; *p != AV_PIX_FMT_NONE; ++p) {
        if (*p == AV_PIX_FMT_D3D11) {
            return *p;
        }
    }
    return AV_PIX_FMT_NONE;
}

} // namespace

FFmpegVideoDecoder::~FFmpegVideoDecoder()
{
    close();
}

bool FFmpegVideoDecoder::openSoftware(const AVCodecParameters* codecParameters,
    int streamTimeBaseNum, int streamTimeBaseDen, unsigned softwareThreads, bool lowLatency)
{
    receiveStatus_ = DecodeReceiveStatus::NeedInput;
    if (context_ != nullptr) {
        close();
    }
    if (codecParameters == nullptr) {
        return false;
    }
    frameTimeBaseNum_ = streamTimeBaseNum;
    frameTimeBaseDen_ = streamTimeBaseDen;

    // The FFmpeg native AV1 decoder is hardware-path oriented in the
    // pinned Windows build.  When the optional LGPL-compatible dav1d
    // backend is present, prefer it for software playback so AV1 files do
    // not fail after the demuxer has already accepted them.  Keep the
    // native decoder as a fallback for installations that do not ship
    // dav1d (and let the caller report the actual decode error).
    const AVCodec* codec = nullptr;
    if (codecParameters->codec_id == AV_CODEC_ID_AV1) {
        codec = avcodec_find_decoder_by_name("libdav1d");
    }
    if (codec == nullptr) {
        codec = avcodec_find_decoder(codecParameters->codec_id);
    }
    if (codec == nullptr) {
        log::error("media", std::format("decoder: no software decoder for codecId={}", static_cast<int>(codecParameters->codec_id)));
        return false;
    }

    context_ = avcodec_alloc_context3(codec);
    if (context_ == nullptr) {
        return false;
    }
    if (avcodec_parameters_to_context(context_, codecParameters) < 0) {
        log::error("media", "decoder: avcodec_parameters_to_context failed");
        avcodec_free_context(&context_);
        return false;
    }
    // Legacy bounded packet pumps keep the default single thread. The file
    // source can supply compressed lookahead until receiveFrame succeeds and
    // explicitly opts into at most four codec workers; no application frame
    // queue and no capture lookahead are introduced.
    context_->thread_count = int(std::clamp(softwareThreads,1u,4u));
    context_->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    if (lowLatency) {
        // Live capture: never trade latency for throughput. Frame threading
        // buffers whole frames before returning any output, so slice-only
        // execution plus the codec's low-delay flag keeps every decoded frame
        // available as soon as it is complete.
        context_->thread_type = FF_THREAD_SLICE;
        context_->flags |= AV_CODEC_FLAG_LOW_DELAY;
    }
    if (avcodec_open2(context_, codec, nullptr) < 0) {
        log::error("media", "decoder: avcodec_open2 failed");
        avcodec_free_context(&context_);
        return false;
    }

    frame_ = av_frame_alloc();
    if (frame_ == nullptr) {
        avcodec_free_context(&context_);
        return false;
    }

    stats_ = DecoderStats{};
    log::info("media", std::format("decoder: software decoder opened codec={} {}x{} pixFmt={} threads={} activeThreadType={} lowLatency={}",
        codec->name, context_->width, context_->height, static_cast<int>(context_->pix_fmt),context_->thread_count,context_->active_thread_type,lowLatency));
    return true;
}

bool FFmpegVideoDecoder::openD3D12VA(const AVCodecParameters* codecParameters,
    int streamTimeBaseNum, int streamTimeBaseDen,
    ID3D12Device* device, ID3D12CommandQueue* queue, bool lowLatency)
{
    receiveStatus_ = DecodeReceiveStatus::NeedInput;
    if (context_ != nullptr) {
        close();
    }
    if (codecParameters == nullptr || device == nullptr || queue == nullptr) {
        return false;
    }
    frameTimeBaseNum_ = streamTimeBaseNum;
    frameTimeBaseDen_ = streamTimeBaseDen;

    const AVCodec* codec = avcodec_find_decoder(codecParameters->codec_id);
    if (codec == nullptr) {
        log::error("media", "decoder: no decoder for d3d12va codec");
        return false;
    }

    // Shared Veyra device wrapped in a D3D12VA hw device context
    // (Playbook 13.2). FFmpeg owns one reference via the buffer.
    AVBufferRef* hwDeviceRef = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D12VA);
    if (hwDeviceRef == nullptr) {
        log::error("media", "decoder: av_hwdevice_ctx_alloc(D3D12VA) failed");
        return false;
    }
    auto* hwDevice = reinterpret_cast<AVHWDeviceContext*>(hwDeviceRef->data);
    auto* hwctx = reinterpret_cast<AVD3D12VADeviceContext*>(hwDevice->hwctx);
    hwctx->device = device;
    device->AddRef(); // FFmpeg context owns/releases this interface
    const int initResult = av_hwdevice_ctx_init(hwDeviceRef);
    if (initResult < 0) {
        log::error("media", std::format("decoder: av_hwdevice_ctx_init failed code={}", initResult));
        av_buffer_unref(&hwDeviceRef);
        return false;
    }

    context_ = avcodec_alloc_context3(codec);
    if (context_ == nullptr) {
        av_buffer_unref(&hwDeviceRef);
        return false;
    }
    if (avcodec_parameters_to_context(context_, codecParameters) < 0) {
        avcodec_free_context(&context_);
        av_buffer_unref(&hwDeviceRef);
        return false;
    }
    context_->thread_count = 1;
    context_->get_format = SelectD3D12Format;
    context_->hw_device_ctx = av_buffer_ref(hwDeviceRef);
    if (lowLatency) {
        // Live capture must not wait for the decoder's reorder window.
        context_->flags |= AV_CODEC_FLAG_LOW_DELAY;
    }
    av_buffer_unref(&hwDeviceRef); // codec ctx holds its own reference now

    if (avcodec_open2(context_, codec, nullptr) < 0) {
        log::error("media", "decoder: avcodec_open2 failed for d3d12va");
        avcodec_free_context(&context_);
        return false;
    }

    frame_ = av_frame_alloc();
    if (frame_ == nullptr) {
        avcodec_free_context(&context_);
        return false;
    }
    stats_ = DecoderStats{};
    hwAccelActive_ = true;
    gpuQueueWaitCount_ = 0;
    log::info("media", std::format("decoder: d3d12va decoder opened codec={} {}x{} lowLatency={} (shared Veyra device)",
        codec->name, context_->width, context_->height, lowLatency));
    return true;
}

void FFmpegVideoDecoder::close()
{
    receiveStatus_ = DecodeReceiveStatus::NeedInput;
    if (frame_ != nullptr) {
        av_frame_free(&frame_);
    }
    if (context_ != nullptr) {
        avcodec_free_context(&context_);
    }
    releaseInterop();
    hwAccelActive_ = false;
    hwAccelKind_ = HardwareDecodeKind::None;
    lastFrameFormat_ = -1;
    gpuQueueWaitCount_ = 0;
}

bool FFmpegVideoDecoder::hardwareFrameImportable() const
{
    if (!hardwareActive() || frame_ == nullptr || frame_->format != AV_PIX_FMT_D3D12) {
        return false;
    }
    auto* d3dFrame = reinterpret_cast<AVD3D12VAFrame*>(frame_->data[0]);
    if (d3dFrame == nullptr || d3dFrame->texture == nullptr) {
        return false;
    }
    const auto desc = d3dFrame->texture->GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.DepthOrArraySize == 0 ||
        desc.MipLevels != 1 || desc.SampleDesc.Count != 1) {
        return false;
    }
    if (desc.Format != DXGI_FORMAT_NV12 && desc.Format != DXGI_FORMAT_P010) {
        return false;
    }
    return context_ != nullptr && desc.Width == static_cast<UINT64>(context_->width) &&
        desc.Height == static_cast<UINT>(context_->height);
}

int FFmpegVideoDecoder::width() const
{
    return context_ != nullptr ? context_->width : 0;
}

int FFmpegVideoDecoder::height() const
{
    return context_ != nullptr ? context_->height : 0;
}

int FFmpegVideoDecoder::pixelFormat() const
{
    return context_ != nullptr ? static_cast<int>(context_->pix_fmt) : -1;
}

bool FFmpegVideoDecoder::sendPacket(const AVPacket* packet)
{
    if (context_ == nullptr) {
        return false;
    }
    const int result = avcodec_send_packet(context_, packet);
    if (result < 0 && result != AVERROR(EAGAIN) && result != AVERROR_EOF) {
        char errorText[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(result, errorText, sizeof(errorText));
        log::error("media", std::format("decoder: send_packet failed code={} text={}", result, errorText));
        return false;
    }
    if (packet != nullptr) {
        ++stats_.framesSubmitted;
    }
    return true;
}

const AVFrame* FFmpegVideoDecoder::receiveFrame()
{
    if (context_ == nullptr) {
        receiveStatus_ = DecodeReceiveStatus::Error;
        return nullptr;
    }
    const int result = avcodec_receive_frame(context_, frame_);
    if (result < 0) {
        if (result == AVERROR(EAGAIN)) receiveStatus_ = DecodeReceiveStatus::NeedInput;
        else if (result == AVERROR_EOF) receiveStatus_ = DecodeReceiveStatus::EndOfStream;
        else {
            receiveStatus_ = DecodeReceiveStatus::Error;
            char errorText[AV_ERROR_MAX_STRING_SIZE]{};
            av_strerror(result, errorText, sizeof(errorText));
            log::error("media", std::format("decoder: receive_frame failed code={} text={}", result, errorText));
        }
        return nullptr;
    }
    receiveStatus_ = DecodeReceiveStatus::Frame;
    ++stats_.framesDecoded;
    // Frame timestamps are in the CODEC context time_base; AVFrame.time_base
    // is not reliably populated by every decoder path.
    const int64_t stamp = frame_->best_effort_timestamp != AV_NOPTS_VALUE
        ? frame_->best_effort_timestamp
        : (frame_->pts != AV_NOPTS_VALUE ? frame_->pts : frame_->pkt_dts);
    // Prefer the frame's own base; fall back to the codec context base, then
    // to the demuxer stream base passed at open (observed: this FFmpeg passes
    // stream-base PTS through while the codec context base stays {0,1}).
    AVRational base = frame_->time_base;
    if (base.num == 0 || base.den == 0) {
        base = context_->time_base;
    }
    if (base.num == 0 || base.den == 0) {
        base.num = frameTimeBaseNum_;
        base.den = frameTimeBaseDen_;
    }
    const int64_t ptsUs = base.den > 0 ? av_rescale_q(stamp, base, { 1, 1000000 }) : 0;
    lastFrameFormat_ = frame_->format;
    if (hwAccelActive_ && frame_->format == AV_PIX_FMT_D3D12) {
        // GPU queue wait on the frame's sync fence (Playbook 13.2: a GPU-side
        // wait, never a CPU WaitForSingleObject per frame).
        auto* d3dFrame = reinterpret_cast<AVD3D12VAFrame*>(frame_->data[0]);
        if (d3dFrame != nullptr && d3dFrame->sync_ctx.fence != nullptr) {
            // Note: the wait target is the Veyra direct queue when the frame
            // consumer runs there; for the probe we count the wait and rely
            // on the next GPU operation to serialize. The real pipeline (P3.4+)
            // issues queue->Wait before touching the texture.
            ++gpuQueueWaitCount_;
        }
    }
    if (stats_.framesDecoded <= 3) {
        log::info("media", std::format("decoder: frame#{} stamp={} tb={}/{} -> ptsUs={} format={}",
            stats_.framesDecoded, stamp, base.num, base.den, ptsUs, frame_->format));
    }
    if (stats_.framesDecoded == 1) {
        stats_.firstPts = ptsUs;
    }
    else if (ptsUs < stats_.lastPts) {
        ++stats_.ptsNonMonotonicCount;
    }
    stats_.lastPts = ptsUs;
    return frame_;
}

void FFmpegVideoDecoder::flushBuffers()
{
    receiveStatus_ = DecodeReceiveStatus::NeedInput;
    if (context_ != nullptr) {
        avcodec_flush_buffers(context_);
    }
}

} // namespace veyra::media
