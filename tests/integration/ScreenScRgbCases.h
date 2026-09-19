#pragma once

// Synthetic GPU ingress isolates the HDR contract from desktop HDR hardware.
inline int screenScRgbCases(veyra::gfx::D3D12DeviceContext& ctx,veyra::gfx::CommandSlotRing& ring){
    using namespace veyra;using Microsoft::WRL::ComPtr;
    D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=64;td.Height=16;
    td.DepthOrArraySize=td.MipLevels=1;td.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;td.SampleDesc.Count=1;
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> texture,upload;
    if(FAILED(ctx.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&texture))))return 1;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;ctx.device()->GetCopyableFootprints(&td,0,1,0,&fp,nullptr,nullptr,&bytes);
    auto bd=td;bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=bytes;bd.Height=1;bd.Format=DXGI_FORMAT_UNKNOWN;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;hp.Type=D3D12_HEAP_TYPE_UPLOAD;
    if(FAILED(ctx.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload))))return 1;
    const float patches[4][4]={{0,0,0,1},{1.25f,1.25f,1.25f,1},{12.5f,12.5f,12.5f,1},{4,-.2f,.1f,1}};
    void* data=nullptr;if(FAILED(upload->Map(0,nullptr,&data)))return 1;
    for(unsigned y=0;y<16;++y)for(unsigned x=0;x<64;++x)for(unsigned c=0;c<4;++c)
        reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(data)+fp.Offset+y*fp.Footprint.RowPitch)[x*4+c]=DirectX::PackedVector::XMConvertFloatToHalf(patches[x/16][c]);
    upload->Unmap(0,nullptr);Status status;unsigned slot=0;auto* list=ring.acquireNext(slot,status);if(!list)return 1;
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=upload.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=fp;to.pResource=texture.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={texture.Get(),0,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON};list->ResourceBarrier(1,&barrier);
    if(!ring.submitAndSignal(slot)||!ring.drainQueue())return 1;
    AVFrame* frame=av_frame_alloc();frame->format=AV_PIX_FMT_D3D11;frame->width=64;frame->height=16;frame->buf[0]=av_buffer_alloc(1);frame->data[0]=frame->buf[0]->data;
    pipeline::ColorDescription color;color.scRgb=true;color.pixelFormat=pipeline::SourcePixelFormat::Rgba16F;color.range=pipeline::ColorRange::Full;color.transfer=pipeline::TransferFunction::Linear;color.primaries=pipeline::ColorPrimaries::BT709;color.matrix=pipeline::YuvMatrix::BT709;
    pipeline::HardwareSurfaceInput surface{texture.Get(),0,nullptr,0,64,16};int failures=0;
    for(bool native:{false,true})for(float exposure:{0.f,1.f}){
        pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc d;d.sourceWidth=d.workWidth=64;d.sourceHeight=d.workHeight=16;d.rgbInput=d.hdrInput=d.noFeatures=true;d.enableNr=d.enableSr=d.enableFg=false;d.hdrOutput=native;d.color.enabled=true;d.color.exposure=exposure;
        pipeline::EnhanceGraph::FrameOutputs out;bool ok=graph.initialize(d)&&graph.createViews()&&graph.process(frame,0,true,out,1,&color,&surface);double error=0;
        if(native){std::vector<float> actual;ok=ok&&captureReadFp16(ctx,ring,graph.diagnosticLinearInput(),actual);
            if(ok)for(unsigned p=0;p<4;++p)for(unsigned c=0;c<3;++c)error=std::max(error,double(std::abs(actual[(p*16+8)*4+c]-patches[p][c]*std::exp2(exposure))));
            ok=ok&&error<.02;
        }else{sink::RgbaImage image;ok=ok&&sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),image);
            if(ok)for(unsigned p=0;p<3;++p){const double linear=hdrToneTests::luminance(std::min(double(patches[p][0]*80*std::exp2(exposure)),1000.),1000);const double srgb=linear<=.0031308?12.92*linear:1.055*std::pow(linear,1/2.4)-.055;error=std::max(error,std::abs(image.pixels[(p*16+8)*4]/255.-srgb));}
            ok=ok&&error<.012;
        }
        std::cout<<"SCREEN_SCRGB native="<<native<<" exposure="<<exposure<<" error="<<error<<" pass="<<ok<<std::endl;failures+=!ok;out={};ring.drainQueue();graph.shutdown();
    }
    av_frame_free(&frame);return failures;
}
