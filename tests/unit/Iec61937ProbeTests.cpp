// Unit checks for the read-only IEC 61937 carrier probe.
//
// The probe exists because a capture card can deliver a compressed stream
// inside a media type that still claims linear PCM, while the same card also
// downmixes to real 2.0 when passthrough is not armed. These checks pin down
// the two properties that make the diagnostic trustworthy: it confirms only on
// repeated, consistent bursts, and it stays bounded and silent otherwise.

#include "veyra/sink/BitstreamAudio.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) {
        std::printf("ok   %s\n", what);
    } else {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

// One IEC 61937 burst: 4-byte sync, 16-bit data type, 16-bit payload length in
// bits, then the payload. Payload bytes are arbitrary here: the probe only
// validates the header, and a real decoder never sees these fixtures.
void appendBurst(std::vector<uint8_t>& out, uint16_t dataType, size_t payloadBytes) {
    out.push_back(0xF8); out.push_back(0x72); out.push_back(0x4E); out.push_back(0x1F);
    out.push_back(uint8_t(dataType & 0xFF));
    out.push_back(uint8_t(dataType >> 8));
    const uint16_t bits = uint16_t(payloadBytes * 8);
    out.push_back(uint8_t(bits & 0xFF));
    out.push_back(uint8_t(bits >> 8));
    out.insert(out.end(), payloadBytes, 0x5A);
}

// Deterministic PCM-ish noise: no accidental sync word, exercises the
// "never conclude early" path without depending on a random seed.
void appendPcmNoise(std::vector<uint8_t>& out, size_t bytes) {
    uint32_t state = 0x12345678u;
    for (size_t i = 0; i < bytes; ++i) {
        state = state * 1664525u + 1013904223u;
        uint8_t value = uint8_t(state >> 24);
        if (value == 0xF8) value = 0xF7;  // keep the fixture free of sync bytes
        out.push_back(value);
    }
}

} // namespace

int wmain() {
    using veyra::sink::BitstreamKind;
    using veyra::sink::Iec61937Probe;

    check(veyra::sink::classifyIec61937DataType(0x01) == BitstreamKind::Ac3, "data type 0x01 maps to AC-3");
    check(veyra::sink::classifyIec61937DataType(0x15) == BitstreamKind::Eac3, "data type 0x15 maps to E-AC-3");
    check(veyra::sink::classifyIec61937DataType(0x0B) == BitstreamKind::Dts, "data type 0x0B maps to DTS");
    check(veyra::sink::classifyIec61937DataType(0x0C) == BitstreamKind::Dts, "data type 0x0C maps to DTS");
    check(veyra::sink::classifyIec61937DataType(0x0D) == BitstreamKind::Dts, "data type 0x0D maps to DTS");
    check(veyra::sink::classifyIec61937DataType(0x11) == BitstreamKind::DtsHd, "data type 0x11 maps to DTS-HD");
    check(veyra::sink::classifyIec61937DataType(0x16) == BitstreamKind::TrueHd, "data type 0x16 maps to TrueHD");
    check(veyra::sink::classifyIec61937DataType(0x42) == BitstreamKind::None, "an unknown data type is rejected");
    check(veyra::sink::classifyIec61937DataType(0x00) == BitstreamKind::None, "data type 0 is rejected");

    {   // Two consistent bursts in one buffer: confirmed.
        std::vector<uint8_t> carrier;
        appendBurst(carrier, 0x15, 384);
        appendBurst(carrier, 0x15, 384);
        Iec61937Probe probe;
        probe.feed(carrier.data(), carrier.size());
        check(probe.concluded(), "two E-AC-3 bursts conclude the probe");
        check(probe.detected(), "two E-AC-3 bursts are reported as IEC 61937");
        check(probe.dataType() == 0x15, "the reported data type is E-AC-3");
        check(probe.burstCount() >= 2, "the burst count is recorded");
    }

    {   // Bursts straddling a buffer boundary must still be found.
        std::vector<uint8_t> carrier;
        appendBurst(carrier, 0x01, 512);
        appendBurst(carrier, 0x01, 512);
        Iec61937Probe probe;
        const size_t split = 5;  // inside the first header
        probe.feed(carrier.data(), split);
        check(!probe.concluded(), "a partial header alone does not conclude the probe");
        probe.feed(carrier.data() + split, carrier.size() - split);
        check(probe.detected(), "bursts split across two feeds are still detected");
        check(probe.dataType() == 0x01, "the split case reports AC-3");
    }

    {   // A sync word followed by an unknown data type is not evidence.
        std::vector<uint8_t> carrier;
        appendBurst(carrier, 0x42, 384);
        appendBurst(carrier, 0x42, 384);
        Iec61937Probe probe;
        probe.feed(carrier.data(), carrier.size());
        check(!probe.detected(), "unknown data types are never accepted as a bitstream");
    }

    {   // One burst is not enough, and the scan must terminate on its own.
        std::vector<uint8_t> carrier;
        appendBurst(carrier, 0x01, 512);
        appendPcmNoise(carrier, Iec61937Probe::kMaxScanBytes);
        Iec61937Probe probe;
        probe.feed(carrier.data(), carrier.size());
        check(probe.concluded(), "the probe reaches a verdict within its scan budget");
        check(!probe.detected(), "a single burst followed by PCM is rejected");
        check(probe.scannedBytes() <= Iec61937Probe::kMaxScanBytes + 64,
              "scanning stays inside the documented budget");
    }

    {   // Plain PCM must stay silent (no verdict) until the budget runs out.
        std::vector<uint8_t> pcm;
        appendPcmNoise(pcm, 64 * 1024);
        Iec61937Probe probe;
        probe.feed(pcm.data(), pcm.size());
        check(!probe.concluded(), "64 KB of PCM does not conclude the probe");
        check(!probe.detected(), "64 KB of PCM is not reported as a bitstream");
    }

    std::printf("%s (%d failure%s)\n", failures == 0 ? "PASS" : "FAIL", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
