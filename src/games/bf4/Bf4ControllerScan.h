#pragma once
// BF4 finds its game pads only by re-enumerating DirectInput game controllers when Windows
// reports a device change (research image, build 179547: the message handler +0x94B6E0 calls
// EnumDevices(GAMECTRL, attached) with callback +0x946830; its add-device routine +0x946690
// makes an XInput device, user index = the count found so far, of every device its
// IsXInputDevice +0x94A090 accepts, a DirectInput device of the rest). That IsXInputDevice is
// the DirectX SDK's WMI scan of every PnP device, on BF4's main thread, once per known device
// and new device: live 2026-10-20, 5.5 s for this PC's 309 devices and 4-11 s in-level freezes.
// With no XInput pad attached BF4 never polls XInput at all, so the VR-driven virtual pad
// (Bf4XInputSink) reached the game only while a real pad was plugged in.
//
// While the adapter runs:
// - BF4's IsXInputDevice is answered from raw-input device names (the same "IG_" and VID/PID
//   test the WMI scan makes, without WMI: milliseconds), a MinHook on the enrolled binding
//   `Input.IsXInputDevice`, removed with every other hook (exact bytes back);
// - its game-controller enumerations pass through to DirectInput (or the Steam overlay's
//   entry), and when they found no XInput pad a stand-in Xbox 360 pad is offered first, which
//   BF4 takes as XInput user 0 (the virtual pad). Only with the code hook installed: BF4's own
//   scan would not know the stand-in.
// - one device change is posted to the game window after adoption if BF4 has not enumerated
//   yet, so the stand-in exists from the menus on.

#include <cstdint>
#include <string>

namespace fvr::bf4 {

class ControllerScan {
public:
    // `isXInputDevice`: BF4's routine (binding Input.IsXInputDevice), or null to leave BF4's
    // own scan in place (enumerations then pass through unchanged). MinHook must be initialised.
    static bool Install(void* isXInputDevice);
    // Puts the EnumDevices class vtable entries back while they still hold ours (exact restore);
    // the code hook passes through from then on and is unpatched with every other MinHook hook
    // (the adapter's detach worker), or here with `unhookCode` (never from a Present detour).
    static void Remove(bool unhookCode = false);
    // Does the code at `fn` look like BF4's WMI routine (its IEnumWbemClassObject::Next(10000 ms,
    // 20) call at +0x1A7: mov r8d,esi; mov edx,2710h; call [rax+20h]), read under an exception
    // guard? Checked before the code hook goes in.
    static bool LooksLikeWmiCheck(const void* fn);
    static void SetEnabled(bool enabled);
    // After adoption: posts one WM_DEVICECHANGE (DBT_DEVNODES_CHANGED) if BF4 has not
    // enumerated since Install, so it builds its device list with the stand-in. Once only.
    static bool RequestEnumeration(void* window);
    // Developer evidence: each enumeration's devices and each answered check, as log lines
    // (the first 64). Called on BF4's thread; null turns it off.
    using LogFn = void (*)(const char* line);
    static void SetLog(LogFn log);
    // Steam's virtual pads: XInput pads (default, as the overlay's WMI answer has them), or, as
    // developer experiments, DirectInput pads, or left out (a PC without them, Steam Input off).
    enum class SteamPads { AsXInput = 0, AsDirectInput = 1, Hidden = 2 };
    static void SetSteamPads(SteamPads mode);

    struct Stats {
        std::uint64_t enumerations = 0;  // BF4's game-controller enumerations
        std::uint64_t devices = 0;       // devices DirectInput reported to it
        std::uint64_t xinputDevices = 0; // ... of them XInput pads
        std::uint64_t standIns = 0;      // stand-in pads offered
        std::uint64_t checks = 0, accepted = 0; // BF4 IsXInputDevice calls answered, of them true
        float lastMs = 0, maxMs = 0;     // DirectInput's own enumeration time
        float checkMaxMs = 0;            // slowest answered check
        bool codeHook = false;
        bool requested = false;          // the device change was posted
    };
    static Stats GetStats();
    static std::string Describe();

    // Pure: is this enumeration one for game controllers (the class or a controller type)?
    static bool Filters(std::uint32_t devType);
    // Pure: an XInput HID device path ("...VID_045E&PID_028E&IG_00...") and its product as
    // DirectInput's guidProduct.Data1 has it (MAKELONG(vid, pid)).
    static bool XInputName(const wchar_t* name, std::uint32_t* product);
    // Is that DirectInput product an attached XInput device (raw-input device names)? Steam's
    // virtual pads are no raw-input devices: BF4's check answers them by kSteamVirtualPad.
    static bool IsXInputProduct(std::uint32_t product);
    // Steam Input's virtual pad (VID 28DE, PID 11FF, "Controller (XBOX 360 For Windows)"). The
    // Steam overlay lists these to the game through DirectInput and answers the game's WMI scan
    // for them with XInput device IDs; they are no raw-input devices (live 2026-10-20: 8 of them
    // in BF4, none in a plain process). BF4 made them its XInput devices, the first one user 0,
    // which is how the VR pad reached the game; the stand-in alone was taken but never polled.
    static constexpr std::uint32_t kSteamVirtualPad = 0x11FF28DEu;
    // The stand-in: an Xbox 360 controller's product id, MAKELONG(0x045E, 0x028E).
    static constexpr std::uint32_t kStandInProduct = 0x028E045Eu;
    static constexpr std::uint32_t kStandInInstance = 0x46565250u; // guidInstance.Data1 ("FVRP")
};

} // namespace fvr::bf4
