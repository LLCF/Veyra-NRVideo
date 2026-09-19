// MPEG chain stage 3: the compressed-payload decoder must drive real H.264
// elementary streams through both backends (D3D12VA and software) and produce
// frames the capture ingress can consume. The physical capture card on this
// machine has no H.264/HEVC format, so the payloads are the Annex-B form of a
// local H.264 clip; the decoder under test is the exact one the capture worker
// uses, driven packet by packet like a live stream.

#include "veyra/source/CaptureCompressedDecoder.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/media/FFmpegVideoDecoder.h"

#include <iostream>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/adler32.h>
}

using namespace veyra;

namespace {

struct ElementaryStream {
    int width = 0, height = 0;
    source::CaptureCodec codec = source::CaptureCodec::None;
    std::vector<std::vector<uint8_t>> accessUnits;
    std::vector<int64_t> pts;
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
                stream.pts.push_back(av_rescale_q(filtered->pts,bsf->time_base_out,AVRational{1,10000000}));
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
    std::map<int64_t,uint32_t> pixels;
    bool recovered=false;
};

DecodeRun decodeAll(ElementaryStream& stream, ID3D12Device* device, ID3D12CommandQueue* queue,bool injectLoss=false)
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
    size_t inputIndex=0;
    auto displayPts=stream.pts;std::sort(displayPts.begin(),displayPts.end());
    bool awaitingKey=false;
    for (const auto& unit : stream.accessUnits) {
        if(injectLoss&&inputIndex==7){decoder.recoverAtKeyframe();awaitingKey=true;}
        if(injectLoss&&inputIndex>=7&&inputIndex<10){++inputIndex;continue;}
        AVFrame* out = nullptr;
        bool hardware = false;
        const bool produced = decoder.decode(unit.data(), unit.size(), stream.pts[inputIndex++], target, &out, hardware);
        if (produced) {
            ++run.frames;
            if(!injectLoss&&(run.frames>displayPts.size()||out->pts!=displayPts[run.frames-1])){++run.failures;run.firstError="decoded PTS was lost or assigned from current packet";}
            if(awaitingKey){
                if(!(out->flags&AV_FRAME_FLAG_KEY)){++run.failures;run.firstError="recovery exposed a dependent frame before keyframe";}
                awaitingKey=false;run.recovered=true;
            }
            if(!hardware){uint32_t checksum=1;for(int plane=0;plane<2;++plane)for(int y=0;y<(plane?out->height/2:out->height);++y)
                checksum=av_adler32_update(checksum,out->data[plane]+ptrdiff_t(y)*out->linesize[plane],out->width);
                run.pixels[out->pts]=checksum;}
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
        AVFrame* remaining=nullptr;bool remainingHardware=false;
        if(decoder.decode(nullptr,0,0,target,&remaining,remainingHardware)||!decoder.waitingForInput()){
            ++run.failures;run.firstError="receive-only drain returned stale output or an error";
        }
    }
    av_frame_free(&target);
    return run;
}

bool packetRetryTest(const char* path,gfx::D3D12DeviceContext& device) {
    AVFormatContext* format=nullptr;
    if(avformat_open_input(&format,path,nullptr,nullptr)<0)return false;
    if(avformat_find_stream_info(format,nullptr)<0){avformat_close_input(&format);return false;}
    int index=av_find_best_stream(format,AVMEDIA_TYPE_VIDEO,-1,-1,nullptr,0);
    if(index<0){avformat_close_input(&format);return false;}
    auto* stream=format->streams[index];
    media::FFmpegVideoDecoder reference,batched;
    bool ok=reference.openSoftware(stream->codecpar,stream->time_base.num,stream->time_base.den)&&batched.openSoftware(stream->codecpar,stream->time_base.num,stream->time_base.den);
    std::vector<int64_t> expected,actual;
    auto drain=[](auto& decoder,auto& pts){while(auto* f=decoder.receiveFrame())pts.push_back(f->pts);};
    AVPacket* packet=av_packet_alloc();size_t count=0;
    while(ok&&av_read_frame(format,packet)>=0){
        if(packet->stream_index==index){
            ok=reference.sendPacket(packet)&&batched.sendPacket(packet);++count;
            drain(reference,expected);
            if(count%8==0)drain(batched,actual); // deliberately forces send EAGAIN
        }
        av_packet_unref(packet);
    }
    ok=ok&&reference.sendPacket(nullptr)&&batched.sendPacket(nullptr);
    drain(reference,expected);drain(batched,actual);
    ok=ok&&!expected.empty()&&expected==actual&&batched.stats().framesSubmitted==count&&batched.stats().packetRetries>0;
    batched.flushBuffers();
    ok=ok&&!batched.receiveFrame()&&batched.receiveStatus()==media::DecodeReceiveStatus::NeedInput;
    std::cout<<(ok?"PASS ":"FAIL ")<<"EAGAIN resend, output order, accepted count and flush frames="<<actual.size()<<" retries="<<batched.stats().packetRetries<<'\n';
    media::FFmpegVideoDecoder interop;
    for(int pass=0;ok&&pass<3;++pass){
        ok=av_seek_frame(format,index,0,AVSEEK_FLAG_BACKWARD)>=0&&interop.openD3D11VA(stream->codecpar,stream->time_base.num,stream->time_base.den,device.adapter().luid,device.device());
        std::vector<int64_t> imported;
        auto receive=[&]{while(auto* frame=interop.receiveFrame()){ok=ok&&interop.hardwareFrameImportable();imported.push_back(frame->pts);}};
        while(ok&&av_read_frame(format,packet)>=0){
            if(packet->stream_index==index){ok=interop.sendPacket(packet);receive();}
            av_packet_unref(packet);
        }
        ok=ok&&interop.sendPacket(nullptr);receive();
        ok=ok&&imported==expected;interop.close();interop.close();
    }
    std::cout<<(ok?"PASS ":"FAIL ")<<"D3D11VA repeated open/decode/close, import and PTS\n";
    av_packet_free(&packet);avformat_close_input(&format);
    return ok;
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
    if(!packetRetryTest(path,context))++failures;
    const auto hardware = decodeAll(stream, context.device(), context.directQueue());
    const auto software = decodeAll(stream, nullptr, nullptr);
    const auto recovery = decodeAll(stream, nullptr, nullptr,true);
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
    bool samePixels=true;for(const auto& [pts,pixels]:recovery.pixels){auto found=software.pixels.find(pts);samePixels=samePixels&&found!=software.pixels.end()&&found->second==pixels;}
    check(recovery.recovered&&recovery.failures==0&&samePixels,"packet loss resumes at keyframe with reference-identical pixels and PTS");
    // B-frame reordering may emit a slightly different count on the software
    // path; the hardware and software backends must stay within one frame.
    const uint64_t difference = hardware.frames > software.frames ? hardware.frames - software.frames : software.frames - hardware.frames;
    check(difference <= 1, "hardware and software frame counts agree");
    std::cout << (failures == 0 ? "ALL PASS" : "FAILURES") << " " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
