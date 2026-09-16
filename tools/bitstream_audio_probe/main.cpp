// Dolby / DTS endpoint passthrough probe (diagnostic only, never shipped).
//
// Enumerates render endpoints and asks each one, in WASAPI exclusive mode,
// whether it accepts the IEC 61937 carrier formats used for compressed
// passthrough (AC-3, E-AC-3/DD+, DTS, TrueHD). A receiver behind HDMI/SPDIF
// only gets real Atmos/DTS:X when the endpoint advertises these formats; the
// player can then forward the capture bitstream without decoding.
//
// Usage: veyra_bitstream_probe [sampleRate] [--write-test]
#include <windows.h>
#include <initguid.h>
#include <ks.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::string toUtf8(const wchar_t* value) {
    if (value == nullptr) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    std::string text(size_t(length > 0 ? length - 1 : 0), '\0');
    if (length > 1) WideCharToMultiByte(CP_UTF8, 0, value, -1, text.data(), length, nullptr, nullptr);
    return text;
}

WAVEFORMATEXTENSIBLE iec61937Format(const GUID& subtype, uint32_t sampleRate) {
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

struct FormatCase {
    const char* name;
    const GUID* subtype;
};

std::string describeHr(HRESULT hr) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "0x%08X", static_cast<unsigned>(hr));
    return buffer;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    uint32_t sampleRate = 48000;
    bool writeTest = false;
    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--write-test") == 0) writeTest = true;
    }
    if (argc > 1 && _wtoi(argv[1]) > 0) {
        const int requested = _wtoi(argv[1]);
        if (requested == 44100 || requested == 48000 || requested == 96000 ||
            requested == 192000) {
            sampleRate = static_cast<uint32_t>(requested);
        }
    }
    const FormatCase cases[] = {
        {"AC-3 (Dolby Digital)", &KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_DIGITAL},
        {"E-AC-3 (Dolby Digital Plus / Atmos carrier)",
         &KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_DIGITAL_PLUS},
        {"DTS", &KSDATAFORMAT_SUBTYPE_IEC61937_DTS},
        {"Dolby TrueHD / MLP", &KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_MLP},
    };

    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(comHr)) {
        std::printf("[bitstream] CoInitializeEx failed %s\n", describeHr(comHr).c_str());
        return 2;
    }
    IMMDeviceEnumerator* enumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr)) {
        std::printf("[bitstream] device enumerator failed %s\n", describeHr(hr).c_str());
        return 3;
    }
    IMMDeviceCollection* collection = nullptr;
    hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr)) {
        std::printf("[bitstream] endpoint enumeration failed %s\n", describeHr(hr).c_str());
        enumerator->Release();
        return 3;
    }
    UINT count = 0;
    collection->GetCount(&count);
    std::printf("[bitstream] render endpoints=%u sampleRate=%u\n", count, sampleRate);
    for (UINT index = 0; index < count; ++index) {
        IMMDevice* device = nullptr;
        if (FAILED(collection->Item(index, &device)) || device == nullptr) continue;
        IPropertyStore* store = nullptr;
        std::string name = "(unnamed)";
        std::string deviceId = "(no id)";
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) && store != nullptr) {
            PROPVARIANT value{};
            if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &value)) &&
                value.vt == VT_LPWSTR) {
                name = toUtf8(value.pwszVal);
            }
            PropVariantClear(&value);
            store->Release();
        }
        {
            LPWSTR id = nullptr;
            if (SUCCEEDED(device->GetId(&id)) && id != nullptr) {
                deviceId = toUtf8(id);
                CoTaskMemFree(id);
            }
        }
        IAudioClient* client = nullptr;
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(&client));
        std::printf("\n[%u] %s\n    id=%s\n    activate=%s\n", index, name.c_str(),
                    deviceId.c_str(), describeHr(hr).c_str());
        if (SUCCEEDED(hr) && client != nullptr) {
            for (const auto& entry : cases) {
                WAVEFORMATEXTENSIBLE format = iec61937Format(*entry.subtype, sampleRate);
                WAVEFORMATEX* closest = nullptr;
                const HRESULT support = client->IsFormatSupported(
                    AUDCLNT_SHAREMODE_EXCLUSIVE,
                    reinterpret_cast<const WAVEFORMATEX*>(&format), &closest);
                if (closest != nullptr) CoTaskMemFree(closest);
                const char* verdict = "no";
                if (support == S_OK) verdict = "YES (exact)";
                else if (support == S_FALSE) verdict = "near (resampled/other rate)";
                std::printf("    %-45s %s %s\n", entry.name, verdict,
                            describeHr(support).c_str());
            }
            client->Release();
        }
        device->Release();
    }
    collection->Release();
    enumerator->Release();

    if (writeTest) {
        // Open an exclusive AC-3 carrier on the first endpoint that supports
        // it and write synthetic bursts through the same code path the
        // passthrough sink uses. The receiver, when one is attached, decides
        // whether the silent pattern is decodable; the probe only proves the
        // stream opens and accepts data.
        std::printf("\n[write-test] opening exclusive AC-3 carrier @ %u Hz\n", sampleRate);
        IMMDeviceEnumerator* enum2 = nullptr;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                       __uuidof(IMMDeviceEnumerator),
                                       reinterpret_cast<void**>(&enum2))) && enum2 != nullptr) {
            IMMDeviceCollection* render = nullptr;
            if (SUCCEEDED(enum2->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &render)) &&
                render != nullptr) {
                UINT total = 0;
                render->GetCount(&total);
                for (UINT index = 0; index < total; ++index) {
                    IMMDevice* candidate = nullptr;
                    if (FAILED(render->Item(index, &candidate)) || candidate == nullptr) continue;
                    IAudioClient* client = nullptr;
                    if (SUCCEEDED(candidate->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                                      reinterpret_cast<void**>(&client))) &&
                        client != nullptr) {
                        WAVEFORMATEXTENSIBLE format = iec61937Format(
                            KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_DIGITAL, sampleRate);
                        WAVEFORMATEX* closest = nullptr;
                        const HRESULT support = client->IsFormatSupported(
                            AUDCLNT_SHAREMODE_EXCLUSIVE,
                            reinterpret_cast<const WAVEFORMATEX*>(&format), &closest);
                        if (closest != nullptr) CoTaskMemFree(closest);
                        if (support == S_OK &&
                            SUCCEEDED(client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, 0, 5000000, 0,
                                                         reinterpret_cast<const WAVEFORMATEX*>(&format),
                                                         nullptr))) {
                            IAudioRenderClient* renderClient = nullptr;
                            UINT32 frames = 0;
                            client->GetBufferSize(&frames);
                            if (SUCCEEDED(client->GetService(__uuidof(IAudioRenderClient),
                                                             reinterpret_cast<void**>(&renderClient))) &&
                                renderClient != nullptr && frames != 0 &&
                                SUCCEEDED(client->Start())) {
                                std::vector<uint8_t> burst(6144, 0);
                                // One IEC 61937 style burst header so the stream
                                // is byte-aligned; content is silence.
                                burst[0] = 0x72; burst[1] = 0xF8; burst[2] = 0x1F; burst[3] = 0x4E;
                                uint64_t written = 0;
                                for (int repeat = 0; repeat < 40; ++repeat) {
                                    UINT32 padding = 0;
                                    while (SUCCEEDED(client->GetCurrentPadding(&padding)) &&
                                           padding >= frames) {
                                        Sleep(2);
                                    }
                                    const UINT32 available = frames - padding;
                                    const UINT32 chunkFrames = available < burst.size() / 4
                                                                   ? available
                                                                   : static_cast<UINT32>(burst.size() / 4);
                                    if (chunkFrames == 0) break;
                                    BYTE* destination = nullptr;
                                    if (FAILED(renderClient->GetBuffer(chunkFrames, &destination)) ||
                                        destination == nullptr) {
                                        break;
                                    }
                                    std::memcpy(destination, burst.data(),
                                                std::min<size_t>(burst.size(),
                                                                 size_t(chunkFrames) * 4));
                                    renderClient->ReleaseBuffer(chunkFrames, 0);
                                    written += size_t(chunkFrames) * 4;
                                    Sleep(20);
                                }
                                client->Stop();
                                std::printf("[write-test] endpoint[%u] wrote %llu bytes through the exclusive carrier\n",
                                            index, static_cast<unsigned long long>(written));
                                renderClient->Release();
                            }
                            client->Release();
                            candidate->Release();
                            break;
                        }
                        client->Release();
                    }
                    candidate->Release();
                }
                render->Release();
            }
            enum2->Release();
        }
    }
    CoUninitialize();
    std::printf("\n[bitstream] probe complete (S_OK = endpoint accepts the IEC 61937 carrier)\n");
    return 0;
}
