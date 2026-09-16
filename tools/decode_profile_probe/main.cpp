// Read-only capability probe: which D3D12 Video Decode profiles does this
// GPU/driver expose? Run it on an NVIDIA, AMD and Intel machine - the answer
// decides whether the capture decoder can move to the Windows-native zero-copy
// path for every format (H.264/HEVC/AV1 and MJPEG/JPEG).
#include <windows.h>
#include <d3d12.h>
#include <d3d12video.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdio>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
struct Profile {
    const wchar_t* name;
    GUID guid;
    DXGI_FORMAT decodeFormat;
};

std::wstring formatName(DXGI_FORMAT format){
    switch(format){
    case DXGI_FORMAT_NV12:return L"NV12";
    case DXGI_FORMAT_P010:return L"P010";
    case DXGI_FORMAT_P016:return L"P016";
    default:return L"other";
    }
}
std::wstring supportFlags(D3D12_VIDEO_DECODE_SUPPORT_FLAGS flags){
    if(flags&D3D12_VIDEO_DECODE_SUPPORT_FLAG_SUPPORTED)return L"SUPPORTED";
    return L"unsupported";
}
std::wstring configurationFlags(D3D12_VIDEO_DECODE_CONFIGURATION_FLAGS flags){
    std::wstring text;
    if(flags&D3D12_VIDEO_DECODE_CONFIGURATION_FLAG_REFERENCE_ONLY_ALLOCATIONS_REQUIRED)text+=L" ref-only-alloc";
    if(flags&D3D12_VIDEO_DECODE_CONFIGURATION_FLAG_ALLOW_RESOLUTION_CHANGE_ON_NON_KEY_FRAME)text+=L" res-change";
    if(flags&D3D12_VIDEO_DECODE_CONFIGURATION_FLAG_POST_PROCESSING_SUPPORTED)text+=L" post-process";
    if(flags&D3D12_VIDEO_DECODE_CONFIGURATION_FLAG_HEIGHT_ALIGNMENT_MULTIPLE_32_REQUIRED)text+=L" h-align32";
    return text.empty()?L"-":text;
}
} // namespace

int wmain(){
    SetConsoleOutputCP(CP_UTF8);
    ComPtr<IDXGIFactory6> factory;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))){wprintf(L"CreateDXGIFactory1 failed\n");return 1;}
    ComPtr<IDXGIAdapter1> adapter;
    for(UINT index=0;factory->EnumAdapters1(index,&adapter)==S_OK;++index){
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)continue;
        ComPtr<ID3D12Device> device;
        if(FAILED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))))continue;
        wprintf(L"adapter[%u] %s vendor=0x%04X device=0x%04X\n",index,desc.Description,desc.VendorId,desc.DeviceId);
        ComPtr<ID3D12VideoDevice> video;
        if(FAILED(device.As(&video))){wprintf(L"  ID3D12VideoDevice not available (D3D12 video decode cannot be used here)\n");continue;}
        const Profile profiles[]={
            {L"H264",           D3D12_VIDEO_DECODE_PROFILE_H264,           DXGI_FORMAT_NV12},
            {L"HEVC_MAIN",      D3D12_VIDEO_DECODE_PROFILE_HEVC_MAIN,      DXGI_FORMAT_NV12},
            {L"HEVC_MAIN10",    D3D12_VIDEO_DECODE_PROFILE_HEVC_MAIN10,    DXGI_FORMAT_P010},
            {L"HEVC_MAIN10_422",D3D12_VIDEO_DECODE_PROFILE_HEVC_MAIN10_422,DXGI_FORMAT_P016},
            {L"AV1_PROFILE0",   D3D12_VIDEO_DECODE_PROFILE_AV1_PROFILE0,   DXGI_FORMAT_NV12},
            {L"VP9",            D3D12_VIDEO_DECODE_PROFILE_VP9,            DXGI_FORMAT_NV12},
            {L"MJPEG_VLD_420",  D3D12_VIDEO_DECODE_PROFILE_MJPEG_VLD_420,  DXGI_FORMAT_NV12},
            {L"MJPEG_VLD_422",  D3D12_VIDEO_DECODE_PROFILE_MJPEG_VLD_422,  DXGI_FORMAT_NV12},
            {L"MJPEG_VLD_444",  D3D12_VIDEO_DECODE_PROFILE_MJPEG_VLD_444,  DXGI_FORMAT_NV12},
            {L"JPEG_VLD_420",   D3D12_VIDEO_DECODE_PROFILE_JPEG_VLD_420,   DXGI_FORMAT_NV12},
            {L"JPEG_VLD_422",   D3D12_VIDEO_DECODE_PROFILE_JPEG_VLD_422,   DXGI_FORMAT_NV12},
        };
        for(const auto& profile:profiles){
            D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT support{};
            support.NodeIndex=0;
            support.Configuration.DecodeProfile=profile.guid;
            support.Width=3840;support.Height=2160;
            support.DecodeFormat=profile.decodeFormat;
            support.FrameRate={60,1};
            support.BitRate=100000000;
            const HRESULT hr=video->CheckFeatureSupport(D3D12_FEATURE_VIDEO_DECODE_SUPPORT,&support,sizeof(support));
            wprintf(L"  %-16s out=%-5s hr=0x%08X %-12s tier=%d flags=%s\n",profile.name,formatName(profile.decodeFormat).c_str(),unsigned(hr),
                supportFlags(support.SupportFlags).c_str(),int(support.DecodeTier),configurationFlags(support.ConfigurationFlags).c_str());
        }
    }
    return 0;
}
