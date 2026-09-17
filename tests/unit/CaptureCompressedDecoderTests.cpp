// MPEG chain stage 3: the compressed-payload decoder must drive real H.264
// elementary streams through both backends (D3D12VA and software) and produce
// frames the capture ingress can consume. The physical capture card on this
// machine has no H.264/HEVC format, so the payloads are the Annex-B form of a
// local H.264 clip; the decoder under test is the exact one the capture worker
// uses, driven packet by packet like a live stream.

#include "veyra/source/CaptureCompressedDecoder.h"
#include "veyra/gfx/D3D12DeviceContext.h"

#include <iostream>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
}

using namespace veyra;

namespace {

struct ElementaryStream {
    int width = 0, height = 0;
    source::CaptureCodec codec = source::CaptureCodec::None;
    std::vector<std::vector<uint8_t>> accessUnits;
};

bool loadAnnexB(const char* path, ElementaryStream& stream, std::string& error)
{
    AVFormatContext* format = nullptr;
    if (avformat_open_input(&format, path, nullptr, nullptr) < 0) {
        error = std::string("cannot open ") + path;
        return false;
    }
    if (avformat_find_stream_info(format, nullptr) < 0) {
        avformat_close_input(&format);
        error = "find_stream_info failed";
        return false;
    }
    const int videoIndex = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoIndex < 0) {
        avformat_close_input(&format);
        error = "no video stream";
        return false;
    }
    AVCodecParameters* parameters = format->streams[videoIndex]->codecpar;
    const char* filterName = nullptr;
    if (parameters->codec_id == AV_CODEC_ID_H264) {
        stream.codec = source::CaptureCodec::H264;
        filterName = "h264_mp4toannexb";
    } else if (parameters->codec_id == AV_CODEC_ID_HEVC) {
        stream.codec = source::CaptureCodec::Hevc;
        filterName = "hevc_mp4toannexb";
    } else {
        avformat_close_input(&format);
        error = "test input is not H.264/HEVC";
        return false;
    }
    stream.width = parameters->width;
    stream.height = parameters->height;
    const AVBitStreamFilter* filter = av_bsf_get_by_name(filterName);
    AVBSFContext* bsf = nullptr;
    if (filter == nullptr || av_bsf_alloc(filter, &bsf) < 0) {
        avformat_close_input(&format);
        error = "bitstream filter unavailable";
        return false;
    }
    if (avcodec_parameters_copy(bsf->par_in, parameters) < 0) {
        av_bsf_free(&bsf);
        avformat_close_input(&format);
        error = "parameter copy failed";
        return false;
    }
    bsf->time_base_in = format->streams[videoIndex]->time_base;
    if (av_bsf_init(bsf) < 0) {
        av_bsf_free(&bsf);
        avformat_close_input(&format);
        error = "bitstream filter init failed";
        return false;
    }
    AVPacket* packet = av_packet_alloc();
    AVPacket* filtered = av_packet_alloc();
    while (packet != nullptr && filtered != nullptr && av_read_frame(format, packet) >= 0) {
        if (packet->stream_index == videoIndex && av_bsf_send_packet(bsf, packet) >= 0) {
            while (av_bsf_receive_packet(bsf, filtered) >= 0) {
                stream.accessUnits.emplace_back(filtered->data, filtered->data + filtered->size);
                av_packet_unref(filtered);
            }
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    av_packet_free(&filtered);
    av_bsf_free(&bsf);
    avformat_close_input(&format);
    if (stream.accessUnits.empty()) {
        error = "no access units extracted";
        return false;
    }
    return true;
}

struct DecodeRun {
    uint64_t frames = 0;
    uint64_t failures = 0;
    bool sawHardwareFrame = false;
    std::string firstError;
};

DecodeRun decodeAll(ElementaryStream& stream, ID3D12Device* device, ID3D12CommandQueue* queue)
{
    DecodeRun run;
    source::CaptureCompressedDecoder decoder;
    if (!decoder.open(stream.codec, unsigned(stream.width), unsigned(stream.height), nullptr, 0, device, queue)) {
        run.firstError = decoder.lastError().empty() ? "decoder open failed" : decoder.lastError();
        ++run.failures;
        return run;
    }
    AVFrame* target = av_frame_alloc();
    target->format = AV_PIX_FMT_NV12;
    target->width = stream.width;
    target->height = stream.height;
    if (av_frame_get_buffer(target, 32) < 0) {
        av_frame_free(&target);
        run.firstError = "target allocation failed";
        ++run.failures;
        return run;
    }
    int64_t pts = 0;
    for (const auto& unit : stream.accessUnits) {
        AVFrame* out = nullptr;
        bool hardware = false;
        const bool produced = decoder.decode(unit.data(), unit.size(), pts, target, &out, hardware);
        if (produced) {
            ++run.frames;
            if (hardware) {
                run.sawHardwareFrame = true;
                if (out == nullptr || out->format != AV_PIX_FMT_D3D12 || out->data[0] == nullptr) {
                    run.firstError = "hardware frame is not a D3D12 surface";
                    ++run.failures;
                }
            } else if (out == nullptr || out != target) {
                run.firstError = "software frame did not land in the target";
                ++run.failures;
            }
        } else if (!decoder.waitingForInput()) {
            if (run.firstError.empty()) run.firstError = decoder.lastError();
            ++run.failures;
        }
        pts += 333667;
    }
    av_frame_free(&target);
    return run;
}

} // namespace

int main(int argc, char** argv)
{
    const char* path = argc > 1 ? argv[1] : "loop/local/fixed_clips/test_h264_1080p.mp4";
    ElementaryStream stream;
    std::string error;
    if (!loadAnnexB(path, stream, error)) {
        std::cout << "SKIP " << error << " (path=" << path << ")\n";
        return 0; // missing corpus is not a product failure; the gate reports it
    }
    std::cout << "loaded accessUnits=" << stream.accessUnits.size() << " " << stream.width << "x" << stream.height << '\n';

    gfx::D3D12DeviceContext context;
    gfx::DeviceContextDesc desc;
    Status status;
    if (!context.initialize(desc, status)) {
        std::cout << "SKIP no D3D12 device available\n";
        return 0;
    }

    int failures = 0;
    const auto hardware = decodeAll(stream, context.device(), context.directQueue());
    const auto software = decodeAll(stream, nullptr, nullptr);
    std::cout << "hardware frames=" << hardware.frames << " failures=" << hardware.failures
        << " sawD3D12=" << hardware.sawHardwareFrame << " error=" << hardware.firstError << '\n';
    std::cout << "software frames=" << software.frames << " failures=" << software.failures
        << " error=" << software.firstError << '\n';

    const auto check = [&failures](bool pass, const char* name) {
        std::cout << (pass ? "PASS " : "FAIL ") << name << '\n';
        if (!pass) ++failures;
    };
    check(hardware.failures == 0 && hardware.frames > 0, "hardware decode produces frames without errors");
    check(hardware.sawHardwareFrame, "hardware backend returns importable D3D12 frames");
    check(software.failures == 0 && software.frames > 0, "software decode produces frames without errors");
    // B-frame reordering may emit a slightly different count on the software
    // path; the hardware and software backends must stay within one frame.
    const uint64_t difference = hardware.frames > software.frames ? hardware.frames - software.frames : software.frames - hardware.frames;
    check(difference <= 1, "hardware and software frame counts agree");
    std::cout << (failures == 0 ? "ALL PASS" : "FAILURES") << " " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
