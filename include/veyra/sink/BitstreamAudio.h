#pragma once

// Dolby / DTS bitstream support for the capture path.
//
// A capture card that is fed Dolby Atmos / Dolby Audio / DTS by the console
// exposes the audio pin as a *compressed* media type instead of PCM. Veyra used
// to enumerate those types and ignore them, so the user only ever got linear
// PCM. This module decodes such a stream back to interleaved float PCM so it can
// enter the existing 5.1 pipeline, and it also carries the (read-only)
// classification used to report device capability.
//
// Decoding uses the FFmpeg already shipped with the player (libavcodec +
// libswresample). Nothing here changes the PCM path: it is only selected when
// the device offers no usable PCM layout or when passthrough is requested.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace veyra::sink {

// Compressed audio families the capture endpoint can expose. Data1 of the
// KSDATAFORMAT/WAVE subtype GUID is what identifies them on the wire.
enum class BitstreamKind { None, Ac3, Eac3, TrueHd, Dts, DtsHd };

// Maps a media-type subtype id to a family. Returns None for anything unknown
// (never guesses).
BitstreamKind classifyBitstreamSubtype(uint32_t subtypeData1);
const char* bitstreamKindName(BitstreamKind kind);
// Negotiation preference: higher wins when several compressed formats are
// offered (TrueHD Atmos > DD+ Atmos > DD+ > DTS-HD > DTS > AC-3).
int bitstreamPreferenceOrder(BitstreamKind kind);
// True when the subtype carries IEC 61937 framed bursts (S/PDIF style) that must
// be unwrapped before the codec parser sees them.
bool bitstreamIsIec61937(uint32_t subtypeData1);

class BitstreamDecoder {
public:
    BitstreamDecoder();
    ~BitstreamDecoder();
    BitstreamDecoder(const BitstreamDecoder&) = delete;
    BitstreamDecoder& operator=(const BitstreamDecoder&) = delete;

    // Opens the decoder for `kind`. `iec61937` unwraps S/PDIF bursts first.
    bool open(BitstreamKind kind, bool iec61937 = false);
    // Feeds raw bytes from the capture endpoint; decoded interleaved float
    // samples are appended to `out`. Returns false only on a hard failure, which
    // is also stored in lastError().
    bool push(const uint8_t* data, size_t bytes, std::vector<float>& out);
    // Drops codec state (seek / discontinuity): the next bytes start a new
    // stream, which is what happens when the console switches audio format.
    void reset();

    bool opened() const;
    unsigned channels() const;
    unsigned sampleRate() const;
    uint64_t decodedFrames() const;
    uint64_t decodedBytesIn() const;
    const std::string& lastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

} // namespace veyra::sink
