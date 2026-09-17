#pragma once

// FFmpeg software video decoder (Phase 3A baseline; Playbook section 13.1).
// D3D12VA arrives in P3.3 as a separate configuration of the same surface.

#include <cstdint>
#include <string>

struct AVCodecContext;
struct AVCodecParameters;
struct AVFrame;
struct AVPacket;
struct AVBufferRef;
struct ID3D12Device;
struct ID3D12CommandQueue;

namespace veyra::media {

struct DecoderStats {
    uint64_t framesDecoded = 0;
    uint64_t framesSubmitted = 0;
    int64_t firstPts = 0;
    int64_t lastPts = 0;
    uint64_t ptsNonMonotonicCount = 0;
    uint64_t pixelReadbackCount = 0; // software decode uploads only; always 0 on the GPU path
};

enum class DecodeReceiveStatus { NeedInput, Frame, EndOfStream, Error };

class FFmpegVideoDecoder {
public:
    FFmpegVideoDecoder() = default;
    ~FFmpegVideoDecoder();

    FFmpegVideoDecoder(const FFmpegVideoDecoder&) = delete;
    FFmpegVideoDecoder& operator=(const FFmpegVideoDecoder&) = delete;

    // Copies the stream's codec parameters into the codec context; the
    // demuxer stays alive independently of this decoder. `streamTimeBaseNum/
    // Den` is the DEMUXER stream time base: decoder frame PTS pass through in
    // that base when the codec context itself carries none.
    bool openSoftware(const AVCodecParameters* codecParameters,
        int streamTimeBaseNum = 0, int streamTimeBaseDen = 0, unsigned softwareThreads = 1,
        bool lowLatency = false);

    // D3D12VA hardware decode on the SHARED Veyra device (Playbook 13.2):
    // creates an AV_HWDEVICE_TYPE_D3D12VA context wrapping `device`/`queue`,
    // installs a get_format callback that only selects AV_PIX_FMT_D3D12, and
    // records GPU queue waits on each frame's sync fence.
    bool openD3D12VA(const AVCodecParameters* codecParameters,
        int streamTimeBaseNum, int streamTimeBaseDen,
        ID3D12Device* device, ID3D12CommandQueue* queue, bool lowLatency = false);
    void close();

    bool opened() const { return context_ != nullptr; }
    int width() const;
    int height() const;
    int pixelFormat() const; // AVPixelFormat as int to keep this header light

    // Send one packet (nullptr flushes at end/seek). Returns false on a hard
    // decode error; end-of-stream is not an error.
    bool sendPacket(const AVPacket* packet);

    // Receive one decoded frame; false when more input is needed or at EOF.
    // The frame stays owned by the decoder and is valid until the next call.
    const AVFrame* receiveFrame();
    // nullptr can mean pending input, normal EOF, or a hard decode error.
    DecodeReceiveStatus receiveStatus() const { return receiveStatus_; }

    void flushBuffers(); // seek boundary (Playbook 13.4)
    const DecoderStats& stats() const { return stats_; }

    // True when the decoder is producing AV_PIX_FMT_D3D12 frames.
    // Time base the decoded frames' PTS are expressed in (the demuxer stream
    // time base, applied when the codec context carries none).
    int frameTimeBaseNum() const { return frameTimeBaseNum_; }
    int frameTimeBaseDen() const { return frameTimeBaseDen_; }

    bool usingD3D12Frames() const { return hwAccelActive_ && context_ != nullptr && lastFrameFormat_ != -1; }
    bool hardwareActive() const { return hwAccelActive_ && context_ != nullptr; }
    // The product ingress currently imports only 2D NV12/P010 D3D12VA
    // surfaces. Validate that contract before handing the first frame to the
    // graph so an unsupported hardware surface can fall back cleanly.
    bool hardwareFrameImportable() const;
    int lastFrameFormat() const { return lastFrameFormat_; }
    uint64_t gpuQueueWaitCount() const { return gpuQueueWaitCount_; }

private:
    AVCodecContext* context_ = nullptr;
    AVFrame* frame_ = nullptr;
    DecodeReceiveStatus receiveStatus_ = DecodeReceiveStatus::NeedInput;
    int frameTimeBaseNum_ = 0;
    int frameTimeBaseDen_ = 0;
    DecoderStats stats_{};
    bool hwAccelActive_ = false;
    int lastFrameFormat_ = -1;
    uint64_t gpuQueueWaitCount_ = 0;
};

} // namespace veyra::media
