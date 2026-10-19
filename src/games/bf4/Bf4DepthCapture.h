#pragma once
// Scene depth for depth-based stereo (Windows, in the adapter; opt-in).
//
// Observes ID3D11DeviceContext::ClearDepthStencilView (a MinHook detour on d3d11.dll's own
// implementation; the call itself always proceeds unchanged) to find the frame's main depth
// buffer: the largest depth target with the backbuffer's aspect (shadow maps and other
// targets have other shapes), single- or multisampled. With two or more clears a frame (BC2
// clears again before the first-person weapon), a copy is recorded on the clearing context
// just before each clear, so it lands in order even on deferred contexts, and the one made
// before the last clear holds the world. With one clear a frame, the live buffer is copied
// at Present instead (and nothing before the clears). BF4 (live, 2026-10-19): one clear a
// frame of a 4x MSAA R24G8 buffer at the render resolution, cleared to 1 (standard depth).
// Towards the game this is read-only: its resources are only ever copied from.
#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace fvr::bf4 {

class DepthCapture {
public:
    struct Stats {
        std::uint64_t clears = 0, mainClears = 0, copies = 0, presents = 0, picks = 0;
        std::uint64_t msaaRefused = 0; // clears of a multisampled main depth (read via sample 0)
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
    // Developer evidence: the last copy as a greyscale BMP (log scale: 0.1 m white, 200 m
    // black; sample 0 of a multisampled one), and in `summary` the metres at the centre, the
    // nearest and the share of pixels at the far plane.
    bool WriteBmp(ID3D11DeviceContext* immediate, const std::string& path, float nearZ, float farZ,
                  std::string* summary = nullptr);

    void OnClear(ID3D11DeviceContext* context, ID3D11DepthStencilView* dsv, float value); // from the detour

private:
    bool EnsureCopy(ID3D11Device* device);
    // The copy's raw depth values (0..1), reading sample 0 of a multisampled copy.
    bool ReadDepth(ID3D11DeviceContext* immediate, std::vector<float>& values, std::uint32_t& w, std::uint32_t& h);
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
