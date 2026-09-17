#include "veyra/engine/VideoExportJob.h"
#include "veyra/engine/CfrTimeline.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/pipeline/ResolutionPlan.h"
#include "veyra/sink/VideoEncoder.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/RuntimePaths.h"
#include <filesystem>
#include <format>
#include <thread>
#include <chrono>
#include <cmath>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/mathematics.h>
#include <libavutil/pixdesc.h>
#include <libavutil/mastering_display_metadata.h>
}
namespace veyra::engine {
namespace { std::string utf8(const std::wstring& s){const int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string r(n,0);WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),r.data(),n,nullptr,nullptr);return r;} }
bool exportVideo(const std::wstring& input,const std::wstring& output,PlayerOptions options,bool hevc,std::atomic<bool>& cancel,const std::function<void(double,const std::wstring&)>& progress,unsigned maxFrames,const std::function<bool()>& frameBoundary,const std::function<void(const ExportCounts&)>& counts){
    if(std::filesystem::exists(output)||std::filesystem::exists(output+L".partial")){progress(0,L"目标或partial文件已存在，请使用其他名称");return false;}
    // XeSS-FG and AMD FSR-FG interpolate inside the present swapchain: the
    // provider presents the extra frames itself, so no output texture ever
    // reaches the application (XeSS-FG 3.0.2 exports only xefgSwapChain*, and
    // the FSR-FG context owns the proxy swapchain). Refusing the job left those
    // users with no file at all, so export now runs the in-graph DLSS path and
    // states the substitution; if that backend cannot initialise either, the
    // job still completes without frame generation instead of failing.
    std::wstring fgNote;
    auto appendFgNote=[&](const std::wstring& extra){if(!fgNote.empty())fgNote+=L"；";fgNote+=extra;};
    if(options.fg&&presentSinkFrameGeneration(options.settings.frameGenerationBackend)){
        const auto requested=options.settings.frameGenerationBackend;
        fgNote=std::format(L"{}补帧由显示交换链直接生成，导出取不到它的画面；本次导出改用 DLSS 补帧 {}X",
            requested==FrameGenerationBackend::XeSS?L"XeSS":L"AMD FSR",options.fgMultiplier);
        veyra::log::warn("export",std::format("present-sink frame generation cannot feed the encoder requested={} multiplier={}; substituting the in-graph DLSS path",frameGenerationBackendName(requested),options.fgMultiplier));
        options.settings.frameGenerationBackend=FrameGenerationBackend::Dlss;
    }
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;source::MediaFileSource source;pipeline::EnhanceGraph graph(ctx,ring);
    std::unique_ptr<sink::VideoEncoder> encoder;
    AVFormatContext *mux=nullptr,*audioInput=nullptr;AVStream* videoStream=nullptr;AVStream* audioStream=nullptr;AVPacket* audioPacket=av_packet_alloc();
    int audioIndex=-1;bool audioPending=false,audioEof=false,ok=false,headerWritten=false;int64_t written=0;double audioEndSeconds=0,videoOriginSeconds=0;
    uint64_t tailSnapped=0,gapFilledSlots=0,gapFillEvents=0;
    std::wstring encoderName;
    std::wstring failureReason;
    auto failAv=[&](const wchar_t* stage,int code){
        char error[AV_ERROR_MAX_STRING_SIZE]{};av_strerror(code,error,sizeof(error));
        failureReason=std::format(L"{}失败（错误 {}）",stage,code);
        veyra::log::error("export",std::format("stage={} code={} detail={}",utf8(stage),code,error));
        return false;
    };
    bool hdrExport=false;
    uint32_t outputWidth=0,outputHeight=0;AVRational outputRate{};
    const auto partial=output+L".partial";
    try { do {
        Status st=Status::Ok;gfx::DeviceContextDesc dd;dd.commandSlotCount=6;
        if(!ctx.initialize(dd,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,st))break;
        // Export runs on every adapter now: NVIDIA uses NVENC (D3D12,
        // zero-copy) and everything else uses the driver's Media Foundation
        // hardware encoder. Features that need NGX/NVOF stay NVIDIA-only and
        // are gated below instead of failing the job.
        const bool nvidiaAdapter=ctx.adapter().isNvidia;
        if(!nvidiaAdapter)veyra::log::info("export",std::format("adapter={} vendor={}; DLSS NR/SR/FG and NVOF are unavailable, encoder falls back to the system Media Foundation hardware MFT",utf8(ctx.adapter().description),ctx.adapter().vendorIdHex));
        source::SourceOpenDesc od;od.path=input;od.preferHardwareDecode=false;if(!source.open(od)){failureReason=source.errorMessage();break;}
        auto info=source.info();if(!pipeline::Extent{info.width,info.height}.valid()){progress(0,L"输入尺寸超出GPU单纹理能力");break;}
        // Bounded metadata scan; decoded pixels are not retained. Rewind the
        // file source afterwards, preserving all source frames for the export.
        std::vector<double> samples;bool scanError=false;
        for(unsigned i=0;i<120&&!cancel;++i){pipeline::FramePacket p;const AVFrame* f=nullptr;const auto rs=source.read(p,&f);if(rs==source::SourceReadStatus::Eos)break;if(rs!=source::SourceReadStatus::Frame||p.pts.isUnknown()){failureReason=source.errorMessage();scanError=true;break;}samples.push_back(p.pts.toDouble());}
        if(scanError||samples.empty()||cancel)break;
        const auto [rateNum,rateDen]=CfrTimeline::select(info.nominalRateNum,info.nominalRateDen,info.timestampQuantum,samples);
        CfrTimeline timeline(rateNum,rateDen,info.timestampQuantum,samples.front());
        if(!timeline.valid()){progress(0,L"无法确认一致的恒定帧率，已停止导出");veyra::log::error("export-timeline","CFR rejected source=preflight; no timestamp-consistent rate candidate");break;}
        source.close();if(!source.open(od))break; // reopen from the true beginning, including negative PTS
        // The first decoded frame is authoritative when container headers omit
        // transfer/range metadata. Do not build an SDR graph from stale header
        // defaults after the preflight discovered HDR/BT.2020 pixels.
        pipeline::FramePacket firstPacket;const AVFrame* firstFrame=nullptr;
        if(source.read(firstPacket,&firstFrame)!=source::SourceReadStatus::Frame||!firstFrame){failureReason=L"导出预读首帧失败";break;}
        info=source.info(); // retain this frame for the export, without decoding it again
        const auto resolution=pipeline::ResolutionPlan::make({info.width,info.height},options.sr,pipeline::NrSizePolicy::Native,true,options.settings.revision,options.settings.srTarget);
        pipeline::EnhanceGraphDesc gd;gd.hdrInput=gd.hdrOutput=info.color.isHdrPath();hdrExport=gd.hdrOutput;
        // Plan v5.3: the grade changes the peak and the content light distribution.
        // Recomputing MaxCLL/MaxFALL needs a full pre-pass before the header is
        // written, which this exporter does not do yet, so say so instead of
        // letting the user assume the static metadata tracks the graded output.
        if(gd.hdrOutput&&options.settings.color.enabled&&!options.settings.color.neutral())
            veyra::log::warn("color-export","HDR export with the colour grade active: MaxCLL/MaxFALL are carried over from the source and NOT recomputed for the graded output (marked as not updated)");
        gd.captureBitDepth=info.color.pixelFormat==pipeline::SourcePixelFormat::P010?10:info.color.pixelFormat==pipeline::SourcePixelFormat::P016?16:8;
        if(gd.hdrOutput&&!hevc){failureReason=L"HDR视频请使用HEVC Main10导出（选择HEVC）";break;}
        // Adapter gate mirrors the preview rules (EngineController): DLSS NR,
        // DLSS SR and the NVOF guidance need an NVIDIA device; AMD FSR
        // upscaling is the only vendor-neutral video SR we ship. Requesting an
        // unavailable feature must degrade the export, never fail it.
        const bool nvidiaFeatures=nvidiaAdapter;
        const bool srAvailable=resolution.srApplied&&(nvidiaFeatures||options.settings.videoSrQuality==kVideoSrFsr);
        if(options.nr&&!nvidiaFeatures)fgNote+=fgNote.empty()?L"当前显卡不能使用 DLSS NR，本次导出自动关闭 NR":L"；当前显卡不能使用 DLSS NR，本次导出自动关闭 NR";
        if(options.sr&&!srAvailable)fgNote+=fgNote.empty()?L"当前显卡不能使用所选超分，本次导出关闭超分":L"；当前显卡不能使用所选超分，本次导出关闭超分";
        if(!nvidiaFeatures&&srAvailable)fgNote+=fgNote.empty()?L"本次导出使用 AMD FSR 超分":L"；本次导出使用 AMD FSR 超分";
        gd.sourceWidth=info.width;gd.sourceHeight=info.height;gd.workWidth=resolution.base.width;gd.workHeight=resolution.base.height;gd.nrWidth=resolution.nr.width;gd.nrHeight=resolution.nr.height;gd.flowWidth=resolution.flow.width;gd.flowHeight=resolution.flow.height;gd.enableSr=srAvailable;gd.videoSrQuality=options.settings.videoSrQuality;gd.enableNr=options.nr&&nvidiaFeatures;gd.nrRuntime=options.settings.nrRuntime;gd.enableFg=options.fg&&nvidiaFeatures;gd.fgMultiplier=options.fgMultiplier;gd.frameGenerationBackend=options.settings.frameGenerationBackend;gd.enableNvofStandalone=options.nr&&nvidiaFeatures;gd.model=options.settings.model;gd.residual=options.settings.residual;gd.protection=options.settings.protection;gd.color=options.settings.color;gd.settingsRevision=options.settings.revision;gd.flowQuality=options.settings.flow;gd.opticalFlowBackend=options.settings.opticalFlowBackend;gd.amdFlowHalfResolution=options.settings.amdFlowHalfResolution;gd.contentRate=options.settings.content;gd.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
        // One attempt per frame-generation request. A rejected multiplier is a
        // capability statement, not an infrastructure failure: retry at 2X (the
        // floor every DLSS-G capable GPU honours) and only then fall back to a
        // real-frames-only export, so the user keeps a usable file either way.
        bool graphReady=false,fgFailure=false;
        auto startGraph=[&](bool fg,uint32_t multiplier){
            gd.enableFg=fg;gd.fgMultiplier=fg?multiplier:1;
            if(graph.initialize(gd)&&graph.createViews()){graphReady=true;return;}
            fgFailure=graph.failedBackend()==FailedBackend::Fg;
            graph.shutdown();
        };
        if(options.fg&&!nvidiaFeatures){options.fg=false;options.fgMultiplier=1;}
        if(options.fg){
            startGraph(true,options.fgMultiplier);
            if(!graphReady&&fgFailure&&options.fgMultiplier>2){
                startGraph(true,2);
                if(graphReady){
                    veyra::log::warn("export",std::format("DLSS frame generation rejected multiplier={} on this adapter; exporting at 2X",options.fgMultiplier));
                    options.fgMultiplier=2;appendFgNote(L"DLSS 补帧在该显卡上不支持请求的倍率，本次导出降为 2X");
                }
            }
            if(!graphReady&&fgFailure){
                startGraph(false,1);
                if(graphReady){
                    veyra::log::warn("export","DLSS frame generation unavailable; exporting real frames only");
                    options.fg=false;options.fgMultiplier=1;appendFgNote(L"DLSS 补帧无法初始化，本次导出只输出原始帧（补帧关闭）");
                }
            }
        } else startGraph(false,1);
        if(!graphReady){if(failureReason.empty())failureReason=L"增强管线初始化失败，请查看诊断";break;}
        if(!fgNote.empty())progress(0,fgNote);
        const AVRational rate=av_mul_q({rateNum,rateDen},{int(options.fg?options.fgMultiplier:1),1});
        outputRate=rate;
        veyra::log::info("export-timeline",std::format("CFR declared={}/{} candidate={}/{} timestampQuantum={} sampled={} output={}/{} fg={} backend={} note={} (timestamp-consistent candidate, quantized short clips may be ambiguous; every PTS validated)",info.nominalRateNum,info.nominalRateDen,rateNum,rateDen,info.timestampQuantum,samples.size(),rate.num,rate.den,options.fg?options.fgMultiplier:1,frameGenerationBackendName(options.settings.frameGenerationBackend),utf8(fgNote)));
        outputWidth=gd.workWidth;outputHeight=gd.workHeight;
        if(avformat_alloc_output_context2(&mux,nullptr,"mp4",utf8(partial).c_str())<0||!mux)break;
        videoStream=avformat_new_stream(mux,nullptr);if(!videoStream)break;videoStream->time_base={rate.den,rate.num};videoStream->avg_frame_rate=rate;
        auto* cp=videoStream->codecpar;cp->codec_type=AVMEDIA_TYPE_VIDEO;cp->codec_id=hevc?AV_CODEC_ID_HEVC:AV_CODEC_ID_H264;cp->width=gd.workWidth;cp->height=gd.workHeight;cp->format=AV_PIX_FMT_YUV420P;cp->color_range=AVCOL_RANGE_MPEG;cp->color_space=AVCOL_SPC_BT709;cp->color_primaries=AVCOL_PRI_BT709;cp->color_trc=AVCOL_TRC_IEC61966_2_1;
        if(gd.hdrOutput){cp->format=AV_PIX_FMT_YUV420P10LE;cp->color_space=AVCOL_SPC_BT2020_NCL;cp->color_primaries=AVCOL_PRI_BT2020;cp->color_trc=AVCOL_TRC_SMPTE2084;cp->profile=AV_PROFILE_HEVC_MAIN_10;}
        // Plan v5.3: this exporter cannot recompute content light levels (that
        // needs a full pre-pass before the header is written), so the source
        // declaration is carried into the 'clli' box verbatim and the graded case
        // is flagged as "not updated" instead of silently changing meaning.
        if(gd.hdrOutput&&info.color.hdrMaxCllNits>0.0f){
            if(auto* side=av_packet_side_data_new(&cp->coded_side_data,&cp->nb_coded_side_data,AV_PKT_DATA_CONTENT_LIGHT_LEVEL,sizeof(AVContentLightMetadata),0)){
                auto* cll=reinterpret_cast<AVContentLightMetadata*>(side->data);
                cll->MaxCLL=unsigned(info.color.hdrMaxCllNits);
                cll->MaxFALL=unsigned(info.color.hdrMaxFallNits);
            }else failureReason=L"无法写入内容亮度元数据（MaxCLL/MaxFALL）";
        }
        auto inputUtf8=utf8(input);
        if(avformat_open_input(&audioInput,inputUtf8.c_str(),nullptr,nullptr)<0||avformat_find_stream_info(audioInput,nullptr)<0){progress(0,L"无法读取源音轨信息，已停止导出");break;}
        {
            audioIndex=av_find_best_stream(audioInput,AVMEDIA_TYPE_AUDIO,-1,-1,nullptr,0);
            if(audioIndex>=0){auto* acp=audioInput->streams[audioIndex]->codecpar;
                if(avformat_query_codec(mux->oformat,acp->codec_id,FF_COMPLIANCE_NORMAL)<=0){progress(0,L"此音频编码不能封装为MP4，已停止导出");break;}
                audioStream=avformat_new_stream(mux,nullptr);if(!audioStream||avcodec_parameters_copy(audioStream->codecpar,acp)<0)break;audioStream->codecpar->codec_tag=0;audioStream->time_base=audioInput->streams[audioIndex]->time_base;
            }
            av_dict_copy(&mux->metadata,audioInput->metadata,0);
        }
        auto writeAudioUntil=[&](double seconds){if(!audioStream)return true;
            for(;;){if(!audioPending){if(audioEof)return true;av_packet_unref(audioPacket);const int readResult=av_read_frame(audioInput,audioPacket);if(readResult==AVERROR_EOF){audioEof=true;return true;}if(readResult<0){veyra::log::error("export-audio",std::format("source packet read failed code={}",readResult));return false;}if(audioPacket->stream_index!=audioIndex)continue;if(audioPacket->flags&AV_PKT_FLAG_CORRUPT){veyra::log::error("export-audio","corrupt source audio packet");return false;}audioPending=true;}
                const auto tb=audioInput->streams[audioIndex]->time_base;const int64_t ts=audioPacket->pts!=AV_NOPTS_VALUE?audioPacket->pts:audioPacket->dts;const double time=ts==AV_NOPTS_VALUE?0:ts*av_q2d(tb)-videoOriginSeconds;if(time>seconds)return true;
                const int64_t origin=av_rescale_q(static_cast<int64_t>(videoOriginSeconds*1000000),{1,1000000},tb);
                if(audioPacket->pts!=AV_NOPTS_VALUE)audioPacket->pts-=origin;if(audioPacket->dts!=AV_NOPTS_VALUE)audioPacket->dts-=origin;
                av_packet_rescale_ts(audioPacket,tb,audioStream->time_base);audioPacket->stream_index=audioStream->index;audioPacket->pos=-1;audioPending=false;const int rc=av_interleaved_write_frame(mux,audioPacket);if(rc<0)return failAv(L"写入音轨",rc);
            }};
        auto writer=[&](const uint8_t* bytes,size_t size,int64_t pts,bool key){if(!headerWritten)return false;
            AVPacket* pkt=av_packet_alloc();if(!pkt)return false;if(av_new_packet(pkt,int(size))<0){av_packet_free(&pkt);return false;}memcpy(pkt->data,bytes,size);pkt->stream_index=videoStream->index;pkt->pts=pkt->dts=pts;pkt->duration=1;if(key)pkt->flags|=AV_PKT_FLAG_KEY;
            av_packet_rescale_ts(pkt,{rate.den,rate.num},videoStream->time_base);const int rc=av_interleaved_write_frame(mux,pkt);av_packet_free(&pkt);if(rc<0)return failAv(L"写入视频帧",rc);++written;audioEndSeconds=(pts+1)*double(rate.den)/rate.num;return writeAudioUntil(audioEndSeconds);};
        // Must drain before rate/writeAudioUntil/writer leave scope, including cancel/error.
        struct EncoderCloser {sink::VideoEncoder* encoder;~EncoderCloser(){if(encoder)encoder->close();}} closer{encoder.get()};
        sink::EncoderConfig encoderConfig;encoderConfig.hevc=hevc;encoderConfig.fpsNum=unsigned(rate.num);encoderConfig.fpsDen=unsigned(rate.den);encoderConfig.bitrateMbps=options.settings.exportBitrateMbps;
        std::wstring encoderDetail;
        encoder=sink::openVideoEncoder(ctx,ring,graph,encoderConfig,writer,encoderDetail);
        if(!encoder){
            progress(0,encoderDetail.empty()?L"没有可用的视频编码器":encoderDetail);
            if(failureReason.empty())failureReason=encoderDetail.empty()?L"没有可用的视频编码器":encoderDetail;
            veyra::log::error("export","no usable video encoder for this adapter/codec");
            break;
        }
        encoderName=encoder->describe();
        veyra::log::info("export",std::format("encoder={} codec={} bitrateMbps={} rate={}/{}",std::string(sink::encoderBackendName(encoder->backend())),hevc?"HEVC":"H264",encoderConfig.bitrateMbps,rate.num,rate.den));
        auto headers=encoder->headers();cp->extradata=static_cast<uint8_t*>(av_mallocz(headers.size()+AV_INPUT_BUFFER_PADDING_SIZE));if(!cp->extradata)break;memcpy(cp->extradata,headers.data(),headers.size());cp->extradata_size=int(headers.size());
        int muxResult=avio_open(&mux->pb,utf8(partial).c_str(),AVIO_FLAG_WRITE);
        if(muxResult<0){failAv(L"创建输出文件",muxResult);break;}
        muxResult=avformat_write_header(mux,nullptr);
        if(muxResult<0){failAv(L"写入MP4文件头",muxResult);break;}headerWritten=true;
        uint64_t sourceCount=0,generatedCount=0,holdCount=0;int64_t outputIndex=0;bool error=false;std::shared_ptr<pipeline::FrameLease> lastReal;
        // Container duration x rate bounds the tail exception below: only the
        // last frames of the real stream may sit off the CFR grid.
        const double durationSeconds=info.duration.toDouble();
        const uint64_t estimatedFrames=durationSeconds>0?uint64_t(std::llround(durationSeconds*double(rateNum)/rateDen)):0;
        while(!cancel){if(frameBoundary&&!frameBoundary()){error=true;break;}pipeline::FramePacket packet;const AVFrame* frame=nullptr;
            source::SourceReadStatus rs;
            if(sourceCount==0){packet=firstPacket;frame=firstFrame;rs=source::SourceReadStatus::Frame;}
            else rs=source.read(packet,&frame);
            if(rs==source::SourceReadStatus::Eos)break;if(rs!=source::SourceReadStatus::Frame||packet.pts.isUnknown()){failureReason=source.errorMessage();error=true;break;}
            if(sourceCount==0)videoOriginSeconds=packet.pts.toDouble();
            const double expectedPts=timeline.expected(sourceCount);
            if(!timeline.accepts(sourceCount,packet.pts.toDouble())){
                const double pts=packet.pts.toDouble(),deviationMs=(pts-expectedPts)*1000.0;
                if(timeline.tailAccepts(sourceCount,pts,estimatedFrames)){
                    ++tailSnapped;
                    veyra::log::warn("export-timeline",std::format("tail snap source={} pts={} expected={} deviationMs={:.3f} estimatedFrames={} outputIndex={}",sourceCount,pts,expectedPts,deviationMs,estimatedFrames,outputIndex));
                } else if(const uint64_t missing=timeline.missingSlots(sourceCount,pts);missing>0&&lastReal){
                    // The source is missing whole samples here (a dropped frame
                    // that kept the timeline). Repeat the previous real frame for
                    // every missing slot so the exported grid stays continuous,
                    // then re-align the validator. No interpolation is invented
                    // for the hole and this is reported to the user, never silent.
                    const uint32_t multiplier=options.fg?options.fgMultiplier:1u;
                    veyra::log::warn("export-timeline",std::format(
                        "gap filled source={} pts={} expected={} deviationMs={:.3f} missingSlots={} outputIndex={} multiplier={} (previous frame repeated; no interpolation invented)",
                        sourceCount,pts,expectedPts,deviationMs,missing,outputIndex,multiplier));
                    ++gapFillEvents;gapFilledSlots+=missing;
                    bool fillOk=true;
                    for(uint64_t slotIndex=0;slotIndex<missing&&fillOk;++slotIndex){
                        for(uint32_t j=0;j<multiplier;++j){
                            if(!encoder->encode(lastReal->slot,false,outputIndex++)){fillOk=false;break;}
                            ++holdCount;
                        }
                    }
                    if(!fillOk){if(failureReason.empty())failureReason=L"缺口补帧编码失败，请查看编码器诊断";error=true;break;}
                    timeline.resync(sourceCount,pts);
                } else {
                    progress(0,std::format(L"源文件第 {} 帧时间戳偏移 {:.1f} 毫秒，无法按恒定帧率无损对齐；已停止写入并保留 partial",sourceCount,deviationMs));
                    veyra::log::error("export-timeline",std::format("CFR rejected source={} pts={} expected={} deviationMs={:.3f} estimatedFrames={} outputIndex={}",sourceCount,pts,expectedPts,deviationMs,estimatedFrames,outputIndex));error=true;break;
                }
            }
            pipeline::EnhanceGraph::FrameOutputs out;if(!graph.process(frame,packet.pts.toDouble()*1000,sourceCount==0||pipeline::breaksHistory(packet.flags),out,packet.sequence,&packet.colorInfo,&packet.hardwareSurface,false)){error=true;break;}
            const auto readyStart=std::chrono::steady_clock::now();
            while(!cancel&&!graph.resolveGeneration(out)){
                if(std::chrono::steady_clock::now()-readyStart>std::chrono::seconds(2)){error=true;break;}
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if(error||cancel)break;
            if(sourceCount>0&&options.fg){
                for(uint32_t j=1;j<options.fgMultiplier;++j){
                    pipeline::BatchFrame* item=nullptr;
                    for(uint32_t k=0;k<out.batch.count;++k)if(out.batch.frames[k].subframe==j&&out.batch.frames[k].validity==pipeline::GenerationValidity::Valid)item=&out.batch.frames[k];
                    if(item){if(!encoder->encode(item->lease->slot,true,outputIndex++)){error=true;break;}item->lease->consumerFence=ring.lastSignaledValue();++generatedCount;}
                    else {if(!lastReal||!encoder->encode(lastReal->slot,false,outputIndex++)){error=true;break;}lastReal->consumerFence=ring.lastSignaledValue();++holdCount;}
                }
            }
            if(error)break;
            auto& real=out.batch.frames[out.batch.count-1];
            if(!encoder->encode(real.lease->slot,false,outputIndex++)){error=true;break;}
            real.lease->consumerFence=ring.lastSignaledValue();lastReal=real.lease;
            ++sourceCount;if(counts)counts({sourceCount,generatedCount,holdCount,uint64_t(written)});progress(info.duration.toDouble()>0?std::clamp((packet.pts.toDouble()-videoOriginSeconds)/info.duration.toDouble(),0.0,.99):0,std::format(L"正在导出：{}张源帧 / {}张编码帧（{}）",sourceCount,outputIndex,encoderName));
            if(maxFrames&&sourceCount>=maxFrames)break;
        }
        if(error||cancel)break;
        if(options.fg&&lastReal)for(uint32_t j=1;j<options.fgMultiplier;++j){if(!encoder->encode(lastReal->slot,false,outputIndex++)){error=true;break;}++holdCount;}
        if(error)break;
        veyra::log::info("export-counts",std::format("source={} generated={} hold={} output={} multiplier={} tailSnapped={} gapFillEvents={} gapFilledSlots={} backend={} encoder={} bitrateMbps={} note={} (CFR holds are not DLSSG)",sourceCount,generatedCount,holdCount,outputIndex,options.fg?options.fgMultiplier:1,tailSnapped,gapFillEvents,gapFilledSlots,frameGenerationBackendName(options.settings.frameGenerationBackend),std::string(sink::encoderBackendName(encoder->backend())),options.settings.exportBitrateMbps,utf8(fgNote)));
        progress(.99,L"正在收尾：等待编码器输出剩余帧");
        if(!encoder->finish()){if(failureReason.empty())failureReason=L"编码器收尾失败，请查看编码器诊断";break;}
        if(!writeAudioUntil(audioEndSeconds))break;
        progress(.995,L"正在收尾：写入MP4索引");
        muxResult=av_write_trailer(mux);
        if(muxResult<0){failAv(L"写入MP4索引",muxResult);break;}
        ok=written>0;if(counts)counts({sourceCount,generatedCount,holdCount,uint64_t(written)});
    }while(false); }catch(const std::exception& e){veyra::log::error("export",std::format("exception: {}",e.what()));failureReason=L"导出异常，请查看诊断";ok=false;}
    encoder.reset();if(mux){if(mux->pb){const int rc=avio_closep(&mux->pb);if(rc<0){failAv(L"刷新并关闭输出文件",rc);ok=false;}}avformat_free_context(mux);}if(audioInput)avformat_close_input(&audioInput);av_packet_free(&audioPacket);
    ring.drainQueue();graph.shutdown();source.close();ring.shutdown();ctx.shutdown();
    if(ok){
        progress(.999,L"正在封装和验证输出");
        source::MediaFileSource check;source::SourceOpenDesc od;od.path=partial;od.preferHardwareDecode=false;
        ok=!cancel&&check.open(od);
        if(!ok&&!cancel){failureReason=L"无法重新打开输出验证："+check.errorMessage();veyra::log::error("export-verify",utf8(failureReason));}
        const auto info=check.info();
        CfrTimeline verifiedTimeline(outputRate.num,outputRate.den,info.timestampQuantum,0);
        const bool headerValid=info.width==outputWidth&&info.height==outputHeight&&verifiedTimeline.valid();
        if(ok&&!headerValid)veyra::log::error("export-verify",std::format("header actual={}x{} expected={}x{} rate={}/{} quantum={}",info.width,info.height,outputWidth,outputHeight,outputRate.num,outputRate.den,info.timestampQuantum));
        ok=ok&&headerValid;
        uint64_t decoded=0;bool reachedEos=false;
        while(ok&&!cancel){
            pipeline::FramePacket pkt;const AVFrame* frame=nullptr;const auto rs=check.read(pkt,&frame);
            if(rs==source::SourceReadStatus::Eos){reachedEos=true;break;}
            if(rs!=source::SourceReadStatus::Frame||pkt.pts.isUnknown()||decoded>=uint64_t(written)||!verifiedTimeline.accepts(decoded,pkt.pts.toDouble())){
                veyra::log::error("export-verify",std::format("frame={} status={} pts={} expectedPts={} written={} decoder={}",decoded,int(rs),pkt.pts.toDouble(),verifiedTimeline.expected(decoded),written,utf8(check.errorMessage())));
                ok=false;break;
            }
            if(hdrExport){
                const auto* format=av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame->format));
                if(!format||format->comp[0].depth<10||pkt.colorInfo.transfer!=pipeline::TransferFunction::PQ||
                   pkt.colorInfo.primaries!=pipeline::ColorPrimaries::BT2020||pkt.colorInfo.matrix!=pipeline::YuvMatrix::BT2020NCL){ok=false;break;}
            }
            ++decoded;
            if(decoded%120==0)progress(.999,std::format(L"正在逐帧验证：{} / {} 帧",decoded,written));
        }
        ok=ok&&!cancel&&reachedEos&&decoded==uint64_t(written);
        veyra::log::info("export-verify",std::format("decoded={} expected={} eof={} cancelled={} passed={}",decoded,written,reachedEos,bool(cancel),ok));
        check.close();
        if(!ok&&!cancel&&failureReason.empty())failureReason=std::format(L"输出验证未通过（解码 {} / {} 帧），详见export-verify日志",decoded,written);
        if(ok){
            progress(.999,L"验证通过，正在保存正式文件");
            ok=!cancel&&MoveFileExW(partial.c_str(),output.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE;
            if(!ok&&!cancel){const DWORD error=GetLastError();failureReason=std::format(L"视频验证已通过，但保存文件名失败（Windows错误 {}）；可保留partial文件",error);veyra::log::error("export-rename",std::format("MoveFileExW failed error={} partial={}",error,utf8(partial)));}
        }
    }
    if(ok){
        std::wstring done=encoderName.empty()?L"视频导出完成，逐帧完整性验证通过":std::format(L"视频导出完成（{}），逐帧完整性验证通过",encoderName);
        if(tailSnapped>0)done+=std::format(L"（尾部 {} 帧时间戳已按恒定帧率对齐）",tailSnapped);
        if(gapFilledSlots>0)done+=std::format(L"（源文件缺帧 {} 处/{} 帧，已按恒定帧率复制上一帧补齐）",gapFillEvents,gapFilledSlots);
        progress(1,fgNote.empty()?done:fgNote+L"；"+done);
    }
    else {
        std::wstring message=cancel?L"导出已取消":failureReason.empty()?L"视频导出失败，请查看诊断":failureReason;
        std::error_code ec;
        message+=std::filesystem::exists(partial,ec)?L"；partial文件已保留":L"；未生成输出文件";
        progress(0,message);
    }
    return ok;
}
}
