#include "Bf4ResearchControl.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>

namespace fvr::bf4 {

namespace {
std::string_view NextToken(std::string_view& s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    std::size_t n = 0;
    while (n < s.size() && s[n] != ' ' && s[n] != '\t' && s[n] != '\r') ++n;
    const std::string_view t = s.substr(0, n);
    s.remove_prefix(n);
    return t;
}

bool ParseHex(std::string_view t, std::uint64_t& out) {
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) t.remove_prefix(2);
    auto [p, ec] = std::from_chars(t.data(), t.data() + t.size(), out, 16);
    return !t.empty() && ec == std::errc{} && p == t.data() + t.size();
}

bool ParseInt(std::string_view t, std::int32_t& out) {
    auto [p, ec] = std::from_chars(t.data(), t.data() + t.size(), out);
    return !t.empty() && ec == std::errc{} && p == t.data() + t.size();
}

bool ParseUnit(std::string_view t, float& out) {
    const std::string v(t);
    char* end = nullptr;
    out = std::strtof(v.c_str(), &end);
    return !v.empty() && end && *end == '\0' && out >= 0.0f && out <= 1.0f;
}
} // namespace

std::vector<ResearchCommand> ParseResearchCommands(std::string_view text, std::uint64_t afterId) {
    std::vector<ResearchCommand> out;
    while (!text.empty()) {
        const auto eol = text.find('\n');
        std::string_view line = text.substr(0, eol);
        text = eol == std::string_view::npos ? std::string_view{} : text.substr(eol + 1);
        std::string_view rest = line;
        const std::string_view idText = NextToken(rest);
        if (idText.empty() || idText.front() == '#') continue;
        ResearchCommand c;
        auto [p, ec] = std::from_chars(idText.data(), idText.data() + idText.size(), c.id);
        if (ec != std::errc{} || p != idText.data() + idText.size() || c.id <= afterId) continue;
        const std::string_view verb = NextToken(rest);
        const std::string_view a1 = NextToken(rest), a2 = NextToken(rest);
        if (verb == "snap" && !a1.empty()) { c.kind = ResearchCommand::Kind::Snap; c.arg = std::string(a1); }
        else if ((verb == "click" || verb == "move") && ParseUnit(a1, c.u) && ParseUnit(a2, c.v))
            c.kind = verb == "click" ? ResearchCommand::Kind::Click : ResearchCommand::Kind::Move;
        else if (verb == "key" && ResearchKeyCode(a1) != 0) { c.kind = ResearchCommand::Kind::Key; c.arg = std::string(a1); }
        else if (verb == "dumpimage") c.kind = ResearchCommand::Kind::DumpImage;
        else if (verb == "census") c.kind = ResearchCommand::Kind::Census;
        else if (verb == "scanq" && ParseHex(a1, c.value) && c.value > 0 && c.value < 0x40000000u)
            c.kind = ResearchCommand::Kind::ScanQ;
        else if (verb == "peek" && ParseHex(a1, c.value) && c.value >= 0x10000) {
            std::uint64_t len = 0;
            if (std::from_chars(a2.data(), a2.data() + a2.size(), len).ec == std::errc{} && len >= 1 && len <= 256) {
                c.kind = ResearchCommand::Kind::Peek;
                c.length = static_cast<std::uint32_t>(len);
            } else c.arg = std::string(verb);
        }
        else if (verb == "type" && !a1.empty() && a1.size() <= 120) { c.kind = ResearchCommand::Kind::Type; c.arg = std::string(a1); }
        else if (verb == "depthprobe") c.kind = ResearchCommand::Kind::DepthProbe;
        else if (verb == "viewcopies" && ParseInt(a1, c.x) && c.x >= 0 && c.x <= 3) c.kind = ResearchCommand::Kind::ViewCopies;
        else if (verb == "mouse" && ParseInt(a1, c.x) && ParseInt(a2, c.y) && std::abs(c.x) <= 2000 && std::abs(c.y) <= 2000)
            c.kind = ResearchCommand::Kind::Mouse;
        else if (verb == "sendkey" && ParseHex(a1, c.value) && c.value >= 1 && c.value <= 0xFF)
            c.kind = ResearchCommand::Kind::SendKey;
        else if (verb == "setting" && !a1.empty() && a1.size() <= 40 && ParseInt(a2, c.x) && c.x >= 0 && c.x <= 16) {
            c.kind = ResearchCommand::Kind::Setting;
            c.arg = std::string(a1);
        }

        else c.arg = std::string(verb);
        // A snap name becomes a file name: keep it to [A-Za-z0-9_-].
        if (c.kind == ResearchCommand::Kind::Snap &&
            !std::all_of(c.arg.begin(), c.arg.end(), [](char ch) {
                return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
            }))
            c.kind = ResearchCommand::Kind::Unknown;
        out.push_back(std::move(c));
    }
    return out;
}

unsigned ResearchKeyCode(std::string_view n) {
    if (n == "enter") return 0x0D;
    if (n == "esc") return 0x1B;
    if (n == "space") return 0x20;
    if (n == "tab") return 0x09;
    if (n == "backspace") return 0x08;
    if (n == "grave") return 0xC0; // VK_OEM_3: the Frostbite console key on US layouts
    if (n == "left") return 0x25;
    if (n == "up") return 0x26;
    if (n == "right") return 0x27;
    if (n == "down") return 0x28;
    if (n.size() >= 2 && n.size() <= 3 && n.front() == 'f') {
        int k = 0;
        auto [p, ec] = std::from_chars(n.data() + 1, n.data() + n.size(), k);
        if (ec == std::errc{} && p == n.data() + n.size() && k >= 1 && k <= 12) return 0x70 + static_cast<unsigned>(k - 1);
    }
    if (n.size() == 1 && n[0] >= 'a' && n[0] <= 'z') return static_cast<unsigned>(n[0] - 'a' + 'A');
    if (n.size() == 1 && n[0] >= '0' && n[0] <= '9') return static_cast<unsigned>(n[0]);
    return 0;
}

std::vector<TimedPointerEvent> PointerSequence(float u, float v, int w, int h, bool click) {
    std::vector<TimedPointerEvent> out;
    if (w <= 0 || h <= 0) return out;
    const int x = std::clamp(static_cast<int>(u * w), 0, w - 1), y = std::clamp(static_cast<int>(v * h), 0, h - 1);
    out.push_back({0, {PointerEvent::Type::Move, x, y}});
    if (click) {
        out.push_back({6, {PointerEvent::Type::ButtonDown, x, y}}); // hover first
        out.push_back({4, {PointerEvent::Type::ButtonUp, x, y}});
    }
    out.push_back({6, {PointerEvent::Type::Deactivate, x, y}}); // never keep the cursor
    return out;
}

} // namespace fvr::bf4
