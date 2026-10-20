// Emulated BF4 process for end-to-end adapter tests on any Windows machine (CI included).
// No game, no GPU (WARP), no headset. It plays the launcher and the game at once:
//   1. publishes the session section exactly like launcher/fvrlaunch/session.py
//   2. loads ./dxgi.dll (our proxy) the way bf4.exe would, which loads the adapter
//   3. renders frames through a real DXGI swapchain into a window with a "button"
//   4. it also plays the XR host's side of the theater link (menu lane): it receives the
//      game screen as the panel image and sends the laser pointer back as MenuControl,
//      which must click that button through the adapter's Win32 pointer path
//   5. engine stereo: loads ./nvapi64.dll (our shim) and runs the 3D Vision Direct call
//      sequence (red left eye, blue right eye); the adapter must capture both eyes,
//      write the side-by-side snapshot and report "working"
//   6. loads a fake pbcl.dll mid-run: the adapter must lock itself off (MP lockdown) and
//      the shim must report stereo off from then on
// Exit code 0 = all checks passed.
//
// d3d11.dll is delay-loaded (see cmake/Bf4Adapter.cmake) so that when D3D11 finally binds
// its "dxgi.dll" import, the already-loaded module with that name is OUR proxy - the same
// situation as bf4.exe with the proxy in its folder. That exercises the GLOBALROOT
// forwarders, not just the adapter.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11_1.h>
#include <xinput.h>

#include "fvr/graphics/D3D11SharedPair.h"
#include "fvr/ipc/MenuChannel.h"
#include "fvr/ipc/RemoteFrameProvider.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

namespace {
constexpr int kWidth = 1280, kHeight = 720;
int g_buttonClicks = 0;
bool g_downInside = false;

// An 80x40 button at the centre of the client area as it actually is: Windows caps a
// window to the desktop (CI runners have small desktops), and the laser aims at
// normalised client coordinates, not at fixed pixels.
RECT CentreButton(HWND hwnd) {
    RECT c{};
    GetClientRect(hwnd, &c);
    const LONG cx = (c.right - c.left) / 2, cy = (c.bottom - c.top) / 2;
    return RECT{cx - 40, cy - 20, cx + 40, cy + 20};
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    const POINT p{static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
    const RECT button = CentreButton(hwnd);
    switch (msg) {
    case WM_LBUTTONDOWN: g_downInside = PtInRect(&button, p) != FALSE; return 0;
    case WM_LBUTTONUP:
        if (g_downInside && PtInRect(&button, p)) ++g_buttonClicks;
        g_downInside = false;
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

std::string Status() {
    HMODULE adapter = GetModuleHandleW(L"FrostbiteVR.Bf4.dll");
    if (!adapter) return {};
    using StatusFn = int(__cdecl*)(char*, int);
    auto fn = reinterpret_cast<StatusFn>(GetProcAddress(adapter, "FvrBf4_Status"));
    if (!fn) return {};
    std::string buf(8192, '\0');
    buf.resize(static_cast<std::size_t>(fn(buf.data(), static_cast<int>(buf.size()))));
    return buf;
}

int g_fail = 0;
}
bool CheckEyeResamplerReticle(ID3D11Device* device, const char*& why); // ResamplerCheck.cpp
bool CheckDepthCapture(ID3D11Device* device, const char*& why);        // DepthCaptureCheck.cpp
bool CheckShaderCensus(ID3D11Device* device, const char*& why);        // ShaderCensusCheck.cpp
bool CheckDrawProbe(ID3D11Device* device, const char*& why);           // ShaderCensusCheck.cpp
bool CheckFirstPersonView(ID3D11Device* device, const char*& why);     // ShaderCensusCheck.cpp
bool CheckHudSplit(ID3D11Device* device, const char*& why);           // ShaderCensusCheck.cpp
bool CheckFirstPersonArms(ID3D11Device* device, const char*& why);    // ShaderCensusCheck.cpp
bool CheckControllerScan(const char*& why);                            // ControllerScanCheck.cpp
namespace {
void Check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_fail;
}
} // namespace

int wmain() {
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring dir(exePath);
    dir.resize(dir.find_last_of(L'\\'));
    char dirUtf8[MAX_PATH * 3]{};
    WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, dirUtf8, sizeof(dirUtf8), nullptr, nullptr);

    // Fresh evidence only: the adapter appends to its log, and research ids are one-shot.
    DeleteFileW((dir + L"\\bf4-adapter.log").c_str());
    DeleteFileW((dir + L"\\research-emu\\cmd.txt").c_str());
    DeleteFileW((dir + L"\\research-emu\\ack.txt").c_str());

    // 1. Session section, as the launcher writes it.
    const std::string session = "protocol=1\nadapter=" + std::string(dirUtf8) + "\\FrostbiteVR.Bf4.dll\nlog_dir=" +
                                std::string(dirUtf8) + "\nreload_mode=manual\ndeveloper=1\nstereo=probe\nstereo_route=nvapi\n"
                                "exe_sha256=9d9d1a3fca6ffd4c34a543b3fd7b049336ddbd43c0e6c5eee742e51431a6c2aa\n"
                                "research_dump_dir=" + std::string(dirUtf8) + "\\research-emu\nlauncher_pid=" +
                                std::to_string(GetCurrentProcessId()) + "\n";
    HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4096, L"Local\\FrostbiteVR_BF4_Session");
    if (!section) return 2;
    void* view = MapViewOfFile(section, FILE_MAP_WRITE, 0, 0, 4096);
    std::memcpy(view, session.c_str(), session.size() + 1);

    // XR host side of the theater link: channels + the token file the adapter looks for.
    // FVR_BF4_EXTERNAL_XR_HOST=1: the real FvrXrHost.exe (e.g. on the fake OpenXR runtime)
    // owns the channels and writes the token file; this process is only the game then.
    wchar_t externalFlag[8]{};
    const bool externalHost = GetEnvironmentVariableW(L"FVR_BF4_EXTERNAL_XR_HOST", externalFlag, 8) > 0;
    fvr::ipc::RemoteFrameProvider xrFrames;
    fvr::ipc::MenuChannel xrMenu;
    if (externalHost) {
        for (int i = 0; i < 500 && GetFileAttributesW((dir + L"\\xr-channel.txt").c_str()) == INVALID_FILE_ATTRIBUTES; ++i) Sleep(20);
        Check(GetFileAttributesW((dir + L"\\xr-channel.txt").c_str()) != INVALID_FILE_ATTRIBUTES, "external XR host published its channel token");
    } else {
        const bool hostUp = xrFrames.Create() && xrMenu.CreateHost(xrFrames.Token());
        Check(hostUp, "emulated XR host created the frame + menu channels");
        const std::wstring& token = xrFrames.Token();
        std::ofstream(dir + L"\\xr-channel.txt") << std::string(token.begin(), token.end());
    }

    // 2. Load the proxy from our own folder, as the game would.
    HMODULE proxy = LoadLibraryW((dir + L"\\dxgi.dll").c_str());
    Check(proxy != nullptr, "proxy dxgi.dll loaded from the game folder");

    // 3. Fake game window + swapchain.
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"Bf4EmuWindow";
    RegisterClassExW(&wc);
    RECT r{0, 0, kWidth, kHeight};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Battlefield 4 (emulated)", WS_OVERLAPPEDWINDOW, 50, 50,
                                r.right - r.left, r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
    ShowWindow(hwnd, SW_SHOW);

    // Wait for the proxy's init thread: adapter loaded, vtable hooked, first status published.
    for (int i = 0; i < 250 && Status().find("TheaterView") == std::string::npos; ++i) Sleep(20);
    Check(GetModuleHandleW(L"FrostbiteVR.Bf4.dll") != nullptr, "adapter loaded by proxy");

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = kWidth;
    sd.BufferDesc.Height = kHeight;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain* sc = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    // WARP everywhere (CI); with an external XR host the "game" uses the default hardware
    // adapter like bf4.exe, since a WARP texture cannot be shared with the host's GPU device.
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, externalHost ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP, nullptr, 0,
                                             nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev, nullptr, &ctx)))
        return 3;
    ID3D11Texture2D* back = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back));
    dev->CreateRenderTargetView(back, nullptr, &rtv);
    back->Release();

    auto pump = [] {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
    };
    const float olive[4] = {0.24f, 0.26f, 0.16f, 1.0f};
    // Host side, per frame: laser at the button centre, trigger held for a few frames.
    fvr::graphics::D3D11PairConsumer panelConsumer;
    fvr::graphics::TextureDescriptor panelOpened{};
    std::array<ID3D11Texture2D*, 2> panel{nullptr, nullptr};
    fvr::graphics::PairTicket panelTicket{};
    bool panelFeedback = false, panelConsumed = false;
    unsigned panelImages = 0, panelWidth = 0, panelHeight = 0, panelErrors = 0, panelBusy = 0;
    std::uint64_t controlSequence = 0, menuEpoch = 0;
    unsigned char centre[4] = {};
    std::uint64_t inputGeneration = 0;
    auto hostFrame = [&](int frame) {
        if (externalHost) return; // the real host consumes the panel and owns the pointer
        // Controller sample, as the real host publishes every XR frame (theater input mode):
        // right A (jump) held and the left stick pushed forward (this session reloads by
        // magazine gesture, so right B is not a pad button).
        fvr::interaction::InputFrame input{};
        input.generation = ++inputGeneration;
        input.spaceGeneration = 1;
        input.predictedNs = 1000000 + static_cast<std::int64_t>(frame);
        input.focused = true;
        for (auto& h : input.hands) h.active = fvr::interaction::Components;
        if (frame >= 90) {
            input.hands[1].held = fvr::interaction::Primary;
            input.hands[0].stickY = 1.0f;
        }
        xrFrames.UpdateInput(input);
        fvr::ipc::MenuState st{};
        if (xrMenu.ReadState(st) == fvr::ipc::ChannelResult::Ok && st.mode == fvr::ipc::MenuMode::Menu) menuEpoch = st.epoch;
        fvr::ipc::MenuControl c{};
        c.sequence = ++controlSequence;
        c.space = 1;
        c.flags = fvr::ipc::MenuFocused;
        if (menuEpoch && frame >= 20) {
            c.menuEpoch = menuEpoch;
            c.u = 0.5f; // button centre
            c.v = 0.5f;
            c.flags |= fvr::ipc::MenuPoint;
            if (frame >= 70 && frame < 80) c.flags |= fvr::ipc::MenuDown;
        }
        xrMenu.PublishControl(c);
        if (panelFeedback && xrMenu.SurfaceConsumed(panelTicket, panelConsumed) != fvr::ipc::ChannelResult::Busy) panelFeedback = false;
        fvr::ipc::MenuSurface s;
        if (panelFeedback || xrMenu.TakeSurface(s) != fvr::ipc::ChannelResult::Ok) return;
        panelTicket = s.ticket;
        panelFeedback = true;
        panelConsumed = false;
        const auto& d = s.descriptor;
        if (std::memcmp(&panelOpened, &d, sizeof(d)) != 0) {
            panelOpened = {};
            panelConsumer.Reset();
            for (auto& p : panel) if (p) { p->Release(); p = nullptr; }
            if (panelConsumer.Open(dev, d) == fvr::graphics::TransferResult::Ok) {
                D3D11_TEXTURE2D_DESC td{};
                td.Width = d.width; td.Height = d.height; td.Format = DXGI_FORMAT(d.format);
                td.MipLevels = td.ArraySize = td.SampleDesc.Count = 1;
                bool ok = true;
                for (auto& p : panel) ok = ok && SUCCEEDED(dev->CreateTexture2D(&td, nullptr, &p));
                if (ok) panelOpened = d;
            }
        }
        // Busy is the pair's zero-wait backpressure (the real host drops that surface too);
        // only Invalid/Unavailable/Faulted are errors. Busy shows up under CPU contention.
        auto copy = fvr::graphics::TransferResult::Unavailable;
        if (fvr::graphics::Valid(panelOpened)) copy = panelConsumer.Copy(s.ticket, {{{panel[0], 0}, {panel[1], 0}}});
        panelConsumed = copy == fvr::graphics::TransferResult::Ok;
        if (panelConsumed) {
            ++panelImages;
            panelWidth = d.width;
            panelHeight = d.height;
            D3D11_TEXTURE2D_DESC td{};
            panel[0]->GetDesc(&td);
            td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; td.MiscFlags = 0;
            ID3D11Texture2D* stage = nullptr;
            if (SUCCEEDED(dev->CreateTexture2D(&td, nullptr, &stage))) {
                ctx->CopyResource(stage, panel[0]);
                D3D11_MAPPED_SUBRESOURCE m{};
                if (SUCCEEDED(ctx->Map(stage, 0, D3D11_MAP_READ, 0, &m))) {
                    std::memcpy(centre, static_cast<const unsigned char*>(m.pData) + (td.Height / 2) * m.RowPitch + (td.Width / 2) * 4, 4);
                    ctx->Unmap(stage, 0);
                }
                stage->Release();
            }
        } else if (copy == fvr::graphics::TransferResult::Busy) {
            ++panelBusy;
        } else {
            ++panelErrors;
        }
        if (xrMenu.SurfaceConsumed(panelTicket, panelConsumed) != fvr::ipc::ChannelResult::Busy) panelFeedback = false;
    };
    for (int frame = 0; frame < 150; ++frame) {
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ctx->ClearRenderTargetView(rtv, olive);
        sc->Present(0, 0);
        hostFrame(frame);
        pump();
        Sleep(5);
    }
    std::string s = Status();
    Check(s.find("frames=") != std::string::npos && s.find("frames=0\n") == std::string::npos, "Present hook sees frames");
    Check(s.find("TheaterView: candidate") != std::string::npos, "theater view became a candidate");
    Check(s.find("ManualReload: disabled") != std::string::npos, "manual reload stays disabled (roadmap)");
    Check(s.find("reload_mode=manual") != std::string::npos, "manual request is the magazine gesture (virtual pad)");
    Check(s.find("HeadTrackedCamera: disabled (needs") != std::string::npos, "camera gated on unenrolled bindings");
    if (!externalHost) {
        Check(panelImages > 20 && panelWidth == kWidth && panelHeight == kHeight,
              "XR host received the game screen as the theater panel (menu lane)");
        if (panelErrors || panelBusy) std::printf("panel copies: errors=%u busy=%u images=%u\n", panelErrors, panelBusy, panelImages);
        Check(centre[0] > 50 && centre[0] < 72 && centre[2] > 30 && centre[2] < 50 && panelErrors == 0,
              "theater panel carries the game image (olive centre pixel)");
        Check(g_buttonClicks == 1, "XR host laser (MenuControl) clicked the emulated menu button exactly once");
        // Virtual gamepad: the game polls XInput exactly as bf4.exe does (XINPUT9_1_0).
        XINPUT_STATE pad{};
        DWORD padResult = ERROR_DEVICE_NOT_CONNECTED;
        if (HMODULE xi = LoadLibraryW(L"xinput9_1_0.dll"))
            if (auto getState = reinterpret_cast<DWORD(WINAPI*)(DWORD, XINPUT_STATE*)>(GetProcAddress(xi, "XInputGetState")))
                padResult = getState(0, &pad);
        if (GetModuleHandleW(L"xinput9_1_0.dll") == nullptr) {
            std::printf("SKIP virtual gamepad: xinput9_1_0.dll is not available on this machine\n");
        } else {
            Check(padResult == ERROR_SUCCESS && (pad.Gamepad.wButtons & XINPUT_GAMEPAD_A) && pad.Gamepad.sThumbLY > 32000,
                  "game's XInputGetState sees the VR controllers (right A = jump, left stick forward)");
        }
    }

    // 4b. World lane (head-tracked view): the emulated host asks for eye pairs on the frame
    // lane, as the real host does once the menu lane says Gameplay. The emulator has no
    // renderer to move, so FVR_BF4_WORLD_UNTRACKED sends the game frame as it is; what is
    // checked is the transport (lease -> crops -> bridge -> host copy) and each eye's crop.
    // Game frame: left half orange, right half azure. Eyes (tangents) are asymmetric, so the
    // colour boundary sits at 62.5 % of the left eye's width and 37.5 % of the right eye's.
    if (!externalHost) {
        SetEnvironmentVariableW(L"FVR_BF4_WORLD_UNTRACKED", L"1");
        ID3D11DeviceContext1* ctx1 = nullptr;
        ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&ctx1));
        LUID luid{};
        IDXGIDevice* dxgiDevice = nullptr;
        IDXGIAdapter* adapter = nullptr;
        DXGI_ADAPTER_DESC adapterDesc{};
        if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice))) &&
            SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) && SUCCEEDED(adapter->GetDesc(&adapterDesc)))
            luid = adapterDesc.AdapterLuid;
        if (adapter) adapter->Release();
        if (dxgiDevice) dxgiDevice->Release();
        fvr::runtime::PresentationRequirements req{};
        req.width = 400;
        req.height = 448;
        req.format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        req.adapterLow = luid.LowPart;
        req.adapterHigh = luid.HighPart;
        fvr::runtime::TrackingFrame t{};
        t.spaceGeneration = 1;
        t.focused = t.headValid = true;
        t.head.orientation = {0, 0, 0, 1};
        t.referenceHead = t.head;
        t.eyes = {t.head, t.head};
        t.eyes[0].position.x = -0.032f;
        t.eyes[1].position.x = 0.032f;
        t.fov[0] = {-1.0f, 0.6f, 0.8f, -0.8f};
        t.fov[1] = {-0.6f, 1.0f, 0.8f, -0.8f};
        const float orange[4] = {1.0f, 0.5f, 0.0f, 1.0f}, azure[4] = {0.0f, 0.5f, 1.0f, 1.0f};
        const D3D11_RECT leftHalf{0, 0, kWidth / 2, kHeight}, rightHalf{kWidth / 2, 0, kWidth, kHeight};
        fvr::graphics::D3D11PairConsumer worldConsumer;
        fvr::graphics::TextureDescriptor worldOpened{};
        std::array<ID3D11Texture2D*, 2> eyeOut{nullptr, nullptr};
        unsigned pairs = 0, cropsRight = 0;
        bool sawWorldLane = false;
        auto pixel = [&](ID3D11Texture2D* eye, float x, float y, unsigned char out[4]) {
            D3D11_TEXTURE2D_DESC td{};
            eye->GetDesc(&td);
            td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; td.MiscFlags = 0;
            ID3D11Texture2D* stage = nullptr;
            if (FAILED(dev->CreateTexture2D(&td, nullptr, &stage))) return;
            ctx->CopyResource(stage, eye);
            D3D11_MAPPED_SUBRESOURCE m{};
            if (SUCCEEDED(ctx->Map(stage, 0, D3D11_MAP_READ, 0, &m))) {
                const unsigned px = static_cast<unsigned>(x * td.Width), py = static_cast<unsigned>(y * td.Height);
                std::memcpy(out, static_cast<const unsigned char*>(m.pData) + py * m.RowPitch + px * 4, 4);
                ctx->Unmap(stage, 0);
            }
            stage->Release();
        };
        auto isOrange = [](const unsigned char* p) { return p[0] > 200 && p[2] < 50; };
        auto isAzure = [](const unsigned char* p) { return p[0] < 50 && p[2] > 200; };
        for (int frame = 0; frame < 600 && (pairs < 8 || frame < 90); ++frame) {
            ctx->OMSetRenderTargets(1, &rtv, nullptr);
            ctx->ClearRenderTargetView(rtv, olive);
            if (ctx1) {
                ctx1->ClearView(rtv, orange, &leftHalf, 1);
                ctx1->ClearView(rtv, azure, &rightHalf, 1);
            }
            sc->Present(0, 0);
            hostFrame(150 + frame);
            ++t.generation;
            t.predictedNs = 2000000000LL + static_cast<std::int64_t>(frame) * 11000000LL;
            fvr::runtime::TrackingFrame rendered{};
            fvr::graphics::TextureDescriptor d{};
            fvr::graphics::PairTicket ticket{};
            if (xrFrames.TryGetCompletedPair(req, t, rendered, d, ticket)) {
                if (std::memcmp(&worldOpened, &d, sizeof(d)) != 0) {
                    worldOpened = {};
                    worldConsumer.Reset();
                    for (auto& e : eyeOut) if (e) { e->Release(); e = nullptr; }
                    if (worldConsumer.Open(dev, d) == fvr::graphics::TransferResult::Ok) {
                        D3D11_TEXTURE2D_DESC td{};
                        td.Width = d.width; td.Height = d.height; td.Format = DXGI_FORMAT(d.format);
                        td.MipLevels = td.ArraySize = td.SampleDesc.Count = 1;
                        bool ok = true;
                        for (auto& e : eyeOut) ok = ok && SUCCEEDED(dev->CreateTexture2D(&td, nullptr, &e));
                        if (ok) worldOpened = d;
                    }
                }
                const bool copied = fvr::graphics::Valid(worldOpened) &&
                                    worldConsumer.Copy(ticket, {{{eyeOut[0], 0}, {eyeOut[1], 0}}}) == fvr::graphics::TransferResult::Ok;
                xrFrames.PairConsumed(ticket, copied);
                if (copied) {
                    ++pairs;
                    unsigned char l0[4]{}, l1[4]{}, r0[4]{}, r1[4]{};
                    pixel(eyeOut[0], 0.30f, 0.5f, l0);
                    pixel(eyeOut[0], 0.90f, 0.5f, l1);
                    pixel(eyeOut[1], 0.10f, 0.5f, r0);
                    pixel(eyeOut[1], 0.70f, 0.5f, r1);
                    if (isOrange(l0) && isAzure(l1) && isOrange(r0) && isAzure(r1) && d.width == req.width &&
                        d.height == req.height && rendered.generation != 0)
                        ++cropsRight;
                    else if (pairs == 1)
                        std::printf("world eye pixels L %u,%u,%u / %u,%u,%u  R %u,%u,%u / %u,%u,%u (%ux%u)\n", l0[0], l0[1], l0[2],
                                    l1[0], l1[1], l1[2], r0[0], r0[1], r0[2], r1[0], r1[1], r1[2], d.width, d.height);
                }
            }
            pump();
            Sleep(5);
            sawWorldLane = sawWorldLane || Status().find("lane=world") != std::string::npos;
        }
        s = Status();
        Check(pairs >= 5, "world lane: the XR host received eye pairs through the frame bridge");
        Check(cropsRight >= 1, "world lane: each eye carries its own asymmetric crop of the game frame");
        Check(sawWorldLane, "world lane: the menu lane switched to gameplay (no panel)");
        if (pairs < 5 || !cropsRight) std::printf("%s\n", s.c_str());
        for (auto& e : eyeOut) if (e) e->Release();
        worldConsumer.Reset();
        if (ctx1) ctx1->Release();
        SetEnvironmentVariableW(L"FVR_BF4_WORLD_UNTRACKED", nullptr);
        for (int frame = 0; frame < 60; ++frame) { // back to the theater panel
            ctx->OMSetRenderTargets(1, &rtv, nullptr);
            ctx->ClearRenderTargetView(rtv, olive);
            sc->Present(0, 0);
            hostFrame(800 + frame);
            pump();
            Sleep(10);
        }
        Check(Status().find("lane=theater") != std::string::npos, "world lane: back on the theater panel when not wanted");
    }

    // 4c. Hang diagnostics (developer sessions): no Present for > 4 s makes the adapter's
    // watchdog log where every thread is, without deadlocking the process.
    {
        Sleep(5500);
        std::string log;
        if (std::FILE* f = _wfopen((dir + L"\\bf4-adapter.log").c_str(), L"rb")) {
            char buf[4096];
            for (size_t n; (n = std::fread(buf, 1, sizeof(buf), f)) > 0;) log.append(buf, n);
            std::fclose(f);
        }
        const auto at = log.find("thread stacks follow");
        Check(at != std::string::npos, "hang watchdog: a 4 s stall is reported");
        Check(at != std::string::npos && log.find("  thread ", at) != std::string::npos &&
                  (log.find("Bf4EmuHost.exe+0x", at) != std::string::npos || log.find("ntdll.dll+0x", at) != std::string::npos),
              "hang watchdog: thread stacks resolve to module+offset");
    }

    // 5. Engine stereo, BF4-style: our nvapi64.dll answers like a 3D Vision driver.
    HMODULE nv = LoadLibraryW((dir + L"\\nvapi64.dll").c_str());
    Check(nv != nullptr, "nvapi64.dll shim loaded from the game folder");
    using QueryFn = void*(__cdecl*)(unsigned);
    auto qi = nv ? reinterpret_cast<QueryFn>(GetProcAddress(nv, "nvapi_QueryInterface")) : nullptr;
    using InitFn = int(__cdecl*)();
    using ModeFn = int(__cdecl*)(int);
    using CreateFn = int(__cdecl*)(void*, void**);
    using IsActFn = int(__cdecl*)(void*, unsigned char*);
    using EyeFn = int(__cdecl*)(void*, int);
    void* stereoHandle = nullptr;
    IsActFn isActivated = nullptr;
    EyeFn setEye = nullptr;
    if (qi) {
        reinterpret_cast<InitFn>(qi(0x0150e828))();                          // NvAPI_Initialize
        reinterpret_cast<ModeFn>(qi(0x5e8f0bec))(2);                         // SetDriverMode(DIRECT)
        reinterpret_cast<CreateFn>(qi(0xac7e37f4))(dev, &stereoHandle);      // CreateHandleFromIUnknown
        isActivated = reinterpret_cast<IsActFn>(qi(0x1fb0bc30));
        setEye = reinterpret_cast<EyeFn>(qi(0x96eea9f8));
    }
    const float red[4] = {1, 0, 0, 1}, blue[4] = {0, 0, 1, 1};
    auto stereoFrame = [&] {
        unsigned char on = 0;
        if (isActivated) isActivated(stereoHandle, &on);
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        if (on && setEye) {
            setEye(stereoHandle, 2); ctx->ClearRenderTargetView(rtv, red);  // NVAPI_STEREO_EYE_LEFT
            setEye(stereoHandle, 1); ctx->ClearRenderTargetView(rtv, blue); // NVAPI_STEREO_EYE_RIGHT
        } else {
            ctx->ClearRenderTargetView(rtv, olive);
        }
        sc->Present(0, 0);
        pump();
    };
    // The tracker judges the dominant pattern over the whole session, so the stereo phase
    // runs until it outweighs the mono phases before it (theater + world lane).
    for (int frame = 0; frame < 1500 && (frame < 200 || Status().find("stereo=3D theater") == std::string::npos); ++frame) {
        stereoFrame();
        Sleep(2);
    }
    s = Status();
    Check(s.find("stereo=3D theater") != std::string::npos, "engine stereo pairs shown as 3D theater");
    Check(s.find("stereo_pattern=sequential-LR") != std::string::npos, "eye passes recognised as sequential L then R");
    std::string log;
    if (std::FILE* f = _wfopen((dir + L"\\bf4-adapter.log").c_str(), L"rb")) {
        char buf[4096];
        for (size_t n; (n = std::fread(buf, 1, sizeof(buf), f)) > 0;) log.append(buf, n);
        std::fclose(f);
    }
    float lr = 0, lg = 0, lb = 0, rr = 0, rg = 0, rb = 0;
    const auto at = log.find("stereo snapshot written left_mean=");
    if (at != std::string::npos)
        std::sscanf(log.c_str() + at, "stereo snapshot written left_mean=%f,%f,%f right_mean=%f,%f,%f", &lr, &lg, &lb, &rr, &rg, &rb);
    Check(lr > 0.9f && lb < 0.1f && rb > 0.9f && rr < 0.1f, "left eye captured red, right eye captured blue");
    Check(GetFileAttributesW((dir + L"\\stereo-probe-sbs.bmp").c_str()) != INVALID_FILE_ATTRIBUTES, "side-by-side snapshot written");

    // 5b. Weapon-firing census (developer research, read-only): a limited-ammo pair sharing
    // one weapon data (client/server shape) and an unlimited-ammo AI copy, planted on the
    // heap with the analysed layout; the real background scan must report the pair.
    {
        auto alloc = [](std::size_t n) {
            auto* p = static_cast<std::uint8_t*>(_aligned_malloc(n, 16));
            std::memset(p, 0, n);
            return p;
        };
        auto put = [](std::uint8_t* p, std::size_t off, auto v) { std::memcpy(p + off, &v, sizeof(v)); };
        std::uint8_t* weaponData = alloc(0x60);
        std::uint8_t* firingData = alloc(0x250);
        put(weaponData, 0x10, reinterpret_cast<std::uint64_t>(firingData));
        put(firingData, 0x1DC, 2.4f);
        put(firingData, 0x1EC, 0.6f);
        put(firingData, 0x204, std::int32_t{2});
        put(firingData, 0x208, std::int32_t{2});
        put(firingData, 0x220, std::int32_t{31});
        put(firingData, 0x224, std::int32_t{4});
        std::uint8_t* objs[3];
        for (int i = 0; i < 3; ++i) {
            objs[i] = alloc(0x210);
            put(objs[i], 0x128, reinterpret_cast<std::uint64_t>(weaponData));
            put(objs[i], 0x130, reinterpret_cast<std::uint64_t>(firingData) + 0x220);
            put(objs[i], 0x148, std::int32_t{3});
            put(objs[i], 0x150, std::int32_t{3});
            put(objs[i], 0x17C, 1.0f);
            put(objs[i], 0x188, std::int32_t{-1});
            put(objs[i], 0x1A0, std::int32_t{i == 2 ? 12 : 27});
            put(objs[i], 0x1A4, std::int32_t{i == 2 ? 999 : 93});
            put(objs[i], 0x1C0, std::int32_t{-1});
        }
        objs[2][0x208] = 0x10; // AI copy: unlimited ammo
        // Owners as the census expects them: the client weapon holds its firing object at
        // +0x8, the server weapon at +0xA0, each starting with its class's vtable (only the
        // value is compared). A real polymorphic object holds the client weapon at +0x40.
        const auto exeBase = reinterpret_cast<std::uint64_t>(GetModuleHandleW(nullptr));
        std::uint8_t* clientWeapon = alloc(0x100);
        std::uint8_t* serverWeapon = alloc(0x200);
        put(clientWeapon, 0, exeBase + 0x1D467A0); // client weapon class A
        put(clientWeapon, 0x8, reinterpret_cast<std::uint64_t>(objs[0]));
        put(serverWeapon, 0, exeBase + 0x1D64680);
        put(serverWeapon, 0xA0, reinterpret_cast<std::uint64_t>(objs[1]));
        struct Holder {
            virtual ~Holder() = default;
            std::uint8_t pad[0x38] = {};
            void* weapon = nullptr;
        };
        static_assert(offsetof(Holder, weapon) == 0x40);
        // The client weapon entity: its vtable first, class A at +0xA8 (active at +0x98).
        std::uint8_t* weaponEntity = alloc(0x100);
        put(weaponEntity, 0, exeBase + 0x1D46540);
        put(weaponEntity, 0x98, reinterpret_cast<std::uint64_t>(clientWeapon));
        put(weaponEntity, 0xA8, reinterpret_cast<std::uint64_t>(clientWeapon));
        auto* holder = new Holder;
        holder->weapon = weaponEntity; // the next owner up holds the entity at +0x40
        CreateDirectoryW((dir + L"\\research-emu").c_str(), nullptr);
        std::ofstream(dir + L"\\research-emu\\cmd.txt") << "1 census\n";
        std::string census;
        for (int frame = 0; frame < 2000 && census.find("firing census (research): done") == std::string::npos; ++frame) {
            stereoFrame();
            Sleep(5);
            if (frame % 50 == 0) {
                census.clear();
                if (std::FILE* f = _wfopen((dir + L"\\bf4-adapter.log").c_str(), L"rb")) {
                    char buf[4096];
                    for (size_t n; (n = std::fread(buf, 1, sizeof(buf), f)) > 0;) census.append(buf, n);
                    std::fclose(f);
                }
            }
        }
        char pair[96], ai[48];
        // The pair is reported in memory order, so either object may come first.
        const auto first = std::min(reinterpret_cast<std::uintptr_t>(objs[0]), reinterpret_cast<std::uintptr_t>(objs[1]));
        std::snprintf(pair, sizeof(pair), "pair candidate: %llx", static_cast<unsigned long long>(first));
        std::snprintf(ai, sizeof(ai), "firing %llx", static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(objs[2])));
        const auto aiLine = census.find(ai);
        Check(census.find("firing census (research)") != std::string::npos && census.find(pair) != std::string::npos,
              "firing census finds the planted client/server-shaped pair");
        Check(aiLine != std::string::npos && census.find(" unlimited", aiLine) < census.find('\n', aiLine),
              "firing census marks the unlimited-ammo copy");
        char clientLine[64], serverLine[64], holderLine[96];
        std::snprintf(clientLine, sizeof(clientLine), "-> client weapon A %llx", static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(clientWeapon)));
        std::snprintf(serverLine, sizeof(serverLine), "-> server weapon %llx", static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(serverWeapon)));
        // The holder line for the pointer at holder+0x40, whose nearest vtable is the holder itself.
        std::snprintf(holderLine, sizeof(holderLine), "held at %llx: nearest vtable %llx",
                      static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(holder) + 0x40),
                      static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(holder)));
        const auto holderAt = census.find(holderLine);
        Check(census.find(clientLine) != std::string::npos && census.find(serverLine) != std::string::npos,
              "firing census tells the client weapon from the server weapon");
        Check(holderAt != std::string::npos && census.find("at -40", holderAt) < census.find('\n', holderAt),
              "firing census follows the weapon entity to the object holding it");
        char entityLine[96];
        std::snprintf(entityLine, sizeof(entityLine), "client weapon entity %llx holds client weapon A %llx; active (+0x98) = %llx",
                      static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(weaponEntity)),
                      static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(clientWeapon)),
                      static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(clientWeapon)));
        Check(census.find(entityLine) != std::string::npos, "firing census finds the client weapon entity and its active class");
        delete holder;
        _aligned_free(weaponEntity);
        _aligned_free(clientWeapon);
        _aligned_free(serverWeapon);
        for (auto* o : objs) _aligned_free(o);
        _aligned_free(firingData);
        _aligned_free(weaponData);
    }

    // 5b. The world lane's eye resampler on this device: plain crops, then the aim reticle.
    {
        const char* why = "";
        const bool ok = CheckEyeResamplerReticle(dev, why);
        if (!ok) std::printf("resampler check: %s\n", why);
        Check(ok, "eye resampler shader draws the aim reticle only where the gun aims");
    }
    {
        const char* why = "";
        const bool ok = CheckDepthCapture(dev, why);
        if (!ok) std::printf("depth capture check: %s\n", why);
        Check(ok, "depth capture finds the main depth and keeps the world from before the first-person clear");
    }
    {
        const char* why = "";
        const bool ok = CheckShaderCensus(dev, why);
        if (!ok) std::printf("shader census check: %s\n", why);
        Check(ok, "shader census names a vertex shader's constant buffer and variables, and stops when removed");
    }
    {
        const char* why = "";
        const bool ok = CheckDrawProbe(dev, why);
        if (!ok) std::printf("draw probe check: %s\n", why);
        Check(ok, "draw probe records one frame's draw with its layout, camera position and bone palette");
    }
    {
        const char* why = "";
        const bool ok = CheckFirstPersonView(dev, why);
        if (!ok) std::printf("first-person view check: %s\n", why);
        Check(ok, "first-person view pass gets the render's projection and a moved view; other binds pass through");
    }
    {
        const char* why = "";
        const bool ok = CheckFirstPersonArms(dev, why);
        if (!ok) std::printf("first-person arms check: %s\n", why);
        Check(ok, "arms from the body: a palette bound in the pass is rebound with the shoulders at the body, hands on the gun");
    }
    {
        const char* why = "";
        const bool ok = CheckHudSplit(dev, why);
        if (!ok) std::printf("hud split check: %s\n", why);
        Check(ok, "HUD split copies the scene before the first HUD draw and puts back the centre box only");
    }
    {
        const char* why = "";
        const bool ok = CheckControllerScan(why);
        if (!ok) std::printf("controller scan check: %s\n", why);
        Check(ok, "BF4's controller scan: DirectInput's pads passed through, the stand-in pad ahead of them, its check answered without WMI");
    }

    // 6. Multiplayer evidence: a PunkBuster client module appears.
    LoadLibraryW((dir + L"\\pbcl.dll").c_str());
    for (int frame = 0; frame < 300; ++frame) { // > 2 s so the module scan runs
        stereoFrame();
        Sleep(10);
    }
    s = Status();
    Check(s.find("detached=1") != std::string::npos, "adapter locked itself off on PunkBuster evidence");
    Check(s.find("GameMenuPointer: disabled (locked") != std::string::npos, "every feature disabled after lockdown");
    unsigned char stillOn = 1;
    if (isActivated) isActivated(stereoHandle, &stillOn);
    Check(stillOn == 0, "shim reports stereo off after lockdown (engine back to one pass)");

    std::printf("---- adapter status ----\n%s\n", s.c_str());
    for (auto& p : panel) if (p) p->Release();
    panelConsumer.Reset();
    xrMenu.Close();
    rtv->Release(); sc->Release(); ctx->Release(); dev->Release();
    UnmapViewOfFile(view);
    CloseHandle(section);
    DestroyWindow(hwnd);
    return g_fail == 0 ? 0 : 1;
}
