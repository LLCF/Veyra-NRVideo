// Unit checks for the AVerMedia multichannel-audio switch wrapper.
//
// The component it drives is optional: it only exists on machines where the
// user installed AVerMedia's OBS plugin. These checks therefore have to hold on
// both kinds of machine - with and without the component - and they must hold
// without any capture hardware attached.

#include "veyra/source/AverMediaAudioSwitch.h"

#include <windows.h>

#include <cstdio>
#include <string>

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

} // namespace

int wmain() {
    using veyra::source::AverMediaAudioSwitch;

    // Device-path classification. AVerMedia's VID is 0x07CA; the GC553G2 is
    // PID 0x2553, which is what the shipped component also matches on.
    check(AverMediaAudioSwitch::isAverMediaDevicePath(
              L"\\\\?\\usb#vid_07ca&pid_2553#a0000000000#{65e8773d-8f56-11d0-a3b9-00a0c9223196}\\global"),
          "vid_07ca path is recognised");
    check(AverMediaAudioSwitch::isAverMediaDevicePath(
              L"\\\\?\\USB#VID_07CA&PID_2553#serial#{guid}"),
          "uppercase VID_07CA is recognised (case-insensitive)");
    check(!AverMediaAudioSwitch::isAverMediaDevicePath(
              L"\\\\?\\usb#vid_046d&pid_0825#6&2f0a1c4&0&1#{65e8773d-8f56-11d0-a3b9-00a0c9223196}\\global"),
          "a non-AVerMedia device is rejected");
    check(!AverMediaAudioSwitch::isAverMediaDevicePath(L""), "an empty path is rejected");
    check(!AverMediaAudioSwitch::isAverMediaDevicePath(L"{0.0.1.00000000}.{1234}"),
          "a WASAPI endpoint id is rejected");

    // Component discovery must either find a usable folder or report nothing at
    // all; a half-matched folder would hand garbage to LoadLibrary later.
    const std::wstring root = AverMediaAudioSwitch::locateComponentRoot();
    if (root.empty()) {
        std::printf("note component not installed on this machine (expected on a bare test host)\n");
        check(true, "locateComponentRoot returns empty when absent");
    } else {
        const std::wstring dll = root + L"\\obs-plugins\\64bit\\avt_device_opener.dll";
        check(GetFileAttributesW(dll.c_str()) != INVALID_FILE_ATTRIBUTES,
              "located component provides avt_device_opener.dll");
        std::wprintf(L"note component root: %ls\n", root.c_str());
    }

    // Without the component, apply() must fail soft: no crash, no throw, a
    // state a caller can log, and a usable object afterwards.
    AverMediaAudioSwitch first;
    const bool applied = first.apply(L"USB3 Digital Audio",
                                     L"\\\\?\\usb#vid_07ca&pid_2553#serial#{65e8773d-8f56-11d0-a3b9-00a0c9223196}\\global");
    const auto& status = first.status();
    if (root.empty()) {
        check(!applied, "apply() reports failure when the component is absent");
        check(!status.componentFound, "status marks the component as absent");
        check(!status.dllsLoaded, "status marks the DLLs as not loaded");
        check(!status.detail.empty(), "status carries a reason for the log");
        check(status.state == veyra::source::AverMediaSwitchState::Unavailable,
              "status state is Unavailable");
    } else {
        // With a component present the call is allowed to succeed or fail (no
        // card is attached here), but it must never fault the process.
        std::printf("note apply() with component present returned %d: %s\n", applied ? 1 : 0, status.detail.c_str());
        check(true, "apply() completed without faulting");
    }

    // Teardown must be idempotent: close() calls stop() and then the destructor
    // runs it again.
    first.stop();
    first.stop();
    check(!first.active(), "stop() leaves the helper inactive");
    {
        AverMediaAudioSwitch second;
        second.stop();
    }
    check(true, "destructor after stop() is safe");

    std::printf("%s (%d failure%s)\n", failures == 0 ? "PASS" : "FAIL", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
