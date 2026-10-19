#include "Bf4SessionInfo.h"

#include <charconv>
#include <cstdlib>

namespace fvr::bf4 {

namespace {
std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\0')) s.remove_suffix(1);
    return s;
}
template <typename T>
bool ToInt(std::string_view s, T& out) {
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc{} && p == s.data() + s.size();
}
} // namespace

std::optional<SessionInfo> ParseSessionInfo(std::string_view text, std::string* error) {
    SessionInfo info;
    auto fail = [&](std::string msg) -> std::optional<SessionInfo> {
        if (error) *error = std::move(msg);
        return std::nullopt;
    };
    // Stop at the first NUL: the section is zero-padded.
    if (auto nul = text.find('\0'); nul != std::string_view::npos) text = text.substr(0, nul);
    while (!text.empty()) {
        const auto eol = text.find('\n');
        std::string_view line = Trim(text.substr(0, eol));
        text = eol == std::string_view::npos ? std::string_view{} : text.substr(eol + 1);
        if (line.empty() || line.front() == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        const std::string_view key = Trim(line.substr(0, eq));
        const std::string_view value = Trim(line.substr(eq + 1));
        if (key == "protocol") { if (!ToInt(value, info.protocol)) return fail("bad protocol"); }
        else if (key == "adapter") info.adapterPath = std::string(value);
        else if (key == "log_dir") info.logDir = std::string(value);
        else if (key == "exe_sha256") info.exeSha256 = std::string(value);
        else if (key == "host_pipe") info.hostPipe = std::string(value);
        else if (key == "reload_mode") {
            const auto mode = interaction::ParseReloadMode(value);
            if (!mode) return fail("bad reload_mode");
            info.reloadMode = *mode;
        }
        else if (key == "developer") info.developerFeatures = value == "1" || value == "true";
        else if (key == "stereo") {
            if (value == "off") info.stereo = StereoSetting::Off;
            else if (value == "probe") info.stereo = StereoSetting::Probe;
            else if (value == "on") info.stereo = StereoSetting::On;
            else return fail("bad stereo");
        }
        else if (key == "stereo_route") {
            if (value == "split") info.stereoRoute = StereoRoute::Split;
            else if (value == "nvapi") info.stereoRoute = StereoRoute::Nvapi;
            else return fail("bad stereo_route");
        }
        else if (key == "stereo_separation") {
            const std::string v(value);
            char* end = nullptr;
            const float f = std::strtof(v.c_str(), &end);
            if (end == v.c_str() || *end || f < 0.0f || f > 100.0f) return fail("bad stereo_separation");
            info.stereoSeparation = f;
        }
        else if (key == "spoof_gpu_vendor") info.spoofGpuVendor = value == "1" || value == "true";
        else if (key == "launcher_pid") { if (!ToInt(value, info.launcherPid)) return fail("bad launcher_pid"); }
        else if (key == "research_dump_dir") info.researchDumpDir = std::string(value);
    }
    if (info.protocol != kSessionProtocol) return fail("protocol mismatch");
    if (info.adapterPath.empty()) return fail("missing adapter");
    return info;
}

} // namespace fvr::bf4
