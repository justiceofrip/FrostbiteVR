#pragma once
// Scene depth for depth-based stereo (Windows, in the adapter; opt-in).
//
// Observes ID3D11DeviceContext::ClearDepthStencilView (a MinHook detour on d3d11.dll's own
// implementation; the call itself always proceeds unchanged) to find the frame's main depth
// buffer: the largest single-sample depth target with the backbuffer's aspect (shadow maps
// and other targets have other shapes). Just before each clear of it, a copy of its contents
// is recorded on the same context, so it lands in order even when the game records on
// deferred contexts. Frostbite clears depth again before drawing the first-person weapon
// (BC2 does), so with two or more clears a frame the copy made before the last clear holds
// the world; with one clear a frame, the live buffer is copied at Present instead.
// Towards the game this is read-only: its resources are only ever copied from.
#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <mutex>
#include <string>

namespace fvr::bf4 {

class DepthCapture {
public:
    struct Stats {
        std::uint64_t clears = 0, mainClears = 0, copies = 0, presents = 0, picks = 0, msaaRefused = 0;
        std::uint32_t width = 0, height = 0, format = 0;
        float clearValue = 1;
        bool split = false; // two or more clears a frame: the pre-clear copy holds the world
    };
    struct Frame {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture; // our copy (typeless depth family), owned
        std::uint32_t format = 0;           // its DXGI format (typeless)
        float clear = 1;                    // the value the game clears it to (0: reversed depth)
    };

    ~DepthCapture();
    // Hooks the immediate context's ClearDepthStencilView. False when the function is not
    // d3d11.dll's or the hook cannot be made (then depth stereo stays off).
    bool Install(ID3D11DeviceContext* immediate);
    bool Installed() const { return installed_; }
    void Remove(); // unhooks (the game's clears go straight to d3d11 again)
    // Once per Present, on the presenting thread: the frame's scene depth, if one is known.
    // `aspect` is the backbuffer's (the main depth matches it).
    bool TakeFrame(ID3D11DeviceContext* immediate, float aspect, Frame& out);
    Stats GetStats() const;
    std::string Describe() const;
    // Developer evidence: the last copy as a greyscale BMP (near bright, far dark).
    bool WriteBmp(ID3D11DeviceContext* immediate, const std::string& path, float nearZ, float farZ);

    void OnClear(ID3D11DeviceContext* context, ID3D11DepthStencilView* dsv, float value); // from the detour

private:
    bool EnsureCopy(ID3D11Device* device);
    bool HookEntry(void* entry);

    mutable std::mutex mutex_;
    bool installed_ = false;
    std::array<void*, 4> entries_{}; // hooked ClearDepthStencilView implementations
    std::size_t hooked_ = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> main_, copy_;
    D3D11_TEXTURE2D_DESC mainDesc_{};
    float aspect_ = 0;
    unsigned clearsThisFrame_ = 0;
    std::uint64_t lastMainClear_ = 0; // presents count at the main depth's latest clear
    std::array<unsigned, 8> clearHistory_{};
    std::uint64_t historyIndex_ = 0;
    Stats stats_;
};

} // namespace fvr::bf4
