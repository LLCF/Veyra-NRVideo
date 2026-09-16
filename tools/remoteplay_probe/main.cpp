// Headless Remote Play probe: drives the exact product session stack with a
// saved (already paired) profile so codec/bitrate variables can be compared
// without the GUI. It never prints credentials.
#include "veyra/remoteplay/ProfileStore.h"
#include "veyra/source/RemotePlaySessionSource.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/Log.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

using namespace veyra;

namespace {

std::string narrow(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), int(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size_t(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), int(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

const char* stateName(remoteplay::SessionState state) {
    switch (state) {
    case remoteplay::SessionState::Idle: return "idle";
    case remoteplay::SessionState::Connecting: return "connecting";
    case remoteplay::SessionState::WaitingFirstFrame: return "waiting-first-frame";
    case remoteplay::SessionState::Streaming: return "streaming";
    case remoteplay::SessionState::LoginPinRequired: return "login-pin-required";
    case remoteplay::SessionState::Failed: return "failed";
    case remoteplay::SessionState::Stopping: return "stopping";
    }
    return "?";
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    std::string codec = "keep";
    unsigned bitrate = 0;
    int seconds = 20;
    std::filesystem::path profile;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--codec" && i + 1 < argc) codec = narrow(argv[++i]);
        else if (arg == L"--bitrate" && i + 1 < argc) bitrate = unsigned(wcstoul(argv[++i], nullptr, 10));
        else if (arg == L"--seconds" && i + 1 < argc) seconds = _wtoi(argv[++i]);
        else if (arg == L"--profile" && i + 1 < argc) profile = argv[++i];
    }
    if (profile.empty()) {
        // The GUI saves one <32-hex-account-id>.dat per paired console; fall
        // back to the first one when the caller does not name a profile.
        const auto directory = remoteplay::profileDirectory();
        profile = directory / L"remoteplay-profile.dat";
        if (!std::filesystem::exists(profile)) {
            std::error_code error;
            for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
                const auto stem = entry.path().stem().wstring();
                const bool hex = stem.size() == 32 && std::all_of(stem.begin(), stem.end(), [](wchar_t c) {
                    return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f');
                });
                if (hex && entry.path().extension() == L".dat") { profile = entry.path(); break; }
            }
        }
    }
    std::cout << "profile=" << profile.string() << "\n";
    auto request = remoteplay::loadProfile(profile);
    if (!request) { std::cout << "PROBE profile-unavailable\n"; return 2; }
    if (codec == "h264") request->video.codec = remoteplay::Codec::H264;
    else if (codec == "h265") request->video.codec = remoteplay::Codec::H265;
    else if (codec == "h265hdr") request->video.codec = remoteplay::Codec::H265Hdr;
    if (bitrate != 0) request->video.bitrateKbps = bitrate;
    std::cout << "host=" << request->host << " codec=" << int(request->video.codec)
        << " bitrate=" << request->video.bitrateKbps << " profile=" << request->video.width << "x"
        << request->video.height << "@" << request->video.fps << "\n";

    gfx::D3D12DeviceContext context;
    gfx::DeviceContextDesc desc;
    Status status;
    if (!context.initialize(desc, status)) { std::cout << "PROBE no-d3d12\n"; return 3; }
    context.device()->AddRef();

    source::RemotePlayConnectDesc connect;
    connect.request = std::move(*request);
    connect.decodeMode = source::RemotePlayConnectDesc::DecodeMode::Automatic;
    connect.decodeDevice = std::shared_ptr<ID3D12Device>(context.device(), [](ID3D12Device* device) { device->Release(); });

    source::RemotePlaySessionSource session;
    if (!session.connect(std::move(connect))) { std::cout << "PROBE connect-refused\n"; return 4; }

    pipeline::FramePacket packet;
    const AVFrame* frame = nullptr;
    uint64_t frames = 0;
    int lastState = -1;
    std::wstring lastMessage;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto result = session.read(packet, &frame);
        if (result == source::SourceReadStatus::Frame) ++frames;
        else if (result == source::SourceReadStatus::Error) break;
        const auto snapshot = session.sessionSnapshot();
        if (int(snapshot.state) != lastState) {
            lastState = int(snapshot.state);
            std::cout << "state=" << stateName(snapshot.state) << " t="
                << std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - (deadline - std::chrono::seconds(seconds))).count() << "s\n";
        }
        if (snapshot.state == remoteplay::SessionState::LoginPinRequired) { std::cout << "PROBE pin-required\n"; break; }
        // A Failed session state must NOT end the probe: the recovery layer owns
        // the retry (including the RP_IN_USE backoff), so the deadline decides.
        const auto recovery = session.recoveryStatus();
        if (recovery.active && recovery.message != lastMessage) {
            lastMessage = recovery.message;
            std::wcout << L"recovery: " << recovery.message << L"\n";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto snapshot = session.sessionSnapshot();
    std::cout << "RESULT frames=" << frames << " state=" << stateName(snapshot.state)
        << " received=" << snapshot.video.accessUnits << " decoded=" << snapshot.decodedFrames
        << " waitingIdr=" << (snapshot.video.waitingForIdr ? 1 : 0) << " idrRequests=" << snapshot.video.idrRequests
        << " errorCode=" << snapshot.errorCode << " recoveredFrames=" << snapshot.framesLost
        << " hwDecode=" << (snapshot.hardwareDecode ? 1 : 0) << " decodeFallback=" << (snapshot.decodeFallback ? 1 : 0) << "\n";
    const auto recovery = session.recoveryStatus();
    if (!recovery.message.empty()) std::wcout << L"recovery: " << recovery.message << L"\n";
    session.close();
    return frames > 30 ? 0 : 1;
}
