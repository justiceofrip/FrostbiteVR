#pragma once
// Developer research channel (developer sessions with a research directory only; never in
// player sessions). A text file of numbered commands that the adapter executes once each on
// the render thread, so an offline tester can look at the game (snapshots) and drive its
// menus through the SAME pointer path the VR laser uses (PointerEvent -> Win32PointerSink).
//
//   <id> snap <name>        full backbuffer -> <research>/<name>.bmp (half size)
//   <id> click <u> <v>      move, press, release at normalised client coords (0..1)
//   <id> move <u> <v>       hover only
//   <id> key <name>         key press (enter, esc, space, up, down, left, right, tab, ...)
//   <id> dumpimage          read-only copy of the decrypted bf4.exe image (offline RE)
//   <id> scanq <rva>        find objects whose first qword is exe base + rva (a vtable);
//                           background thread, read-only, at most 16 addresses
//   <id> peek <addr> <len>  hex dump of <= 256 readable bytes at an absolute address
//   <id> census             read-only census of weapon-firing objects (background thread)
// The channel never writes game memory.
//   <id> type <text>        characters to the game window (e.g. its console); '_' = space
//   <id> depthprobe         installs the depth capture (3D from depth), then after ~2 s logs
//                           what it found and writes vr-snapshot-depth.bmp
//   <id> mouse <dx> <dy>    relative mouse counts through SendInput (focused game only); logs
//                           the camera's yaw/pitch before and ~20 frames after (rad/count)
//   <id> sendkey <scan>     a key by set-1 scan code through SendInput (held 6 frames), as the
//                           holster gestures press BF4's keys
//   <id> setting <id> <n>   sets a mod menu item to choice n (as if clicked in the headset)
//   <id> viewcopies <0..3>  which of the view block's transform copies get the head pose
//                           (bit 0: +0x00, bit 1: +0x40; 3 = both, the default)
//   <id> viewdump <name>    the next submitted view block (0x1020 bytes, as the game
//                           submitted it, before any override) -> <research>/<name>.bin
//   <id> aimtrace <name>    the last 4096 controller-aim updates -> <research>/<name>.csv

#include "Bf4PointerInjector.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fvr::bf4 {

struct ResearchCommand {
    enum class Kind : std::uint8_t {
        Snap, Click, Move, Key, DumpImage, ScanQ, Peek, Type, Census, DepthProbe, Mouse, SendKey, Setting, ViewCopies,
        ViewDump, AimTrace, ShaderNames, ShaderDump, DrawProbe, FirstPersonView, DeviceChange, FirstPersonHide, FirstPersonMask, SecondView, SecondSnap, DrawStack, Unknown
    };
    std::uint64_t id = 0;
    Kind kind = Kind::Unknown;
    std::string arg;    // snap name, key name, or the unparsed verb for Unknown
    float u = 0, v = 0; // click/move
    std::uint64_t value = 0; // scanq rva, peek address
    std::uint32_t length = 0; // peek length
    std::int32_t x = 0, y = 0; // mouse counts; setting value (x)
};

// Lines "<id> <verb> [args]"; '#' comments and blank lines skipped. Returns the commands
// with id > afterId in file order. Malformed lines become Unknown (reported, not run).
std::vector<ResearchCommand> ParseResearchCommands(std::string_view text, std::uint64_t afterId);
// The highest command id in the file: what a session opening the channel starts after, so
// commands of earlier sessions are never replayed (a stale menu click once landed on a
// multiplayer tile at startup).
std::uint64_t LastResearchCommandId(std::string_view text);

// Windows virtual-key code for a key name; 0 if unknown. Names: enter, esc, space, tab,
// backspace, up, down, left, right, grave (console), f1..f12, a..z, 0..9.
unsigned ResearchKeyCode(std::string_view name);

// Pointer events for a hover/click at (u, v), spread over frames so a UI sees hover, then
// press, then release, then the laser lets go of the cursor.
struct TimedPointerEvent {
    std::uint32_t frameDelay = 0; // frames after the previous event
    PointerEvent event;
};
std::vector<TimedPointerEvent> PointerSequence(float u, float v, int clientWidth, int clientHeight, bool click);

} // namespace fvr::bf4
