#pragma once
#include <windows.h>
#include <dshow.h>
#include <mmreg.h>
#include <wrl/client.h>
#include <functional>
namespace veyra::source {
// Native terminal filter: no legacy SampleGrabber orientation/VideoInfo2
// restriction, conversion filter or extra queue. Callback borrows the sample.
HRESULT createNativeCaptureSink(const AM_MEDIA_TYPE&,std::function<HRESULT(IMediaSample*)>,Microsoft::WRL::ComPtr<IBaseFilter>&,Microsoft::WRL::ComPtr<IPin>&);
HRESULT createNativeAudioSink(const AM_MEDIA_TYPE&,std::function<HRESULT(IMediaSample*)>,Microsoft::WRL::ComPtr<IBaseFilter>&,Microsoft::WRL::ComPtr<IPin>&);
// Compressed (MJPEG/H.264/HEVC/...) variant: carries the driver payload without
// a pixel-layout contract; decoding happens in our own backend.
HRESULT createCompressedCaptureSink(const AM_MEDIA_TYPE&,std::function<HRESULT(IMediaSample*)>,Microsoft::WRL::ComPtr<IBaseFilter>&,Microsoft::WRL::ComPtr<IPin>&);
// Advisory request on the upstream output pin, before ConnectDirect. Failure
// does not invalidate the device; callers retain the compatible connection.
HRESULT suggestCaptureAudioBuffering(IPin*,const WAVEFORMATEX&);
// Advisory video-pin request (N4): suggest the driver's allocator buffer count
// and per-frame byte size before ConnectDirect. Failure is logged and ignored.
HRESULT suggestCaptureVideoBuffering(IPin*,long buffers,long bytes);
// Reads the allocator actually in use on our input pin after ConnectDirect.
HRESULT queryCaptureAllocatorProperties(IPin*,ALLOCATOR_PROPERTIES&);
}
