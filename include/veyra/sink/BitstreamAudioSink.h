#pragma once

// Dolby / DTS compressed-audio passthrough output.
//
// When the capture endpoint delivers an IEC 61937 compressed stream (Dolby
// Digital, Dolby Digital Plus, DTS, ...) the player can forward it unmodified
// to a receiver instead of decoding it. The receiver then performs the real
// decode, which is the only way a Dolby Atmos (E-AC-3 + JOC) or DTS:X stream
// keeps its object metadata - a PCM decode inside the player cannot.
//
// This sink owns a WASAPI *exclusive* stream on the first active render
// endpoint that advertises the matching IEC 61937 carrier format. Exclusive
// mode is required: the shared engine only accepts PCM. Compressed frames are
// written as a byte stream; IEC 61937 is self-synchronising, so no packet
// repacking is needed.

#include <windows.h>
#include <mmreg.h>

#include <cstdint>
#include <memory>
#include <string>

namespace veyra::sink {

struct BitstreamAudioSinkState {
    bool open = false;
    uint32_t sampleRate = 0;
    std::wstring endpointName;
    std::wstring endpointId;
    uint64_t bytesWritten = 0;
    uint64_t framesWritten = 0;
    uint64_t underruns = 0;
    std::wstring error;
};

class BitstreamAudioSink {
public:
    BitstreamAudioSink();
    ~BitstreamAudioSink();
    BitstreamAudioSink(const BitstreamAudioSink&) = delete;
    BitstreamAudioSink& operator=(const BitstreamAudioSink&) = delete;

    // Opens an exclusive WASAPI stream for `subtype` (one of the
    // KSDATAFORMAT_SUBTYPE_IEC61937_* GUIDs) at `sampleRate`, choosing the
    // first active render endpoint that accepts the format exactly.
    // `preferredEndpointId` (optional) is tried first, e.g. the endpoint the
    // PCM path already uses, so a device with both PCM and passthrough
    // support keeps one routing target.
    bool open(const GUID& subtype, uint32_t sampleRate,
              const std::wstring& preferredEndpointId = {});
    // Forwards one chunk of the compressed stream. Blocks briefly while the
    // endpoint buffer is full; byte counts that do not fill a 16-bit stereo
    // frame are held back for the next call.
    bool write(const uint8_t* data, size_t bytes);
    void close();

    bool opened() const;
    const BitstreamAudioSinkState& state() const;

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

} // namespace veyra::sink
