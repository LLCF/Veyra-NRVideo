#include "veyra/source/CaptureCardSource.h"
#include "veyra/source/WasapiAudioInput.h"
#include "veyra/source/CaptureTiming.h"
#include "veyra/source/CaptureMediaType.h"
#include "veyra/source/NativeCaptureSink.h"
#include "veyra/pipeline/ColorMetadata.h"
#include "veyra/Log.h"
#include "veyra/sink/AudioFormat.h"
#include "veyra/sink/BitstreamAudio.h"
#include "veyra/sink/BitstreamAudioSink.h"
#include <windows.h>
#include <dshow.h>
#include <dvdmedia.h>
#include <ks.h>
#include <ksmedia.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <format>
#include <chrono>
#include <cmath>
#include <string_view>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
namespace veyra::source {
using Microsoft::WRL::ComPtr;
namespace {
// Legacy qedit interfaces are intentionally declared without obsolete qedit.h,
// which conflicts with modern D3D headers. ABI follows Microsoft DirectShow.
struct __declspec(uuid("0579154A-2B53-4994-B0D0-E773148EFF85")) ISampleGrabberCB: IUnknown {virtual HRESULT STDMETHODCALLTYPE SampleCB(double,IMediaSample*)=0;virtual HRESULT STDMETHODCALLTYPE BufferCB(double,BYTE*,long)=0;};
struct __declspec(uuid("6B652FFF-11FE-4FCE-92AD-0266B5D7C78F")) ISampleGrabber:IUnknown {virtual HRESULT STDMETHODCALLTYPE SetOneShot(BOOL)=0;virtual HRESULT STDMETHODCALLTYPE SetMediaType(const AM_MEDIA_TYPE*)=0;virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType(AM_MEDIA_TYPE*)=0;virtual HRESULT STDMETHODCALLTYPE SetBufferSamples(BOOL)=0;virtual HRESULT STDMETHODCALLTYPE GetCurrentBuffer(long*,long*)=0;virtual HRESULT STDMETHODCALLTYPE GetCurrentSample(IMediaSample**)=0;virtual HRESULT STDMETHODCALLTYPE SetCallback(ISampleGrabberCB*,long)=0;};
const CLSID SampleGrabberClass={0xc1f400a0,0x3f08,0x11d3,{0x9f,0x0b,0x00,0x60,0x08,0x03,0x9e,0x37}};
const CLSID NullRendererClass={0xc1f400a4,0x3f08,0x11d3,{0x9f,0x0b,0x00,0x60,0x08,0x03,0x9e,0x37}};
void freeType(AM_MEDIA_TYPE* t,bool pointer=true){if(!t)return;CoTaskMemFree(t->pbFormat);if(t->pUnk)t->pUnk->Release();if(pointer)CoTaskMemFree(t);}
std::wstring propertyString(IMoniker* moniker,LPCOLESTR property){
    if(!moniker)return {};
    ComPtr<IPropertyBag> bag;VARIANT value;VariantInit(&value);std::wstring result;
    if(SUCCEEDED(moniker->BindToStorage(nullptr,nullptr,IID_PPV_ARGS(&bag)))&&
       SUCCEEDED(bag->Read(property,&value,nullptr))&&value.vt==VT_BSTR&&value.bstrVal)result=value.bstrVal;
    VariantClear(&value);return result;
}
std::wstring monikerPath(IMoniker* moniker){
    if(auto path=propertyString(moniker,L"DevicePath");!path.empty())return path;
    ComPtr<IBindCtx> context;LPOLESTR display=nullptr;
    if(SUCCEEDED(CreateBindCtx(0,&context))&&SUCCEEDED(moniker->GetDisplayName(context.Get(),nullptr,&display))&&display){
        std::wstring path(display);CoTaskMemFree(display);return path;
    }
    return {};
}
bool audioOutputPin(IPin* pin){
    if(!pin)return false;PIN_DIRECTION direction{};if(FAILED(pin->QueryDirection(&direction))||direction!=PINDIR_OUTPUT)return false;
    ComPtr<IEnumMediaTypes> types;if(FAILED(pin->EnumMediaTypes(&types)))return false;
    for(;;){AM_MEDIA_TYPE* type=nullptr;const HRESULT hr=types->Next(1,&type,nullptr);if(hr!=S_OK||!type)break;const bool audio=type->majortype==MEDIATYPE_Audio;freeType(type);if(audio)return true;}
    return false;
}
HRESULT findAudioOutputPin(IBaseFilter* filter,ComPtr<IPin>& result){
    result.Reset();if(!filter)return E_POINTER;ComPtr<IEnumPins> pins;HRESULT hr=filter->EnumPins(&pins);if(FAILED(hr))return hr;
    ComPtr<IPin> fallback;
    for(;;){ComPtr<IPin> pin;hr=pins->Next(1,&pin,nullptr);if(hr!=S_OK)break;if(!audioOutputPin(pin.Get()))continue;
        if(!fallback)fallback=pin;
        // Prefer a pin explicitly classified as capture, but accept a driver
        // that omits the category and exposes only an audio output pin.
        ComPtr<IKsPropertySet> properties;if(SUCCEEDED(pin.As(&properties))){GUID category{};DWORD returned=0;
            if(SUCCEEDED(properties->Get(AMPROPSETID_Pin,AMPROPERTY_PIN_CATEGORY,nullptr,0,&category,sizeof(category),&returned))&&category==PIN_CATEGORY_CAPTURE){result=pin;return S_OK;}}
    }
    if(fallback){result=fallback;return S_OK;}return VFW_E_NOT_FOUND;
}
std::vector<ComPtr<IMoniker>> monikers(bool audio){std::vector<ComPtr<IMoniker>> out;ComPtr<ICreateDevEnum> de;ComPtr<IEnumMoniker> en;if(FAILED(CoCreateInstance(CLSID_SystemDeviceEnum,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&de)))||de->CreateClassEnumerator(audio?CLSID_AudioInputDeviceCategory:CLSID_VideoInputDeviceCategory,&en,0)!=S_OK)return out;for(;;){ComPtr<IMoniker> m;if(en->Next(1,&m,nullptr)!=S_OK)break;out.push_back(m);}return out;}
bool bind(unsigned index,bool audio,ComPtr<IBaseFilter>& filter){auto list=monikers(audio);return index<list.size()&&SUCCEEDED(list[index]->BindToObject(nullptr,nullptr,IID_PPV_ARGS(&filter)));}
bool bindPath(std::wstring_view wanted,bool audio,ComPtr<IBaseFilter>& filter){
    if(wanted.empty())return false;for(auto& moniker:monikers(audio))if(monikerPath(moniker.Get())==wanted)return SUCCEEDED(moniker->BindToObject(nullptr,nullptr,IID_PPV_ARGS(&filter)));return false;
}
bool createConfiguration(ComPtr<IGraphBuilder>& g,ComPtr<ICaptureGraphBuilder2>& b){return SUCCEEDED(CoCreateInstance(CLSID_FilterGraph,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&g)))&&SUCCEEDED(CoCreateInstance(CLSID_CaptureGraphBuilder2,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&b)))&&SUCCEEDED(b->SetFiltergraph(g.Get()));}
bool configuration(unsigned device,ComPtr<IGraphBuilder>& g,ComPtr<ICaptureGraphBuilder2>& b,ComPtr<IBaseFilter>& f,ComPtr<IAMStreamConfig>& c){return createConfiguration(g,b)&&bind(device,false,f)&&SUCCEEDED(g->AddFilter(f.Get(),L"Capture card"))&&SUCCEEDED(b->FindInterface(&PIN_CATEGORY_CAPTURE,&MEDIATYPE_Video,f.Get(),IID_PPV_ARGS(&c)));}
bool configuration(std::wstring_view path,ComPtr<IGraphBuilder>& g,ComPtr<ICaptureGraphBuilder2>& b,ComPtr<IBaseFilter>& f,ComPtr<IAMStreamConfig>& c){return createConfiguration(g,b)&&bindPath(path,false,f)&&SUCCEEDED(g->AddFilter(f.Get(),L"Capture card"))&&SUCCEEDED(b->FindInterface(&PIN_CATEGORY_CAPTURE,&MEDIATYPE_Video,f.Get(),IID_PPV_ARGS(&c)));}
HRESULT audioPinFor(ICaptureGraphBuilder2* builder,IBaseFilter* filter,ComPtr<IPin>& pin){
    if(!builder||!filter)return E_POINTER;const HRESULT categorized=builder->FindPin(filter,PINDIR_OUTPUT,&PIN_CATEGORY_CAPTURE,&MEDIATYPE_Audio,FALSE,0,&pin);
    if(SUCCEEDED(categorized))return categorized;
    pin.Reset();return findAudioOutputPin(filter,pin);
}
std::wstring encodePath(std::wstring_view value){
    static constexpr wchar_t digits[]=L"0123456789ABCDEF";std::wstring encoded;encoded.reserve(value.size()*4);
    for(const wchar_t character:value){const uint16_t unit=static_cast<uint16_t>(character);for(int shift=12;shift>=0;shift-=4)encoded.push_back(digits[(unit>>shift)&0xF]);}return encoded;
}
std::string pathTag(std::wstring_view value){const auto encoded=encodePath(value);std::string tag;tag.reserve(encoded.size());for(const wchar_t character:encoded)tag.push_back(static_cast<char>(character));return tag;}
int hexValue(wchar_t character){if(character>=L'0'&&character<=L'9')return character-L'0';if(character>=L'A'&&character<=L'F')return character-L'A'+10;if(character>=L'a'&&character<=L'f')return character-L'a'+10;return -1;}
bool decodePath(std::wstring_view encoded,std::wstring& value){
    if(encoded.size()%4!=0)return false;value.clear();value.reserve(encoded.size()/4);
    for(size_t i=0;i<encoded.size();i+=4){int unit=0;for(size_t j=0;j<4;++j){const int nibble=hexValue(encoded[i+j]);if(nibble<0)return false;unit=(unit<<4)|nibble;}value.push_back(static_cast<wchar_t>(unit));}return true;
}
bool parseInt(std::wstring_view text,int& value){
    if(text.empty())return false;const std::wstring copy(text);size_t consumed=0;try{value=std::stoi(copy,&consumed);}catch(...){return false;}return consumed==copy.size();
}
bool parseUnsigned(std::wstring_view text,unsigned& value){int parsed=0;if(!parseInt(text,parsed)||parsed<0)return false;value=static_cast<unsigned>(parsed);return true;}
struct CaptureSelection {unsigned videoIndex=0;int format=0;int audio=kCaptureAudioDisabled;unsigned colorOverride=0;bool stable=false;std::wstring videoPath,audioPath;};
bool parseCapturePath(std::wstring_view path,CaptureSelection& selection){
    selection={};
    constexpr std::wstring_view prefix=L"capture2:";
    if(path.starts_with(prefix)){
        std::array<std::wstring_view,5> fields{};size_t cursor=prefix.size();
        for(size_t i=0;i<fields.size();++i){const size_t end=path.find(L':',cursor);if(i+1<fields.size()){if(end==std::wstring_view::npos)return false;fields[i]=path.substr(cursor,end-cursor);cursor=end+1;}else{if(end!=std::wstring_view::npos)return false;fields[i]=path.substr(cursor);}}
        if(fields[0].empty()||!decodePath(fields[0],selection.videoPath)||!parseInt(fields[1],selection.format)||!parseInt(fields[2],selection.audio)||!decodePath(fields[3],selection.audioPath)||!parseUnsigned(fields[4],selection.colorOverride)||selection.format<0||selection.colorOverride>2)return false;
        if(selection.audio!=kCaptureAudioDisabled&&selection.audio!=kCaptureAudioFromVideoDevice&&selection.audio!=kCaptureAudioWasapi&&selection.audio<0)return false;
        if((selection.audio>=0||selection.audio==kCaptureAudioWasapi)&&selection.audioPath.empty())return false;selection.stable=true;return true;
    }
    unsigned videoIndex=0,colorOverride=0;int format=0,audio=kCaptureAudioDisabled;const std::wstring legacy(path);
    const int fields=swscanf_s(legacy.c_str(),L"capture:%u:%d:%d:%u",&videoIndex,&format,&audio,&colorOverride);
    if(fields<3||format<0||audio<kCaptureAudioFromVideoDevice||colorOverride>2)return false;
    selection.videoIndex=videoIndex;selection.format=format;selection.audio=audio;selection.colorOverride=fields>=4?colorOverride:0;return true;
}
}
struct CaptureCardSource::Impl:ISampleGrabberCB {
    using Clock=std::chrono::steady_clock;
    std::atomic<ULONG> refs{1};std::mutex mutex;std::condition_variable wake;
    // One pending frame plus one reader-owned frame, never an IMediaSample
    // reference. Holding the producer's sole RGB32 sample starves its allocator.
    AVFrame* frame=nullptr;AVFrame* pendingFrame=nullptr;bool pending=false,callbackError=false,configured=false;
    double pendingTime=0,lastPts=0,readAgeMs=0;bool pendingDiscontinuity=false,forceDiscontinuity=false;
    int64_t nominalDuration100ns=0;pipeline::Rational pendingDuration=pipeline::Rational::unknown();
    uint64_t received=0,dropped=0,lastDrop=0,sequence=0;
    Clock::time_point pendingArrival{},readArrival{},firstArrival{},latestArrival{};
    ComPtr<IGraphBuilder> graph;ComPtr<ICaptureGraphBuilder2> builder;ComPtr<IBaseFilter> device,grabFilter,nullFilter,audioFilter;ComPtr<IAMStreamConfig> config;ComPtr<ISampleGrabber> grab;ComPtr<IMediaControl> control;ComPtr<IMediaEvent> events;
    float lastAudioGain=-1;bool audioGainSupported=false;
    ComPtr<IBaseFilter> audioSink;ComPtr<IReferenceClock> referenceClock;
    std::unique_ptr<sink::CaptureAudioSession> audioSession;
    // Dolby/DTS passthrough: when the device offers only compressed media types
    // the raw bursts are decoded here and the session is configured with the
    // decoded layout on the first frame (that is why its start is deferred).
    std::shared_ptr<sink::BitstreamDecoder> audioBitstream;
    // Bitstream-first mode: the compressed stream is forwarded unmodified to a
    // receiver over an exclusive WASAPI IEC 61937 carrier when one accepts it.
    std::shared_ptr<sink::BitstreamAudioSink> audioPassthrough;
    bool audioSessionDeferred=false;
    std::wstring audioBitstreamKind;
    // 0 automatic, 1 PCM only, 2 bitstream preferred (see
    // engine::CaptureAudioIngress). Read when the audio graph is built.
    unsigned audioIngressMode=0;
    std::unique_ptr<WasapiAudioInput> wasapi;
    std::wstring audioError;
    AudioInputRecovery audioRecovery;
    SourceInfo info;CaptureMediaLayout layout;Clock::time_point lastFrame;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** pp)override{if(!pp)return E_POINTER;*pp=nullptr;if(id==IID_IUnknown||id==__uuidof(ISampleGrabberCB)){*pp=static_cast<ISampleGrabberCB*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{return --refs;}
    HRESULT STDMETHODCALLTYPE SampleCB(double time,IMediaSample* sample)override{
        const auto arrival=Clock::now();BYTE* data=nullptr;
        REFERENCE_TIME sampleStart=0,sampleEnd=0;
        const bool sampleTime=sample&&sample->GetTime(&sampleStart,&sampleEnd)==S_OK;
        const bool valid=sample&&std::isfinite(time)&&SUCCEEDED(sample->GetPointer(&data))&&data&&
            sample->GetActualDataLength()>=LONG(layout.sampleBytes);
        {
            std::lock_guard lock(mutex);
            if(!valid||!pendingFrame){callbackError=true;}
            else {
                // Copy directly into our bounded mailbox; read() swaps frames
                // under this lock, so the frame consumed by the GPU is untouched.
                if(!copyCaptureSample(layout,data,size_t(sample->GetActualDataLength()),*pendingFrame)){callbackError=true;wake.notify_one();return S_OK;}
                if(pending)++dropped;
                // Inspect consecutive callbacks, not consecutive mailbox reads.
                // Preserve a driver/clock break when its sample is overwritten.
                pendingDiscontinuity=captureDiscontinuity(pending,pendingDiscontinuity,
                    sample->IsDiscontinuity()==S_OK,received>0,pendingTime,time,info.averageFps);
                pending=true;pendingTime=time;pendingArrival=arrival;
                pendingDuration=captureDuration(sampleStart,sampleEnd,sampleTime,nominalDuration100ns);
                if(!received)firstArrival=arrival;
                ++received;latestArrival=arrival;
            }
        }
        wake.notify_one();return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BufferCB(double,BYTE*,long)override{return E_NOTIMPL;}
};
CaptureCardSource::CaptureCardSource():p_(std::make_unique<Impl>()){}
CaptureCardSource::~CaptureCardSource(){close();}
std::vector<CaptureDevice> CaptureCardSource::deviceDetails(bool audio){
    std::vector<CaptureDevice> result;
    for(auto& moniker:monikers(audio)){
        CaptureDevice device;device.name=propertyString(moniker.Get(),L"FriendlyName");
        if(device.name.empty())device.name=L"Unknown capture device";
        device.path=monikerPath(moniker.Get());
        if(!audio){ComPtr<IBaseFilter> filter;ComPtr<IPin> audioPin;
            if(SUCCEEDED(moniker->BindToObject(nullptr,nullptr,IID_PPV_ARGS(&filter)))&&SUCCEEDED(findAudioOutputPin(filter.Get(),audioPin)))device.hasEmbeddedAudio=true;
        }
        result.push_back(std::move(device));
    }
    if(audio)for(auto& endpoint:WasapiAudioInput::devices())result.push_back({std::move(endpoint.name),std::move(endpoint.id),false,true});
    return result;
}
std::vector<std::wstring> CaptureCardSource::devices(bool audio){std::vector<std::wstring> result;for(auto& device:deviceDetails(audio))result.push_back(std::move(device.name));return result;}
std::wstring CaptureCardSource::makeCapturePath(unsigned videoIndex,const CaptureDevice& video,int format,int audioMode,const CaptureDevice* audio,unsigned colorOverride){
    if(audio&&audio->wasapi){
        if(video.path.empty()||audio->path.empty())return {};
        return std::format(L"capture2:{}:{}:{}:{}:{}",encodePath(video.path),format,kCaptureAudioWasapi,encodePath(audio->path),colorOverride);
    }
    if(video.path.empty()||(audioMode>=0&&(!audio||audio->path.empty())))return std::format(L"capture:{}:{}:{}:{}",videoIndex,format,audioMode,colorOverride);
    return std::format(L"capture2:{}:{}:{}:{}:{}",encodePath(video.path),format,audioMode,audioMode>=0?encodePath(audio->path):L"",colorOverride);
}
std::vector<CaptureFormat> enumerateFormats(IAMStreamConfig* config){
    std::vector<CaptureFormat> out;if(!config)return out;int count=0,size=0;
    if(FAILED(config->GetNumberOfCapabilities(&count,&size))||size<1||size>65536)return out;
    std::vector<BYTE> caps(size);
    for(int i=0;i<count;++i){AM_MEDIA_TYPE* type=nullptr;if(FAILED(config->GetStreamCaps(i,&type,caps.data())))continue;BITMAPINFOHEADER* bitmap=nullptr;REFERENCE_TIME duration=0;
        if(type->formattype==FORMAT_VideoInfo&&type->cbFormat>=sizeof(VIDEOINFOHEADER)){auto* info=reinterpret_cast<VIDEOINFOHEADER*>(type->pbFormat);bitmap=&info->bmiHeader;duration=info->AvgTimePerFrame;}
        else if(type->formattype==FORMAT_VideoInfo2&&type->cbFormat>=sizeof(VIDEOINFOHEADER2)){auto* info=reinterpret_cast<VIDEOINFOHEADER2*>(type->pbFormat);bitmap=&info->bmiHeader;duration=info->AvgTimePerFrame;}
        // Device capabilities, not a 1080p/2160p 30/60 preset list. Keep native
        // indices so the selected row opens the exact driver media type.
        if(bitmap&&bitmap->biWidth>0&&bitmap->biWidth<=3840&&std::abs(int64_t(bitmap->biHeight))>0&&std::abs(int64_t(bitmap->biHeight))<=2160&&duration>0){unsigned width=bitmap->biWidth,height=unsigned(std::abs(int64_t(bitmap->biHeight)));double fps=1e7/duration;
            const auto pixel=capturePixelName(type->subtype);CaptureMediaLayout layout;const bool valid=captureMediaLayout(*type,layout);const bool knownRaw=capturePacking(type->subtype)!=CapturePacking::Unknown;
            const wchar_t* support=valid?((layout.format==AV_PIX_FMT_P010||layout.format==AV_PIX_FMT_P016)?L"原生 · SDR":L"原生"):knownRaw?L"布局/颜色暂不支持":L"需系统解码/转换";
            wchar_t subtype[40]{},formatType[40]{};StringFromGUID2(type->subtype,subtype,40);StringFromGUID2(type->formattype,formatType,40);
            const auto key=std::format(L"{}:{}:{}:{}:{}:{}:{}",width,bitmap->biHeight,duration,subtype,formatType,bitmap->biBitCount,bitmap->biCompression);
            out.push_back({i,width,height,fps,std::format(L"{} x {} @ {:.2f} fps · {} · {} [format {}]",width,height,fps,pixel,support,i),key});
        }
        freeType(type);
    }
    return out;
}
std::vector<CaptureFormat> CaptureCardSource::formats(unsigned device){ComPtr<IGraphBuilder> g;ComPtr<ICaptureGraphBuilder2>b;ComPtr<IBaseFilter>f;ComPtr<IAMStreamConfig>c;if(!configuration(device,g,b,f,c))return {};return enumerateFormats(c.Get());}
std::vector<CaptureFormat> CaptureCardSource::formatsByPath(std::wstring_view devicePath){ComPtr<IGraphBuilder> g;ComPtr<ICaptureGraphBuilder2>b;ComPtr<IBaseFilter>f;ComPtr<IAMStreamConfig>c;if(!configuration(devicePath,g,b,f,c))return {};return enumerateFormats(c.Get());}
const SourceInfo& CaptureCardSource::info()const{return p_->info;}
void CaptureCardSource::setAudioIngress(unsigned mode){
    auto& p=*p_;
    const unsigned clamped=mode>2?0:mode;
    if(p.audioIngressMode==clamped)return;
    p.audioIngressMode=clamped;
    log::info("capture-audio-ingress",std::format("mode={} ({}) takes effect on the next connect",clamped,
        clamped==1?"PCM only":clamped==2?"bitstream preferred":"automatic"));
}
bool CaptureCardSource::setAudioGain(float gain){
    if(p_->wasapi){p_->wasapi->setGain(gain);return p_->wasapi->snapshot().available;}
    auto& p=*p_;if(p.audioSession){p.audioSession->setGain(gain);return p.audioSession->snapshot().available;}if(!p.graph||!p.audioFilter)return false;
    if(gain==p.lastAudioGain)return p.audioGainSupported;
    ComPtr<IBasicAudio> audio;HRESULT hr=p.graph.As(&audio);
    if(SUCCEEDED(hr)){long attenuation=gain<=0?-10000:long(std::clamp(2000.0*std::log10(double(gain)),-10000.0,0.0));hr=audio->put_Volume(attenuation);}
    p.lastAudioGain=gain;p.audioGainSupported=SUCCEEDED(hr);log::info("capture-audio",std::format("application gain={} hr=0x{:X}",gain,unsigned(hr)));return p.audioGainSupported;
}
void CaptureCardSource::videoPresented(double pts,int64_t time,int64_t arrival){
    if(p_->wasapi)p_->wasapi->videoPresented(double(arrival)/10000,time,arrival);
    else if(p_->audioSession)p_->audioSession->videoPresented(pts,time,arrival);
}
void CaptureCardSource::videoReset(bool resetAudio){if(p_->wasapi)p_->wasapi->videoReset(resetAudio);else if(p_->audioSession)p_->audioSession->videoReset(resetAudio);}
void CaptureCardSource::setAudioSync(unsigned mode,int offset){if(p_->wasapi)p_->wasapi->setSync(mode,offset);else if(p_->audioSession)p_->audioSession->setSync(mode,offset);}
sink::CaptureAudioState CaptureCardSource::audioState()const{auto state=p_->wasapi?p_->wasapi->snapshot():p_->audioSession?p_->audioSession->snapshot():sink::CaptureAudioState{};if(!p_->audioError.empty())state.error=p_->audioError;return state;}
bool CaptureCardSource::open(const SourceOpenDesc& desc){return configure(desc)&&start();}
bool CaptureCardSource::configure(const SourceOpenDesc& desc){close();p_->lastAudioGain=-1;auto& p=*p_;CaptureSelection selection;if(!parseCapturePath(desc.path,selection))return false;const unsigned index=selection.videoIndex;const int format=selection.format;const int audio=selection.audio;
    if(selection.stable?!configuration(selection.videoPath,p.graph,p.builder,p.device,p.config):!configuration(index,p.graph,p.builder,p.device,p.config))return false;
    int count=0,size=0;if(FAILED(p.config->GetNumberOfCapabilities(&count,&size))||format<0||format>=count||size<=0||size>65536)return false;
    std::vector<BYTE> caps(size);AM_MEDIA_TYPE* native=nullptr;if(FAILED(p.config->GetStreamCaps(format,&native,caps.data())))return false;
    HRESULT hr=p.config->SetFormat(native);const GUID requestedSubtype=native->subtype;
    log::info("capture",std::format("SetFormat device={} nativeIndex={} subtype=0x{:08X} hr=0x{:08X}",index,format,native->subtype.Data1,uint32_t(hr)));freeType(native);if(FAILED(hr))return false;
    // Read the driver-negotiated type back. Native YUY2/NV12/RGB32 connects
    // directly to our terminal filter: no intelligent-connect converter.
    native=nullptr;hr=p.config->GetFormat(&native);if(FAILED(hr)||!native){freeType(native);return false;}
    CaptureMediaLayout nativeLayout;const bool nativeSupported=captureMediaLayout(*native,nativeLayout);
    if(!nativeSupported&&capturePacking(native->subtype)!=CapturePacking::Unknown){log::error("capture","Known raw format has unsupported layout/color metadata; refusing implicit RGB conversion");freeType(native);return false;}
    if(nativeSupported&&(nativeLayout.format==AV_PIX_FMT_P010||nativeLayout.format==AV_PIX_FMT_P016)&&desc.legacyCaptureRgbForDiagnostic){log::error("capture","10/16-bit capture cannot use the legacy 8-bit RGB diagnostic converter");freeType(native);return false;}
    const bool direct=nativeSupported&&!desc.legacyCaptureRgbForDiagnostic;
    AM_MEDIA_TYPE connected{};
    if(direct){
        ComPtr<IPin> input,output;
        hr=createNativeCaptureSink(*native,[&p](IMediaSample* sample){REFERENCE_TIME a=0,b=0;const auto timeHr=sample->GetTime(&a,&b);if(FAILED(timeHr))return timeHr;return p.SampleCB(double(a)/1e7,sample);},p.grabFilter,input);
        if(SUCCEEDED(hr))hr=p.graph->AddFilter(p.grabFilter.Get(),L"Native frame mailbox");
        if(SUCCEEDED(hr))hr=p.builder->FindPin(p.device.Get(),PINDIR_OUTPUT,&PIN_CATEGORY_CAPTURE,&MEDIATYPE_Video,FALSE,0,&output);
        if(SUCCEEDED(hr))hr=p.graph->ConnectDirect(output.Get(),input.Get(),native);
        if(SUCCEEDED(hr))hr=input->ConnectionMediaType(&connected);
        log::info("capture",std::format("native ConnectDirect subtype=0x{:08X} hr=0x{:08X} converters=0",native->subtype.Data1,uint32_t(hr)));
    }else{
        log::warn("capture",std::format("explicit RGB32 compatibility path subtype=0x{:08X} diagnostic={} (decoder/color converter may be inserted)",requestedSubtype.Data1,desc.legacyCaptureRgbForDiagnostic));
        hr=CoCreateInstance(SampleGrabberClass,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&p.grabFilter));
        if(SUCCEEDED(hr))hr=p.grabFilter.As(&p.grab);
        if(SUCCEEDED(hr))hr=p.graph->AddFilter(p.grabFilter.Get(),L"Decoded RGB compatibility mailbox");
        AM_MEDIA_TYPE want{};want.majortype=MEDIATYPE_Video;want.subtype=MEDIASUBTYPE_RGB32;want.formattype=FORMAT_VideoInfo;
        if(SUCCEEDED(hr))hr=p.grab->SetMediaType(&want);
        if(SUCCEEDED(hr))hr=CoCreateInstance(NullRendererClass,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&p.nullFilter));
        if(SUCCEEDED(hr))hr=p.graph->AddFilter(p.nullFilter.Get(),L"Video sink");
        if(SUCCEEDED(hr))hr=p.builder->RenderStream(&PIN_CATEGORY_CAPTURE,&MEDIATYPE_Video,p.device.Get(),p.grabFilter.Get(),p.nullFilter.Get());
        if(SUCCEEDED(hr))hr=p.grab->GetConnectedMediaType(&connected);
    }
    freeType(native);const bool layoutValid=SUCCEEDED(hr)&&captureMediaLayout(connected,p.layout);freeType(&connected,false);
    if(!layoutValid){log::error("capture",std::format("unsupported negotiated layout/connect failure hr=0x{:08X}",uint32_t(hr)));return false;}
    const unsigned colorOverride=selection.colorOverride;
    if(colorOverride>2)return false;
    if(colorOverride){
        if(p.layout.format!=AV_PIX_FMT_P010&&p.layout.format!=AV_PIX_FMT_P016){log::error("capture-color","Explicit HDR requires P010/P016; select a 10/16-bit capture format");return false;}
        p.layout.color.transfer=colorOverride==1?pipeline::TransferFunction::PQ:pipeline::TransferFunction::HLG;
        p.layout.color.matrix=pipeline::YuvMatrix::BT2020NCL;p.layout.color.primaries=pipeline::ColorPrimaries::BT2020;
        p.layout.color.transferAssumed=p.layout.color.matrixAssumed=p.layout.color.primariesAssumed=false;
        log::info("capture-color",std::format("manual override={} BT2020; range retains negotiated metadata",colorOverride==1?"PQ":"HLG"));
    }
    p.info={};p.info.kind=pipeline::SourceKind::CaptureCard;p.info.width=p.layout.width;p.info.height=p.layout.height;p.info.averageFps=p.layout.duration>0?1e7/p.layout.duration:0;p.info.duration=pipeline::Rational::unknown();p.info.color=p.layout.color;
    p.nominalDuration100ns=p.layout.duration;
    log::info("capture-color",std::format("format={} stride={} rowBytes={} bytes={} bottomUp={} matrix={} assumed={} range={} assumed={} workingTransfer={} assumed={} (explicit transfer contract)",int(p.layout.format),p.layout.stride,p.layout.rowBytes,p.layout.sampleBytes,p.layout.bottomUp,int(p.info.color.matrix),p.info.color.matrixAssumed,int(p.info.color.range),p.info.color.rangeAssumed,int(p.info.color.transfer),p.info.color.transferAssumed));
    if(audio==kCaptureAudioWasapi){
        p.wasapi=std::make_unique<WasapiAudioInput>();
        if(!p.wasapi->configure(selection.audioPath)){p.wasapi.reset();p.audioError=L"WASAPI 音频端点ID无效；视频继续运行";}
        log::info("capture-audio","binding=wasapi shared=1 explicitEndpoint=1 videoClock=ingress-host-estimate");
    }else if(audio!=kCaptureAudioDisabled){
        const bool audioReady=connectDirectShowAudio(desc);
        if(!audioReady){
            p.audioError=L"采集音频设备或 PCM 格式不可用；视频继续运行";
            log::warn("capture-audio","audio connection unavailable; retaining video capture");
            if(p.audioFilter)p.graph->RemoveFilter(p.audioFilter.Get());
            p.audioFilter.Reset();
        }
        // Pin the common graph clock before Run; removing DirectShow's audio
        // renderer must not silently change the capture graph's reference.
        ComPtr<IMediaFilter> mediaFilter;
        if(FAILED(CoCreateInstance(CLSID_SystemClock,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&p.referenceClock)))||
            FAILED(p.graph.As(&mediaFilter))||FAILED(mediaFilter->SetSyncSource(p.referenceClock.Get())))return false;
    }
    if(FAILED(p.graph.As(&p.control))||FAILED(p.graph.As(&p.events)))return false;
    if(p.grab&&(FAILED(p.grab->SetBufferSamples(FALSE))||FAILED(p.grab->SetCallback(&p,0))))return false;
    for(auto** f:{&p.frame,&p.pendingFrame}){
        *f=av_frame_alloc();if(!*f)return false;
        (*f)->format=p.layout.format;(*f)->width=p.info.width;(*f)->height=p.info.height;
        if(av_frame_get_buffer(*f,32)<0)return false;
    }
    p.configured=true;
    reconnectDesc_=desc;reconnectInfo_=p.info;reconnectFormat_.clear();
    for(const auto& candidate:enumerateFormats(p.config.Get()))if(candidate.index==format){reconnectFormat_=candidate.key;break;}
    // Log the upstream type after DirectShow has finished negotiation. The
    // RGB32 output's nominal FPS alone is not proof of actual callback cadence.
    AM_MEDIA_TYPE* actual=nullptr;const auto formatHr=p.config->GetFormat(&actual);
    veyra::log::info("capture",std::format("configured {}x{} nominalFps={:.3f} upstreamSubtype=0x{:08X} formatHr=0x{:X} mailbox=1 ownedBuffers=2 deferredRun=1 audioDevice={}",
        p.info.width,p.info.height,p.info.averageFps,actual?unsigned(actual->subtype.Data1):0,unsigned(formatHr),audio));freeType(actual);
    return true;
}
bool CaptureCardSource::connectDirectShowAudio(const SourceOpenDesc& desc){
    auto& p=*p_;CaptureSelection selection;if(!parseCapturePath(desc.path,selection))return false;
    const int audio=selection.audio;HRESULT hr=S_OK;
    ComPtr<IBaseFilter> audioFilter;const bool embedded=audio==kCaptureAudioFromVideoDevice;
    if(embedded){
        // Some capture cards expose video and HDMI audio on one
        // DirectShow filter. OBS calls this "use video device". Do not
        // AddFilter/RemoveFilter here: p.device owns the graph filter.
        audioFilter=p.device;log::info("capture-audio",std::format("binding=video-filter embedded=1 videoPathTag={}",pathTag(selection.videoPath)));
    }else{
        const bool bound=selection.stable?bindPath(selection.audioPath,true,audioFilter):bind(unsigned(audio),true,audioFilter);
        if(!bound){log::warn("capture-audio",std::format("binding=separate failed mode={} audioIndex={} audioPathTag={}",audio,audio,pathTag(selection.audioPath)));return false;}
        hr=p.graph->AddFilter(audioFilter.Get(),L"Capture audio");
        if(FAILED(hr)){log::warn("capture-audio",std::format("binding=separate AddFilter hr=0x{:08X}",uint32_t(hr)));return false;}
        p.audioFilter=audioFilter;log::info("capture-audio",std::format("binding=separate embedded=0 audioIndex={} audioPathTag={}",audio,audio,pathTag(selection.audioPath)));
    }
    ComPtr<IPin> audioPin;hr=audioPinFor(p.builder.Get(),audioFilter.Get(),audioPin);
    if(FAILED(hr)){log::warn("capture-audio",std::format("audio output pin not found embedded={} hr=0x{:08X}",embedded?1:0,uint32_t(hr)));return false;}
    ComPtr<IEnumMediaTypes> types;hr=audioPin->EnumMediaTypes(&types);if(FAILED(hr)){log::warn("capture-audio",std::format("EnumMediaTypes hr=0x{:08X}",uint32_t(hr)));return false;}
    // Preserve the device's actual speaker layout. Enumeration order is
    // commonly stereo first even when native 5.1 is available.
    auto releaseType=[](AM_MEDIA_TYPE* type){freeType(type);};
    using AudioType=std::unique_ptr<AM_MEDIA_TYPE,decltype(releaseType)>;
std::vector<AudioType> audioTypes;std::vector<AudioType> bitstreamTypes;unsigned typeIndex=0;
// Dolby/DTS bitstream capability probe: the capture card may expose AC-3 /
// E-AC-3 (Dolby Digital Plus, includes Atmos over DD+) / TrueHD / DTS instead
// of PCM. Veyra currently consumes PCM only, so a compressed stream is reported
// here (and then ignored) instead of being silently mis-parsed. Dolby passthrough
// work builds on this inventory.
unsigned bitstreamTypeCount=0;std::string bitstreamSummary;
auto bitstreamName=[](const GUID& subtype)->const char*{
    switch(subtype.Data1){
    case 0x00000092u:return "AC-3(SPDIF)";
    case 0x00002000u:return "AC-3";
    case 0x0000000Au:return "E-AC-3/DD+";
    case 0x0000010Au:return "E-AC-3/DD+ Atmos";
    case 0x0000000Cu:return "TrueHD/MLP";
    case 0x00000008u:return "DTS";
    case 0x0000000Bu:return "DTS-HD";
    case 0x0000010Bu:return "DTS:X(E1)";
    case 0x0000030Bu:return "DTS:X(E2)";
    default:return nullptr;
    }
};
for(;;){AM_MEDIA_TYPE* type=nullptr;if(types->Next(1,&type,nullptr)!=S_OK||!type)break;
AudioType owned(type,releaseType);sink::WavePcmFormat pcm;bool supported=false;
if(type->formattype==FORMAT_WaveFormatEx&&type->pbFormat)supported=sink::parseWavePcm(type->pbFormat,type->cbFormat,pcm);
if(!supported){
    const char* name=bitstreamName(type->subtype);
if(name){
    ++bitstreamTypeCount;if(!bitstreamSummary.empty())bitstreamSummary+=",";bitstreamSummary+=name;
    log::info("capture-audio-bitstream",std::string("mediaType=")+std::to_string(typeIndex)+" subtype=0x"+std::format("{:08X}",unsigned(type->subtype.Data1))+" kind="+name+" (passthrough candidate)");
}
}
        if(type->formattype==FORMAT_WaveFormatEx&&type->pbFormat&&type->cbFormat>=sizeof(WAVEFORMATEX)){
            const auto* wave=reinterpret_cast<const WAVEFORMATEX*>(type->pbFormat);
            log::info("capture-audio",std::format("mediaType={} major=0x{:08X} subtype=0x{:08X} tag={} channels={} mask=0x{:X} rate={} containerBits={} validBits={} floating={} pcm={}",typeIndex++,type->majortype.Data1,type->subtype.Data1,wave->wFormatTag,wave->nChannels,supported?pcm.layout.mask:0,wave->nSamplesPerSec,wave->wBitsPerSample,supported?pcm.validBits:0,supported&&pcm.floating?1:0,supported?1:0));
        }else log::info("capture-audio",std::format("mediaType={} major=0x{:08X} subtype=0x{:08X} format=0x{:08X} pcm=0",typeIndex++,type->majortype.Data1,type->subtype.Data1,type->formattype.Data1));
        if(supported)audioTypes.push_back(std::move(owned));
        else if(bitstreamName(type->subtype))bitstreamTypes.push_back(std::move(owned));
    }
log::info("capture-audio-bitstream",std::format("device bitstream types={} [{}] pcmTypes={}",bitstreamTypeCount,bitstreamSummary.empty()?"none":bitstreamSummary,audioTypes.size()));
const bool wantBitstreamFirst=p.audioIngressMode==2;
const bool allowBitstream=p.audioIngressMode!=1;
if(p.audioIngressMode==1&&bitstreamTypeCount>0)log::info("capture-audio-ingress","manual PCM-only selected: bitstream types are ignored even though the device offers them");
if(audioTypes.empty()&&(!allowBitstream||bitstreamTypes.empty())){log::warn("capture-audio","audio pin has no usable media type for the selected ingress mode");return false;}
    std::stable_sort(audioTypes.begin(),audioTypes.end(),[](const auto& a,const auto& b){
        sink::WavePcmFormat lhs{},rhs{};
        if(!sink::parseWavePcm(a->pbFormat,a->cbFormat,lhs)||!sink::parseWavePcm(b->pbFormat,b->cbFormat,rhs))return false;
        return sink::preferCaptureAudioFormat(lhs,rhs);
    });
    bool connectedAudio=false;
    // PCM attempt as a lambda so the bitstream-preferred mode can try it after
    // the compressed types instead of before them.
    auto connectPcm=[&]()->bool{
    for(const auto& owned:audioTypes){
        auto* type=owned.get();
        auto session=std::make_unique<sink::CaptureAudioSession>();ComPtr<IBaseFilter> candidate;ComPtr<IPin> terminal;
        sink::WavePcmFormat parsed{};
        if(type->formattype==FORMAT_WaveFormatEx&&type->pbFormat&&type->cbFormat>=sizeof(WAVEFORMATEX)&&sink::parseWavePcm(type->pbFormat,type->cbFormat,parsed)&&session->configure(parsed)){
            auto* target=session.get();
            hr=createNativeAudioSink(*type,[target](IMediaSample* sample){
                BYTE* bytes=nullptr;REFERENCE_TIME begin=0,end=0;
                if(FAILED(sample->GetPointer(&bytes))||FAILED(sample->GetTime(&begin,&end)))return VFW_E_SAMPLE_TIME_NOT_SET;
                return target->push(bytes,size_t(sample->GetActualDataLength()),double(begin)/10000,sample->IsDiscontinuity()==S_OK)?S_OK:E_FAIL;
            },candidate,terminal);
            if(SUCCEEDED(hr))hr=p.graph->AddFilter(candidate.Get(),L"Veyra audio PCM");
            // Request small input blocks before connection; downstream
            // playback cannot undo time spent filling a driver buffer.
            // Some devices reject this advisory API, so do not fail capture.
            if(SUCCEEDED(hr))suggestCaptureAudioBuffering(audioPin.Get(),*reinterpret_cast<const WAVEFORMATEX*>(type->pbFormat));
            if(SUCCEEDED(hr))hr=p.graph->ConnectDirect(audioPin.Get(),terminal.Get(),type);
            if(SUCCEEDED(hr)){p.audioSink=candidate;p.audioSession=std::move(session);connectedAudio=true;log::info("capture-audio",std::format("selected media type channels={} mask=0x{:X} rate={} containerBits={} validBits={} floating={}",parsed.layout.channels,parsed.layout.mask,parsed.wave.nSamplesPerSec,parsed.wave.wBitsPerSample,parsed.validBits,parsed.floating?1:0));}
            else if(candidate)p.graph->RemoveFilter(candidate.Get());
            log::info("capture-audio",std::format("PCM ConnectDirect hr=0x{:X}",unsigned(hr)));
        }
        if(connectedAudio)return true;
    }
    return false;
    };
    if(!wantBitstreamFirst)connectedAudio=connectPcm();
    // Dolby/DTS passthrough fallback: no PCM type connected, so take the best
    // compressed type the device offers and decode it back to PCM. This is the
    // path a PS5 feeding Dolby Atmos / Dolby Audio / DTS needs.
    if(!connectedAudio&&allowBitstream&&!bitstreamTypes.empty()){
        std::stable_sort(bitstreamTypes.begin(),bitstreamTypes.end(),[](const auto& a,const auto& b){
            return sink::bitstreamPreferenceOrder(sink::classifyBitstreamSubtype(a->subtype.Data1))>
                   sink::bitstreamPreferenceOrder(sink::classifyBitstreamSubtype(b->subtype.Data1));
        });
        for(const auto& owned:bitstreamTypes){
            auto* type=owned.get();
            const auto kind=sink::classifyBitstreamSubtype(type->subtype.Data1);
            const bool iec=sink::bitstreamIsIec61937(type->subtype.Data1);
            // Bitstream-first mode with an IEC 61937 framed input: forward the
            // compressed stream unchanged to a receiver that accepts the
            // matching carrier (exclusive WASAPI). Only the receiver can
            // decode Dolby Atmos / DTS:X object audio; the in-player decode
            // below would reduce it to plain 5.1 PCM. Falls back silently when
            // no endpoint advertises the format.
            if(wantBitstreamFirst&&iec){
                const GUID* carrier=nullptr;
                switch(kind){
                case sink::BitstreamKind::Ac3:carrier=&KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_DIGITAL;break;
                case sink::BitstreamKind::Eac3:carrier=&KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_DIGITAL_PLUS;break;
                case sink::BitstreamKind::TrueHd:carrier=&KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_MLP;break;
                case sink::BitstreamKind::Dts:carrier=&KSDATAFORMAT_SUBTYPE_IEC61937_DTS;break;
                // DTS-HD has no standard IEC 61937 subtype on Windows; it
                // stays on the decode path.
                default:break;
                }
                uint32_t rate=48000;
                if(type->pbFormat!=nullptr&&type->cbFormat>=sizeof(WAVEFORMATEX)){
                    const auto* wave=reinterpret_cast<const WAVEFORMATEX*>(type->pbFormat);
                    if(wave->nSamplesPerSec==44100||wave->nSamplesPerSec==48000||wave->nSamplesPerSec==96000||wave->nSamplesPerSec==192000)rate=wave->nSamplesPerSec;
                }
                if(carrier!=nullptr){
                    auto passthrough=std::make_shared<sink::BitstreamAudioSink>();
                    if(passthrough->open(*carrier,rate)){
                        ComPtr<IBaseFilter> candidate;ComPtr<IPin> terminal;
                        auto sink=passthrough;
                        hr=createNativeAudioSink(*type,[sink](IMediaSample* sample){
                            BYTE* bytes=nullptr;
                            if(FAILED(sample->GetPointer(&bytes)))return VFW_E_SAMPLE_TIME_NOT_SET;
                            return sink->write(bytes,size_t(sample->GetActualDataLength()))?S_OK:E_FAIL;
                        },candidate,terminal);
                        if(SUCCEEDED(hr))hr=p.graph->AddFilter(candidate.Get(),L"Veyra audio bitstream passthrough");
                        if(SUCCEEDED(hr))hr=p.graph->ConnectDirect(audioPin.Get(),terminal.Get(),type);
                        if(SUCCEEDED(hr)){
                            p.audioSink=candidate;p.audioPassthrough=passthrough;
                            {const std::string name=sink::bitstreamKindName(kind);p.audioBitstreamKind.assign(name.begin(),name.end());}
                            const auto& sinkState=passthrough->state();
                            log::info("capture-audio-bitstream",std::string("passthrough to receiver kind=")+sink::bitstreamKindName(kind)+
                                std::format(" rate={} endpoint=\"{}\" (no in-player decode)",rate,
                                    std::string(sinkState.endpointName.begin(),sinkState.endpointName.end())));
                            connectedAudio=true;
                        }else if(candidate)p.graph->RemoveFilter(candidate.Get());
                    }else{
                        log::info("capture-audio-bitstream",std::string("receiver passthrough unavailable for ")+sink::bitstreamKindName(kind)+"; decoding to PCM");
                    }
                    if(connectedAudio)break;
                }
            }
            auto decoder=std::make_shared<sink::BitstreamDecoder>();
            if(!decoder->open(kind,iec)){
                log::warn("capture-audio-bitstream",std::string("decoder unavailable for ")+sink::bitstreamKindName(kind)+": "+decoder->lastError());
                continue;
            }
            auto session=std::make_unique<sink::CaptureAudioSession>();
            ComPtr<IBaseFilter> candidate;ComPtr<IPin> terminal;
            auto* target=session.get();
            auto pcmBuffer=std::make_shared<std::vector<float>>();
            auto started=std::make_shared<bool>(false);
            const std::string kindName=sink::bitstreamKindName(kind);
            const std::wstring kindWide(kindName.begin(),kindName.end());
            hr=createNativeAudioSink(*type,[target,decoder,pcmBuffer,started,kindWide](IMediaSample* sample){
                BYTE* bytes=nullptr;REFERENCE_TIME begin=0,end=0;
                if(FAILED(sample->GetPointer(&bytes))||FAILED(sample->GetTime(&begin,&end)))return VFW_E_SAMPLE_TIME_NOT_SET;
                pcmBuffer->clear();
                if(!decoder->push(bytes,size_t(sample->GetActualDataLength()),*pcmBuffer))return S_OK;
                if(pcmBuffer->empty())return S_OK;
                const unsigned channels=decoder->channels();
                const unsigned rate=decoder->sampleRate();
                if(channels==0||rate==0)return S_OK;
                if(!*started){
                    WAVEFORMATEXTENSIBLE wfx{};
                    wfx.Format.wFormatTag=WAVE_FORMAT_EXTENSIBLE;
                    wfx.Format.nChannels=WORD(channels);
                    wfx.Format.nSamplesPerSec=rate;
                    wfx.Format.wBitsPerSample=32;
                    wfx.Format.nBlockAlign=WORD(channels*4);
                    wfx.Format.nAvgBytesPerSec=rate*channels*4;
                    wfx.Format.cbSize=sizeof(WAVEFORMATEXTENSIBLE)-sizeof(WAVEFORMATEX);
                    wfx.Samples.wValidBitsPerSample=32;
                    // Standard layouts: 6 = 5.1, 8 = 7.1, otherwise 5.1 as the safe default.
                    wfx.dwChannelMask=channels==8?0x63Fu:channels==6?0x3Fu:channels==2?0x3u:0x3Fu;
                    wfx.SubFormat=KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
                    if(!target->configure(wfx.Format,sizeof(wfx))||!target->start())return E_FAIL;
                    target->setInputBitstream(kindWide);
                    *started=true;
                }
                return target->push(pcmBuffer->data(),pcmBuffer->size()*sizeof(float),double(begin)/10000,sample->IsDiscontinuity()==S_OK)?S_OK:E_FAIL;
            },candidate,terminal);
            if(SUCCEEDED(hr))hr=p.graph->AddFilter(candidate.Get(),L"Veyra audio bitstream");
            if(SUCCEEDED(hr))hr=p.graph->ConnectDirect(audioPin.Get(),terminal.Get(),type);
            if(SUCCEEDED(hr)){
                p.audioSink=candidate;p.audioSession=std::move(session);p.audioBitstream=decoder;
                p.audioSessionDeferred=true;
                {const std::string name=sink::bitstreamKindName(kind);p.audioBitstreamKind.assign(name.begin(),name.end());}
                connectedAudio=true;
                log::info("capture-audio-bitstream",std::string("passthrough selected kind=")+sink::bitstreamKindName(kind)+(iec?" (IEC 61937)":"")+" -> decoded to PCM on the first frame");
            }else if(candidate)p.graph->RemoveFilter(candidate.Get());
            log::info("capture-audio-bitstream",std::string("ConnectDirect hr=0x")+std::format("{:X}",unsigned(hr)));
            if(connectedAudio)break;
        }
    }
    if(!connectedAudio&&wantBitstreamFirst)connectedAudio=connectPcm();
    return connectedAudio;
}
bool CaptureCardSource::start(){
    auto& p=*p_;if(p.info.opened)return true;if(!p.configured||!p.control)return false;
    if(p.audioSession&&p.audioSessionDeferred)log::info("capture-audio-bitstream","audio session starts with the first decoded bitstream frame");
    else if(p.audioSession&&!p.audioSession->start())log::warn("capture-audio","audio start failed; retaining video capture");
    p.audioRecovery.reset(GetTickCount64());
    p.lastFrame=Impl::Clock::now();const auto hr=p.control->Run();p.info.opened=SUCCEEDED(hr);
    if(p.info.opened&&p.wasapi&&!p.wasapi->start())p.audioError=L"WASAPI 音频启动失败；视频继续运行";
    veyra::log::info("capture",std::format("Run hr=0x{:X} actual={}x{} nominalFps={:.3f} mailbox=1 ownedBuffers=2",unsigned(hr),p.info.width,p.info.height,p.info.averageFps));return p.info.opened;
}
void CaptureCardSource::recoverAudio(float gain,unsigned syncMode,int offsetMs){
    auto& p=*p_;if(!p.info.opened||!p.control||p.wasapi)return;
    CaptureSelection selection;if(!parseCapturePath(reconnectDesc_.path,selection)||!selection.stable||selection.audio==kCaptureAudioDisabled||selection.audio==kCaptureAudioWasapi)return;
    const auto state=p.audioSession?p.audioSession->snapshot():sink::CaptureAudioState{};
    if(!p.audioRecovery.due(state.inputBlocks,GetTickCount64()))return;
    // DirectShow audio/video pins share one graph. Briefly stop it to mutate
    // only the audio branch; do not renegotiate or replace the video device.
    const HRESULT stopped=p.control->Stop();
    log::warn("capture-audio-reconnect",std::format("PCM stalled; Stop hr=0x{:08X} videoFilterRetained=1",uint32_t(stopped)));
    if(FAILED(stopped)){p.audioError=L"音频恢复等待采集驱动停止；稍后重试";return;}
    const bool connected=[&]{
        if(p.audioSink){const HRESULT hr=p.graph->RemoveFilter(p.audioSink.Get());log::info("capture-audio-reconnect",std::format("Remove PCM sink hr=0x{:08X}",uint32_t(hr)));if(FAILED(hr))return false;p.audioSink.Reset();}
        if(p.audioSession)p.audioSession->stop();
        p.audioSession.reset();
        if(p.audioFilter){const HRESULT hr=p.graph->RemoveFilter(p.audioFilter.Get());log::info("capture-audio-reconnect",std::format("Remove audio device hr=0x{:08X}",uint32_t(hr)));if(FAILED(hr))return false;p.audioFilter.Reset();}
        if(!connectDirectShowAudio(reconnectDesc_))return false;
        p.audioSession->setGain(gain);p.audioSession->setSync(syncMode,offsetMs);
        return p.audioSession->start();
    }();
    {std::lock_guard lock(p.mutex);p.pending=false;p.forceDiscontinuity=true;}
    ++epoch_;p.lastFrame=Impl::Clock::now();
    const HRESULT resumed=p.control->Run();p.info.opened=SUCCEEDED(resumed);
    p.audioError=connected&&p.info.opened?L"":L"采集音频暂不可用，正在重试原音频设备";
    log::info("capture-audio-reconnect",std::format("connected={} Run hr=0x{:08X} epoch={} awaitingActualPCM=1",connected,uint32_t(resumed),epoch_));
}
bool CaptureCardSource::reconnect(float gain,unsigned syncMode,int offsetMs){
    CaptureSelection selection;
    if(!parseCapturePath(reconnectDesc_.path,selection)||!selection.stable||reconnectFormat_.empty()){
        log::warn("capture-reconnect","Stable device and format identity unavailable; manual selection required");return false;
    }
    const auto desc=reconnectDesc_;const auto key=reconnectFormat_;const auto expected=reconnectInfo_;
    // Stop callbacks before carrying counters into the next device session.
    // Never hold the mailbox lock while DirectShow Stop waits for a callback.
    if(p_->control){const HRESULT hr=p_->control->Stop();log::info("capture-reconnect",std::format("Stop hr=0x{:08X}",uint32_t(hr)));if(FAILED(hr))return false;}
    {std::lock_guard lock(p_->mutex);
        receivedOffset_+=p_->received;deliveredOffset_+=p_->sequence;droppedOffset_+=p_->dropped;}
    close();
    int format=-1;for(const auto& candidate:formatsByPath(selection.videoPath))if(candidate.key==key){format=candidate.index;break;}
    if(format<0)return false;
    auto reopen=desc;reopen.path=std::format(L"capture2:{}:{}:{}:{}:{}",encodePath(selection.videoPath),format,selection.audio,encodePath(selection.audioPath),selection.colorOverride);
    bool ok=configure(reopen);
    if(ok){const auto& current=p_->info;
        ok=current.width==expected.width&&current.height==expected.height&&current.color.pixelFormat==expected.color.pixelFormat&&
           current.color.transfer==expected.color.transfer&&current.color.matrix==expected.color.matrix&&current.color.primaries==expected.color.primaries&&
           current.color.range==expected.color.range&&current.color.displayReferred709==expected.color.displayReferred709&&current.color.preserveSdrCodeValues==expected.color.preserveSdrCodeValues;
        if(!ok)log::warn("capture-reconnect","Negotiated input contract changed; explicit reselection required");
    }
    if(ok){setAudioGain(gain);setAudioSync(syncMode,offsetMs);ok=start();}
    if(ok){++epoch_;log::info("capture-reconnect",std::format("reconnected epoch={} formatIndex={} history reset required",epoch_,format));return true;}
    close();reconnectDesc_=desc;reconnectFormat_=key;reconnectInfo_=expected;return false;
}
CaptureMetrics CaptureCardSource::metrics()const{
    auto& p=*p_;std::lock_guard lock(p.mutex);CaptureMetrics m;
    m.received=receivedOffset_+p.received;m.delivered=deliveredOffset_+p.sequence;m.dropped=droppedOffset_+p.dropped;m.readAgeMs=p.readAgeMs;
    if(p.received>1){const double elapsed=std::chrono::duration<double>(p.latestArrival-p.firstArrival).count();if(elapsed>0)m.callbackFps=(p.received-1)/elapsed;}
    if(p.sequence)m.frameAgeMs=std::chrono::duration<double,std::milli>(Impl::Clock::now()-p.readArrival).count();return m;
}
SourceReadStatus CaptureCardSource::read(pipeline::FramePacket& packet,const AVFrame** frame){return readWithWait(packet,frame,30);}
SourceReadStatus CaptureCardSource::tryRead(pipeline::FramePacket& packet,const AVFrame** frame){return readWithWait(packet,frame,0);}
SourceReadStatus CaptureCardSource::readWithWait(pipeline::FramePacket& packet,const AVFrame** frame,unsigned milliseconds){auto& p=*p_;*frame=nullptr;if(!p.info.opened)return SourceReadStatus::Error;
    long code=0;LONG_PTR a=0,b=0;while(p.events&&p.events->GetEvent(&code,&a,&b,0)==S_OK){if(code==EC_DEVICE_LOST||code==EC_ERRORABORT)log::warn("capture-reconnect",std::format("DirectShow event={} detail=0x{:X}",code,uint64_t(a)));p.events->FreeEventParams(code,a,b);if(code==EC_DEVICE_LOST||code==EC_ERRORABORT)return SourceReadStatus::Error;}
    double time=0;uint32_t flags=0;uint64_t sequence=0;pipeline::Rational duration;
    {
        std::unique_lock lock(p.mutex);if(milliseconds)p.wake.wait_for(lock,std::chrono::milliseconds(milliseconds),[&]{return p.pending||p.callbackError;});
        if(p.callbackError)return SourceReadStatus::Error;
        if(!p.pending)return Impl::Clock::now()-p.lastFrame>std::chrono::seconds(3)?SourceReadStatus::Error:SourceReadStatus::Waiting;
        std::swap(p.frame,p.pendingFrame);p.pending=false;time=p.pendingTime;p.readArrival=p.pendingArrival;
        duration=p.pendingDuration;
        p.readAgeMs=std::chrono::duration<double,std::milli>(Impl::Clock::now()-p.readArrival).count();
        if(!p.sequence)flags|=static_cast<uint32_t>(pipeline::FrameFlagBits::Open);
        if(p.dropped!=p.lastDrop)flags|=static_cast<uint32_t>(pipeline::FrameFlagBits::Drop);
        if(p.pendingDiscontinuity)flags|=static_cast<uint32_t>(pipeline::FrameFlagBits::Discontinuity);
        if(p.forceDiscontinuity){flags|=static_cast<uint32_t>(pipeline::FrameFlagBits::Discontinuity);p.forceDiscontinuity=false;}
        p.lastDrop=p.dropped;p.lastPts=time;++p.sequence;sequence=p.received;
    }
    p.frame->pts=static_cast<int64_t>(time*10000000);p.frame->duration=duration.isUnknown()?0:duration.to100ns();p.frame->time_base={1,10000000};packet={};packet.pts={p.frame->pts,10000000};packet.duration=duration;packet.colorInfo=p.info.color;packet.sourceKind=pipeline::SourceKind::CaptureCard;packet.sequence=sequence;packet.flags=flags;packet.sourceEpoch=1;
    packet.sequence+=receivedOffset_;packet.sourceEpoch=epoch_;
    packet.arrivalHost100ns=std::chrono::duration_cast<std::chrono::nanoseconds>(p.readArrival.time_since_epoch()).count()/100;
    *frame=p.frame;p.lastFrame=Impl::Clock::now();return SourceReadStatus::Frame;
}
void CaptureCardSource::close()noexcept{
    auto& p=*p_;if(p.control){const HRESULT hr=p.control->Stop();if(FAILED(hr))log::error("capture-close",std::format("Stop failed hr=0x{:08X}; releasing graph",uint32_t(hr)));}if(p.grab){const HRESULT hr=p.grab->SetCallback(nullptr,0);if(FAILED(hr))log::error("capture-close",std::format("detach callback hr=0x{:08X}",uint32_t(hr)));}
    if(p.wasapi)p.wasapi->stop();p.wasapi.reset();
    if(p.audioSession)p.audioSession->stop();p.audioError.clear();
    if(p.audioPassthrough)p.audioPassthrough->close();
    p.events.Reset();p.control.Reset();p.grab.Reset();p.nullFilter.Reset();p.grabFilter.Reset();p.audioSink.Reset();p.audioFilter.Reset();p.config.Reset();p.device.Reset();p.builder.Reset();p.graph.Reset();p.referenceClock.Reset();p.audioSession.reset();p.audioPassthrough.reset();
    av_frame_free(&p.frame);av_frame_free(&p.pendingFrame);p.info={};
    p.sequence=p.received=p.dropped=p.lastDrop=0;p.pending=p.callbackError=p.configured=p.forceDiscontinuity=false;p.lastPts=p.readAgeMs=0;
}
}
