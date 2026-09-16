#include "veyra/sink/BitstreamAudioSink.h"

#include "veyra/Log.h"

#include <initguid.h>
#include <ks.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <thread>

namespace veyra::sink {
namespace {

std::wstring friendlyName(IMMDevice* device) {
    IPropertyStore* store = nullptr;
    std::wstring name = L"(unnamed)";
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) && store != nullptr) {
        PROPVARIANT value{};
        if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &value)) &&
            value.vt == VT_LPWSTR) {
            name = value.pwszVal;
        }
        PropVariantClear(&value);
        store->Release();
    }
    return name;
}

WAVEFORMATEXTENSIBLE carrierFormat(const GUID& subtype, uint32_t sampleRate) {
    WAVEFORMATEXTENSIBLE format{};
    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.nChannels = 2;
    format.Format.nSamplesPerSec = sampleRate;
    format.Format.wBitsPerSample = 16;
    format.Format.nBlockAlign = 4;
    format.Format.nAvgBytesPerSec = sampleRate * 4;
    format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    format.Samples.wValidBitsPerSample = 16;
    format.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    format.SubFormat = subtype;
    return format;
}

} // namespace

struct BitstreamAudioSink::Impl {
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioRenderClient* render = nullptr;
    uint32_t bufferFrames = 0;
    uint32_t frameBytes = 4;  // 16-bit stereo carrier
    uint8_t tail[8]{};
    size_t tailBytes = 0;
    bool comInitialized = false;
    BitstreamAudioSinkState state{};

    ~Impl() { shutdown(); }

    void shutdown() {
        if (client != nullptr) {
            client->Stop();
        }
        if (render != nullptr) {
            render->Release();
            render = nullptr;
        }
        if (client != nullptr) {
            client->Release();
            client = nullptr;
        }
        if (device != nullptr) {
            device->Release();
            device = nullptr;
        }
        if (enumerator != nullptr) {
            enumerator->Release();
            enumerator = nullptr;
        }
        state.open = false;
    }

    bool tryEndpoint(IMMDevice* candidate, const WAVEFORMATEXTENSIBLE& format) {
        IAudioClient* audio = nullptr;
        if (FAILED(candidate->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                       reinterpret_cast<void**>(&audio))) ||
            audio == nullptr) {
            return false;
        }
        WAVEFORMATEX* closest = nullptr;
        const HRESULT support = audio->IsFormatSupported(
            AUDCLNT_SHAREMODE_EXCLUSIVE,
            reinterpret_cast<const WAVEFORMATEX*>(&format), &closest);
        if (closest != nullptr) CoTaskMemFree(closest);
        if (support != S_OK) {
            audio->Release();
            return false;
        }
        // 500 ms of carrier keeps the exclusive stream tolerant of small
        // scheduling jitter without adding meaningful latency.
        const HRESULT init = audio->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, 0,
                                               5000000, 0,
                                               reinterpret_cast<const WAVEFORMATEX*>(&format),
                                               nullptr);
        if (FAILED(init)) {
            audio->Release();
            return false;
        }
        IAudioRenderClient* renderClient = nullptr;
        if (FAILED(audio->GetService(__uuidof(IAudioRenderClient),
                                     reinterpret_cast<void**>(&renderClient))) ||
            renderClient == nullptr) {
            audio->Release();
            return false;
        }
        UINT32 frames = 0;
        audio->GetBufferSize(&frames);
        if (FAILED(audio->Start()) || frames == 0) {
            renderClient->Release();
            audio->Release();
            return false;
        }
        client = audio;
        render = renderClient;
        device = candidate;
        device->AddRef();
        bufferFrames = frames;
        return true;
    }
};

BitstreamAudioSink::BitstreamAudioSink() : p_(std::make_unique<Impl>()) {}
BitstreamAudioSink::~BitstreamAudioSink() { close(); }

bool BitstreamAudioSink::open(const GUID& subtype, uint32_t sampleRate,
                              const std::wstring& preferredEndpointId) {
    auto& p = *p_;
    if (p.state.open) return true;
    if (sampleRate == 0) sampleRate = 48000;
    const WAVEFORMATEXTENSIBLE format = carrierFormat(subtype, sampleRate);

    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(comHr)) {
        p.comInitialized = true;
    } else if (comHr != RPC_E_CHANGED_MODE) {
        p.state.error = L"COM initialization failed";
        return false;
    }
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(&p.enumerator));
    if (FAILED(hr) || p.enumerator == nullptr) {
        p.state.error = L"audio device enumerator unavailable";
        return false;
    }
    IMMDeviceCollection* collection = nullptr;
    hr = p.enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr) || collection == nullptr) {
        p.state.error = L"render endpoint enumeration failed";
        return false;
    }
    UINT count = 0;
    collection->GetCount(&count);

    // Preferred endpoint first (usually the one the PCM path uses), then any
    // other endpoint that advertises the carrier format exactly.
    bool opened = false;
    if (!preferredEndpointId.empty()) {
        for (UINT index = 0; index < count && !opened; ++index) {
            IMMDevice* candidate = nullptr;
            if (FAILED(collection->Item(index, &candidate)) || candidate == nullptr) continue;
            LPWSTR id = nullptr;
            bool matches = SUCCEEDED(candidate->GetId(&id)) && id != nullptr &&
                           preferredEndpointId == id;
            if (id != nullptr) CoTaskMemFree(id);
            if (matches) {
                opened = p.tryEndpoint(candidate, format);
                if (opened) {
                    p.state.endpointName = friendlyName(candidate);
                }
            }
            if (!opened) candidate->Release();
        }
    }
    for (UINT index = 0; index < count && !opened; ++index) {
        IMMDevice* candidate = nullptr;
        if (FAILED(collection->Item(index, &candidate)) || candidate == nullptr) continue;
        opened = p.tryEndpoint(candidate, format);
        if (opened) {
            p.state.endpointName = friendlyName(candidate);
        } else {
            candidate->Release();
        }
    }
    if (opened) {
        LPWSTR id = nullptr;
        if (SUCCEEDED(p.device->GetId(&id)) && id != nullptr) {
            p.state.endpointId = id;
            CoTaskMemFree(id);
        }
    }
    collection->Release();
    if (!opened) {
        p.state.error = L"no active render endpoint accepts the IEC 61937 carrier";
        log::warn("bitstream-out", "no render endpoint accepted the compressed carrier format");
        return false;
    }
    p.state.open = true;
    p.state.sampleRate = sampleRate;
    log::info("bitstream-out", std::format(
        "passthrough stream open endpoint=\"{}\" rate={}Hz 16-bit stereo carrier",
        std::string(p.state.endpointName.begin(), p.state.endpointName.end()), sampleRate));
    return true;
}

bool BitstreamAudioSink::write(const uint8_t* data, size_t bytes) {
    auto& p = *p_;
    if (!p.state.open || p.client == nullptr || p.render == nullptr) return false;
    if (data == nullptr || bytes == 0) return true;

    // Keep frame alignment: hold back a trailing partial carrier frame.
    size_t consumed = 0;
    if (p.tailBytes != 0) {
        const size_t need = p.frameBytes - p.tailBytes;
        const size_t take = std::min(need, bytes);
        std::memcpy(p.tail + p.tailBytes, data, take);
        p.tailBytes += take;
        consumed += take;
        if (p.tailBytes < p.frameBytes) return true;
        // Fall through: the completed tail frame is written below.
    }
    const uint8_t* cursor = data + consumed;
    size_t remaining = bytes - consumed;
    auto writeBlock = [&](const uint8_t* block, size_t blockBytes) -> bool {
        size_t offset = 0;
        while (offset < blockBytes) {
            const uint32_t frames = static_cast<uint32_t>((blockBytes - offset) / p.frameBytes);
            if (frames == 0) return true;
            UINT32 padding = 0;
            if (FAILED(p.client->GetCurrentPadding(&padding))) return false;
            const uint32_t available = padding < p.bufferFrames ? p.bufferFrames - padding : 0;
            if (available == 0) {
                ++p.state.underruns;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            const uint32_t chunk = std::min(frames, available);
            BYTE* destination = nullptr;
            if (FAILED(p.render->GetBuffer(chunk, &destination)) || destination == nullptr) {
                return false;
            }
            std::memcpy(destination, block + offset, size_t(chunk) * p.frameBytes);
            p.render->ReleaseBuffer(chunk, 0);
            offset += size_t(chunk) * p.frameBytes;
            p.state.framesWritten += chunk;
            p.state.bytesWritten += size_t(chunk) * p.frameBytes;
        }
        return true;
    };
    if (p.tailBytes == p.frameBytes) {
        if (!writeBlock(p.tail, p.frameBytes)) return false;
        p.tailBytes = 0;
    }
    const size_t whole = remaining - (remaining % p.frameBytes);
    if (whole != 0 && !writeBlock(cursor, whole)) return false;
    if (remaining > whole) {
        std::memcpy(p.tail, cursor + whole, remaining - whole);
        p.tailBytes = remaining - whole;
    }
    return true;
}

void BitstreamAudioSink::close() {
    auto& p = *p_;
    if (p.state.open) {
        log::info("bitstream-out", std::format(
            "passthrough stream closed endpoint=\"{}\" bytes={} frames={} stalls={}",
            std::string(p.state.endpointName.begin(), p.state.endpointName.end()),
            p.state.bytesWritten, p.state.framesWritten, p.state.underruns));
    }
    p.shutdown();
    if (p.comInitialized) {
        CoUninitialize();
        p.comInitialized = false;
    }
}

bool BitstreamAudioSink::opened() const { return p_->state.open; }

const BitstreamAudioSinkState& BitstreamAudioSink::state() const { return p_->state; }

} // namespace veyra::sink
