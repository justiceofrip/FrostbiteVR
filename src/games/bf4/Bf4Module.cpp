// FrostbiteVR.Bf4.dll - Windows glue for the BF4 adapter. Loaded by the session proxy.
//
// All per-frame decisions live in Bf4AdapterCore (platform-neutral, emulated-tested).
// This file only provides the Win32/D3D11/MinHook side of IAdapterPlatform:
//   * bounded log on the install drive
//   * read-only .text scan -> BindingTable, PE identity -> BuildIdentity
//   * IDXGISwapChain::Present/ResizeBuffers hooks, swapchain adoption
//   * PunkBuster module scan, pointer sink, frame sink, hook teardown
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <psapi.h>

#include "Bf4AdapterCore.h"
#include "Bf4GpuVendor.h"
#include "Bf4MenuBridge.h"
#include "Bf4ResearchControl.h"
#include "Bf4XInputSink.h"
#include "Bf4CameraProbe.h"
#include "Bf4FiringCensus.h"
#include "Bf4ViewOverride.h"
#include "Bf4SessionSignals.h"
#include "Bf4EngineStereo.h"
#include "Bf4EyeLoopOverride.h"
#include "Bf4Diagnostics.h"
#include "Bf4WorldView.h"
#include "Bf4DepthCapture.h"
#include "Bf4ControllerScan.h"
#include "Bf4DrawProbe.h"
#include "Bf4FirstPersonView.h"
#include "Bf4HudSplit.h"
#include "Bf4ShaderCensus.h"
#include "fvr/graphics/GdiPanelRaster.h"
#include "stereo/SideBySide.h"

#include <MinHook.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace fvr::bf4 {
namespace {

// ---- Bounded log (one rotation, 1 MiB) ---------------------------------------------------
class FileLog {
public:
    void Open(const std::string& dir) {
        if (dir.empty()) return;
        path_ = dir + "\\bf4-adapter.log";
        WIN32_FILE_ATTRIBUTE_DATA attr{};
        if (GetFileAttributesExA(path_.c_str(), GetFileExInfoStandard, &attr) && attr.nFileSizeLow > kMaxBytes)
            MoveFileExA(path_.c_str(), (path_ + ".1").c_str(), MOVEFILE_REPLACE_EXISTING);
        file_ = std::fopen(path_.c_str(), "ab");
    }
    void Write(const char* fmt, ...) {
        if (!file_) return;
        std::lock_guard<std::mutex> lock(mutex_);
        SYSTEMTIME t;
        GetLocalTime(&t);
        std::fprintf(file_, "%02u:%02u:%02u.%03u ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
        va_list args;
        va_start(args, fmt);
        std::vfprintf(file_, fmt, args);
        va_end(args);
        std::fputc('\n', file_);
        std::fflush(file_);
    }

private:
    static constexpr DWORD kMaxBytes = 1u << 20;
    std::string path_;
    std::FILE* file_ = nullptr;
    std::mutex mutex_;
};

// ---- IAdapterPlatform for the real game process ----------------------------------------
// Static layout facts (docs/BF4-CAMERA-RE.md) hold for this one build; any other hash
// skips the camera probe and the view override.
constexpr char kAnalyzedBuildSha256[] = "9d9d1a3fca6ffd4c34a543b3fd7b049336ddbd43c0e6c5eee742e51431a6c2aa";
bool ReadChecked(std::uintptr_t address, void* out, std::size_t size); // VirtualQuery-checked copy
std::string ReadCStringChecked(std::uintptr_t address, std::size_t maxLength = 160);
// Render device (docs/BF4-STEREO.md): global and class, analysed build only.
constexpr std::uintptr_t kRenderDeviceGlobalRva = 0x2738080;
constexpr std::uintptr_t kRenderDeviceClassRva = 0x1DD1870;
// Two-eye loop class (vtable) and its slot 2 (docs/BF4-STEREO.md, Bf4EyeLoopOverride.h).
constexpr std::uintptr_t kEyeLoopClassRva = 0x1D24258;
constexpr std::uintptr_t kEyeLoopEntryRva = 0x8BE430;
// R5 single-player signals (docs/BF4-SESSION-RE.md), analysed build only.
constexpr std::uintptr_t kClientGameContextRva = 0x2670D80;
constexpr std::uintptr_t kLevelInfoRva = 0x27A1918;

bool InGameImage(std::uintptr_t address, std::size_t size) {
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    return address >= base && address + size <= base + nt->OptionalHeader.SizeOfImage;
}

class Win32Platform final : public IAdapterPlatform {
public:
    explicit Win32Platform(FileLog& log) : log_(log) {}

    Win32PointerSink pointer;
    XInputSink padSink;
    std::unique_ptr<IFrameSink> frames;
    IDXGISwapChain* swapchain = nullptr; // adopted game swapchain, not owned
    HWND window = nullptr;

    // ---- engine stereo -------------------------------------------------------------
    std::string logDir;
    StereoSetting stereoSetting = StereoSetting::Off;
    GpuVendorResult gpu;
    AdapterCore* core = nullptr;
    std::mutex* coreMutex = nullptr;
    ID3D11Texture2D* eyes[2] = {nullptr, nullptr};

    using EyeCallback = void(__cdecl*)(int, void*);
    using BindFn = int(__cdecl*)(EyeCallback, void*);
    using SetSuppressedFn = void(__cdecl*)(int);
    using StateFn = int(__cdecl*)(int*, int*, unsigned long long*);
    using ReportFn = int(__cdecl*)(char*, int);
    struct {
        SetSuppressedFn setSuppressed = nullptr;
        StateFn state = nullptr;
        ReportFn report = nullptr;
    } shim;
    bool shimBound = false, shimForeignLogged = false, suppressPending = false, splitMsaaLogged = false;
    StereoRoute stereoRoute = StereoRoute::Split;
    std::string exeSha256;
    bool developer = false;
    bool worldSnapshotTaken = false;
    SessionSignalFilter sessionSignals_;
    SessionObservation lastObservation_{}; // its levelName points into sessionSignals_
    ULONGLONG lastObservationMs_ = 0;
    bool haveObservation_ = false;
    ViewOverride viewOverride;
    std::uint64_t lastSubmits_ = 0;
    ULONGLONG lastSubmitMs_ = 0;

    // Game memory for the engine stereo controller: VirtualQuery-checked, writes only to
    // committed read-write pages.
    struct Win32Memory final : runtime::IMemory {
        bool Read(std::uint64_t address, void* out, std::size_t size) override {
            return ReadChecked(static_cast<std::uintptr_t>(address), out, size);
        }
        bool Write(std::uint64_t address, const void* in, std::size_t size) override {
            MEMORY_BASIC_INFORMATION mbi{};
            if (address < 0x10000 || !VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) ||
                mbi.State != MEM_COMMIT || !(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) ||
                (mbi.Protect & PAGE_GUARD) ||
                address + size > reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize)
                return false;
            std::memcpy(reinterpret_cast<void*>(address), in, size);
            return true;
        }
    } gameMemory;
    EngineStereo engineStereo{gameMemory};
    EngineStereo::Status lastStereoStatus = EngineStereo::Status::Off;
    bool lastSplit = false;
    std::uint32_t splitStableFrames = 0;
    ULONGLONG worldLaneSinceMs = 0;
    EngineStereoReading lastReading{};
    float lastTanX = 0;
    float lastViewAspect = 0;
    EyeLoopOverride eyeLoop;
    // Depth stereo (opt-in): the frame's scene depth, found through the game's own depth clears.
    DepthCapture depthCapture;
    FirstPersonView firstPersonView; // gun in the hand (Bf4FirstPersonView.h)
    HudSplit hudSplit;               // BF4's crosshair out of the head-tracked view (Bf4HudSplit.h)
    bool hudInstallTried = false;
    std::uint64_t presentCount = 0, gunInHandFrame = 0, laneFrame = 0;
    bool firstPersonOn = false, firstPersonInstallTried = false;
    bool depthInstallTried = false, depthSnapshotPending = false, depthSeen = false;
    bool eyeLoopRefused = false, stereoCrashGuard = false, stereoMarker = false;
    // Background search for the two-eye loop object (read-only): 0 idle, 1 running, 2 done.
    std::atomic<int> eyeLoopScan{0};
    std::atomic<std::uintptr_t> eyeLoopFound{0};
    std::atomic<int> eyeLoopCandidates{0};
    std::atomic<std::uint64_t> eyeLoopScannedMb{0};

    // The renderer's copy of view 0 carries the aspect the view setup applied.
    // The main view in the renderer's per-frame view buffer ([renderer+0x60] entry 0):
    // camera-to-world at +0x40 (read-only; works without the view override installed).
    bool ReadGameView(AimAngle& aim, float& nearZ, float& farZ) {
        if (!core) return false;
        const auto* global = core->Bindings().Find(names::kGameRenderer);
        std::uintptr_t object = 0, views = 0;
        if (!global || global->status != frostbite::ScanStatus::Ok || !ReadChecked(global->address, &object, 8) || !object ||
            !ReadChecked(object + 0x60, &views, 8) || !views)
            return false;
        std::uint8_t rows[64];
        if (!ReadChecked(views + kViewCopyOffset, rows, sizeof(rows)) || !ReadChecked(views + kViewNearOffset, &nearZ, 4) ||
            !ReadChecked(views + kViewFarOffset, &farZ, 4))
            return false;
        aim = CameraAim(ReadViewTransform(rows).forward);
        return std::isfinite(aim.yaw) && std::isfinite(aim.pitch);
    }

    float ReadViewAspect() {
        void* renderer = viewOverride.Object();
        std::uintptr_t views = 0;
        float aspect = 0;
        if (!renderer || !ReadChecked(reinterpret_cast<std::uintptr_t>(renderer) + 0x60, &views, 8) || !views ||
            !ReadChecked(views + kViewAspectOffset, &aspect, 4))
            return 0;
        return aspect;
    }

    void StartEyeLoopScan();

    // Split stereo needs room for two eyes in the eye loop's array (Bf4EyeLoopOverride.h).
    bool EnsureEyeLoop() {
        if (eyeLoop.Installed()) return true;
        if (eyeLoopRefused) return false;
        if (eyeLoopScan.load() == 0) StartEyeLoopScan();
        if (eyeLoopScan.load() != 2) return false;
        const int candidates = eyeLoopCandidates.load();
        const auto object = eyeLoopFound.load();
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        // Still the object the scan found (class, inline array) right before the one write.
        std::uintptr_t check[3] = {};
        const bool still = object && ReadChecked(object, &check[0], 8) && check[0] == base + kEyeLoopClassRva &&
                           ReadChecked(object + kEyeArrayBegin, &check[1], 8) && check[1] == object + kEyeArrayInline &&
                           ReadChecked(object + kEyeArrayCapacity, &check[2], 8) &&
                           check[2] == object + kEyeArrayInline + kEyeEntryBytes;
        if (candidates != 1 || !still) {
            log_.Write("eye loop: %d candidate objects in %lluMB; 3D stays off", candidates,
                       static_cast<unsigned long long>(eyeLoopScannedMb.load()));
            eyeLoopRefused = true;
            return false;
        }
        const auto r = eyeLoop.Install(reinterpret_cast<void*>(object), base + kEyeLoopEntryRva);
        log_.Write("eye loop: object %p, override %s", reinterpret_cast<void*>(object),
                   r == EyeLoopOverride::InstallResult::Ok ? "installed (room for two eyes)" :
                   r == EyeLoopOverride::InstallResult::UnexpectedSlot ? "refused (slot 2 is not the analysed entry)" :
                   r == EyeLoopOverride::InstallResult::NotInlineArray ? "refused (array not in its inline storage)" : "refused");
        if (r != EyeLoopOverride::InstallResult::Ok) eyeLoopRefused = true;
        return r == EyeLoopOverride::InstallResult::Ok;
    }

    std::string DescribeEngineStereo() const {
        static const char* names[] = {"off", "on", "refused", "handed-back"};
        char line[400];
        const auto es = eyeLoop.GetStats();
        std::snprintf(line, sizeof(line),
                      "engine_stereo=%s%s split=%d separation=%.5f convergence=%.3f half_eye=%.4f view_aspect=%.3f adjustments=%llu "
                      "eye_loop=%s calls=%llu widened=%llu restored=%llu foreign=%llu max_entries=%llu",
                      names[static_cast<int>(engineStereo.State())], stereoCrashGuard ? "(crash guard)" : "",
                      lastReading.SplitActive() ? 1 : 0, lastReading.separation, lastReading.convergence,
                      lastReading.HalfEyeDistance(lastTanX), lastViewAspect,
                      static_cast<unsigned long long>(engineStereo.Adjustments()),
                      eyeLoop.Installed() ? "on" : (eyeLoopRefused ? "refused" : "off"), static_cast<unsigned long long>(es.calls),
                      static_cast<unsigned long long>(es.widened), static_cast<unsigned long long>(es.restored),
                      static_cast<unsigned long long>(es.foreign), static_cast<unsigned long long>(es.maxEntries));
        return line;
    }

    // World-lane stereo: BF4's own split-screen two-eye render, on only while the headset
    // shows the world lane (a theater panel must never be side by side).
    void UpdateEngineStereo(FrameMeta& m, const FrameMeta& meta, float tanX) {
        // 3D starts after 10 s of working head tracking, so a first session shows each step.
        const bool lane = m.world && frames && frames->WorldLane();
        const ULONGLONG now = GetTickCount64();
        if (!lane) worldLaneSinceMs = 0;
        else if (!worldLaneSinceMs) worldLaneSinceMs = now;
        if (developer && lane && !worldSnapshotTaken && now - worldLaneSinceMs >= 5000) {
            worldSnapshotTaken = true;
            frames->RequestSnapshot(logDir);
            depthSnapshotPending = depthCapture.Installed();
            log_.Write("snapshot requested (5 s of head tracking): vr-snapshot-frame.bmp / vr-snapshot-eyes.bmp");
        }
        const bool wanted = lane && now - worldLaneSinceMs >= 10000 && meta.worldStereo &&
                            exeSha256 == kAnalyzedBuildSha256 && meta.halfEyeDistance > 0 && tanX > 0 && EnsureEyeLoop();
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        if (wanted && engineStereo.State() == EngineStereo::Status::Off) {
            // A session that dies with 3D on leaves this marker; the next one keeps 3D off.
            if (std::FILE* f = std::fopen((logDir + "\\stereo-armed.txt").c_str(), "wb")) {
                std::fputs("3D was on when this session ended (crash?)\n", f);
                std::fclose(f);
                stereoMarker = true;
            }
            engineStereo.Enable({base + kRenderDeviceGlobalRva, base + kRenderDeviceClassRva}, meta.halfEyeDistance, tanX);
        } else if (!wanted && engineStereo.State() == EngineStereo::Status::On) {
            engineStereo.Disable();
        }
        if (stereoMarker && engineStereo.State() != EngineStereo::Status::On) {
            std::remove((logDir + "\\stereo-armed.txt").c_str());
            stereoMarker = false;
        }
        Diagnostics::ArmFaultLogging(engineStereo.State() == EngineStereo::Status::On);
        if (engineStereo.State() == EngineStereo::Status::On) engineStereo.Update(meta.halfEyeDistance, tanX);
        const EngineStereoReading r = engineStereo.State() == EngineStereo::Status::On ? engineStereo.Read() : EngineStereoReading{};
        if (engineStereo.State() != lastStereoStatus) {
            static const char* names[] = {"off", "on", "refused (device or settings not as analysed)", "handed back (game changed a value)"};
            log_.Write("engine stereo: %s (half eye distance %.4f, tanX %.3f)", names[static_cast<int>(engineStereo.State())],
                       meta.halfEyeDistance, tanX);
            lastStereoStatus = engineStereo.State();
        }
        const bool split = r.SplitActive();
        splitStableFrames = split == lastSplit ? splitStableFrames + 1 : 0;
        if (split != lastSplit) {
            const auto es = eyeLoop.GetStats();
            log_.Write("engine stereo render: %s (mode=%u on=%d separation=%.5f convergence=%.3f half_eye=%.4f "
                       "view_aspect=%.3f adjustments=%llu eye_loop calls=%llu widened=%llu max_entries=%llu)",
                       split ? "side by side" : "single view", r.mode, r.on ? 1 : 0, r.separation, r.convergence,
                       r.HalfEyeDistance(tanX), lastViewAspect, static_cast<unsigned long long>(engineStereo.Adjustments()),
                       static_cast<unsigned long long>(es.calls), static_cast<unsigned long long>(es.widened),
                       static_cast<unsigned long long>(es.maxEntries));
        }
        lastSplit = split;
        lastReading = r;
        lastTanX = tanX;
        m.stereoSplit = split;
        m.stereoSeparation = r.separation;
        m.stereoStableFrames = splitStableFrames;
    }
    bool viewInstallRefused = false;

    static void __cdecl OnEye(int eye, void* context) {
        auto* self = static_cast<Win32Platform*>(context);
        if (!self->core || !self->coreMutex) return;
        std::lock_guard<std::mutex> lock(*self->coreMutex);
        self->core->OnStereoEye(static_cast<stereo::Eye>(eye));
    }

    // Called every Present until bound: the game loads nvapi64.dll on its own schedule.
    void TryBindShim() {
        if (shimBound || stereoSetting == StereoSetting::Off || stereoRoute != StereoRoute::Nvapi) return;
        HMODULE m = GetModuleHandleW(L"nvapi64.dll");
        if (!m) return;
        auto bind = reinterpret_cast<BindFn>(GetProcAddress(m, "FvrNvapiShim_Bind"));
        if (!bind) {
            if (!shimForeignLogged) log_.Write("nvapi64.dll loaded but it is not FrostbiteVR's shim (real driver DLL?)");
            shimForeignLogged = true;
            return;
        }
        shim.setSuppressed = reinterpret_cast<SetSuppressedFn>(GetProcAddress(m, "FvrNvapiShim_SetSuppressed"));
        shim.state = reinterpret_cast<StateFn>(GetProcAddress(m, "FvrNvapiShim_State"));
        shim.report = reinterpret_cast<ReportFn>(GetProcAddress(m, "FvrNvapiShim_Report"));
        bind(&OnEye, this);
        shimBound = true;
        if (suppressPending && shim.setSuppressed) shim.setSuppressed(1);
        log_.Write("nvapi64.dll shim bound; engine eye switches now reach the adapter");
    }

    StereoShimState StereoShim() override {
        StereoShimState s;
        s.bound = shimBound;
        s.gpuVendorId = gpu.realVendorId;
        s.vendorSpoofed = gpu.spoofInstalled;
        if (shimBound && shim.state) {
            int handle = 0, direct = 0;
            unsigned long long switches = 0;
            shim.state(&handle, &direct, &switches);
            s.handleCreated = handle != 0;
            s.directMode = direct != 0;
            s.eyeSwitches = switches;
        }
        return s;
    }

    // Eye texture matching the backbuffer format, `width` wide (full or half for split).
    void EnsureEye(ID3D11Device* device, int eye, const D3D11_TEXTURE2D_DESC& back, UINT width) {
        if (eyes[eye]) {
            D3D11_TEXTURE2D_DESC cur{};
            eyes[eye]->GetDesc(&cur);
            if (cur.Width != width || cur.Height != back.Height || cur.Format != back.Format) {
                eyes[eye]->Release();
                eyes[eye] = nullptr;
            }
        }
        if (!eyes[eye]) {
            D3D11_TEXTURE2D_DESC e = back;
            e.Width = width;
            e.MipLevels = 1;
            e.ArraySize = 1;
            e.SampleDesc.Count = 1;
            e.SampleDesc.Quality = 0;
            e.Usage = D3D11_USAGE_DEFAULT;
            e.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            e.CPUAccessFlags = 0;
            e.MiscFlags = 0;
            device->CreateTexture2D(&e, nullptr, &eyes[eye]);
        }
    }

    bool CaptureSplitPair() override {
        if (!swapchain) return false;
        ID3D11Texture2D* back = nullptr;
        if (FAILED(swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back)))) return false;
        D3D11_TEXTURE2D_DESC d{};
        back->GetDesc(&d);
        bool ok = false;
        if (d.SampleDesc.Count == 1 && d.Width >= 2) {
            ID3D11Device* device = nullptr;
            back->GetDevice(&device);
            ID3D11DeviceContext* ctx = nullptr;
            if (device) device->GetImmediateContext(&ctx);
            const UINT half = d.Width / 2;
            if (ctx) {
                EnsureEye(device, 0, d, half);
                EnsureEye(device, 1, d, half);
                if (eyes[0] && eyes[1]) {
                    const D3D11_BOX left{0, 0, 0, half, d.Height, 1}, right{d.Width - half, 0, 0, d.Width, d.Height, 1};
                    ctx->CopySubresourceRegion(eyes[0], 0, 0, 0, 0, back, 0, &left);
                    ctx->CopySubresourceRegion(eyes[1], 0, 0, 0, 0, back, 0, &right);
                    ok = true;
                }
                ctx->Release();
            }
            if (device) device->Release();
        } else if (!splitMsaaLogged) {
            splitMsaaLogged = true;
            log_.Write("split-screen capture skipped: backbuffer %ux%u samples=%u", d.Width, d.Height, d.SampleDesc.Count);
        }
        back->Release();
        return ok;
    }

    // CPU readback of the current backbuffer (8-bit RGBA/BGRA, no MSAA); `use` runs while mapped.
    bool WithBackbufferPixels(const std::function<void(const stereo::ImageView&)>& use) {
        if (!swapchain) return false;
        ID3D11Texture2D* back = nullptr;
        if (FAILED(swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back)))) return false;
        D3D11_TEXTURE2D_DESC d{};
        back->GetDesc(&d);
        const bool rgba = d.Format == DXGI_FORMAT_R8G8B8A8_UNORM || d.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        const bool bgra = d.Format == DXGI_FORMAT_B8G8R8A8_UNORM || d.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        bool ok = false;
        if ((rgba || bgra) && d.SampleDesc.Count == 1) {
            ID3D11Device* device = nullptr;
            back->GetDevice(&device);
            ID3D11DeviceContext* ctx = nullptr;
            if (device) device->GetImmediateContext(&ctx);
            D3D11_TEXTURE2D_DESC sd = d;
            sd.Usage = D3D11_USAGE_STAGING;
            sd.BindFlags = 0;
            sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            sd.MiscFlags = 0;
            ID3D11Texture2D* staging = nullptr;
            if (ctx && SUCCEEDED(device->CreateTexture2D(&sd, nullptr, &staging))) {
                ctx->CopyResource(staging, back);
                D3D11_MAPPED_SUBRESOURCE m{};
                if (SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m))) {
                    use({static_cast<const std::uint8_t*>(m.pData), static_cast<int>(d.Width), static_cast<int>(d.Height),
                         static_cast<int>(m.RowPitch), rgba});
                    ctx->Unmap(staging, 0);
                    ok = true;
                }
                staging->Release();
            }
            if (ctx) ctx->Release();
            if (device) device->Release();
        }
        back->Release();
        return ok;
    }

    bool AnalyzeBackbufferSplit(stereo::SplitAnalysis& out) override {
        const bool ok = WithBackbufferPixels([&](const stereo::ImageView& full) { out = stereo::AnalyzeSplitFrame(full); });
        if (ok) log_.Write("split check: %s correlation=%.3f textured=%.2f", stereo::SplitVerdictName(out.verdict), out.correlation, out.texture);
        return ok;
    }

    // Research snapshot: the whole backbuffer at half size (the SBS writer re-joins the halves).
    bool WriteBackbufferBmp(const std::string& path) {
        bool written = false;
        WithBackbufferPixels([&](const stereo::ImageView& full) {
            written = stereo::WriteSideBySideBmp(path, stereo::LeftHalf(full), stereo::RightHalf(full), 2);
        });
        return written;
    }
    // ---- FrostbiteVR's own menu panel --------------------------------------------------
    graphics::GdiPanelRaster panelRaster;
    ID3D11Texture2D* panelTexture = nullptr;
    std::uint64_t panelRevision = 0;
    std::uint32_t panelHover = ~0u, panelPressed = ~0u;

    bool PublishPanel(const ui::VrPanel& panel) override {
        if (!swapchain || !frames) return false;
        ID3D11Device* device = nullptr;
        if (FAILED(swapchain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device)))) return false;
        bool ok = true;
        if (!panelTexture) {
            D3D11_TEXTURE2D_DESC d{};
            d.Width = panel.Width();
            d.Height = panel.Height();
            d.MipLevels = d.ArraySize = 1;
            d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            d.SampleDesc.Count = 1;
            d.Usage = D3D11_USAGE_DEFAULT;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            ok = SUCCEEDED(device->CreateTexture2D(&d, nullptr, &panelTexture));
            panelRevision = 0;
        }
        // Re-rasterise only when the widgets or their hover/pressed highlight changed; the
        // laser beam and cursor are drawn by the XR host.
        if (ok && (panel.Revision() != panelRevision || panel.Hover() != panelHover || panel.Pressed() != panelPressed)) {
            ok = panelRaster.Render(panel, /*bgra=*/false);
            if (ok) {
                ID3D11DeviceContext* ctx = nullptr;
                device->GetImmediateContext(&ctx);
                ctx->UpdateSubresource(panelTexture, 0, nullptr, panelRaster.Pixels(), panelRaster.Pitch(), 0);
                ctx->Release();
                panelRevision = panel.Revision();
                panelHover = panel.Hover();
                panelPressed = panel.Pressed();
            }
        }
        device->Release();
        return ok && frames->PublishTexture(panelTexture, panel.Width(), panel.Height());
    }

    bool CaptureEye(int eye) override {
        if (eye < 0 || eye > 1 || !swapchain) return false;
        ID3D11Texture2D* back = nullptr;
        if (FAILED(swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back)))) return false;
        D3D11_TEXTURE2D_DESC d{};
        back->GetDesc(&d);
        ID3D11Device* device = nullptr;
        back->GetDevice(&device);
        bool ok = false;
        if (device) {
            EnsureEye(device, eye, d, d.Width);
            ID3D11DeviceContext* ctx = nullptr;
            device->GetImmediateContext(&ctx);
            if (ctx && eyes[eye]) {
                if (d.SampleDesc.Count > 1) ctx->ResolveSubresource(eyes[eye], 0, back, 0, d.Format);
                else ctx->CopyResource(eyes[eye], back);
                ok = true;
            }
            if (ctx) ctx->Release();
            device->Release();
        }
        back->Release();
        return ok;
    }

    bool WriteStereoSnapshot() override {
        if (!eyes[0] || !eyes[1]) return false;
        D3D11_TEXTURE2D_DESC d{};
        eyes[0]->GetDesc(&d);
        const bool rgba = d.Format == DXGI_FORMAT_R8G8B8A8_UNORM || d.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        const bool bgra = d.Format == DXGI_FORMAT_B8G8R8A8_UNORM || d.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        if (!rgba && !bgra) {
            log_.Write("stereo snapshot skipped: backbuffer format %u is not 8-bit RGBA/BGRA", static_cast<unsigned>(d.Format));
            return false;
        }
        ID3D11Device* device = nullptr;
        eyes[0]->GetDevice(&device);
        ID3D11DeviceContext* ctx = nullptr;
        device->GetImmediateContext(&ctx);
        D3D11_TEXTURE2D_DESC sd = d;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.BindFlags = 0;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ID3D11Texture2D* staging[2] = {nullptr, nullptr};
        D3D11_MAPPED_SUBRESOURCE mapped[2]{};
        bool ok = true;
        for (int i = 0; i < 2 && ok; ++i) {
            ok = SUCCEEDED(device->CreateTexture2D(&sd, nullptr, &staging[i]));
            if (ok) ctx->CopyResource(staging[i], eyes[i]);
        }
        for (int i = 0; i < 2 && ok; ++i) ok = SUCCEEDED(ctx->Map(staging[i], 0, D3D11_MAP_READ, 0, &mapped[i]));
        if (ok) {
            const stereo::ImageView left{static_cast<const std::uint8_t*>(mapped[0].pData), static_cast<int>(d.Width),
                                         static_cast<int>(d.Height), static_cast<int>(mapped[0].RowPitch), rgba};
            const stereo::ImageView right{static_cast<const std::uint8_t*>(mapped[1].pData), static_cast<int>(d.Width),
                                          static_cast<int>(d.Height), static_cast<int>(mapped[1].RowPitch), rgba};
            const auto ml = stereo::Mean(left), mr = stereo::Mean(right);
            ok = stereo::WriteSideBySideBmp(logDir + "\\stereo-probe-sbs.bmp", left, right, d.Width >= 1280 ? 2 : 1);
            log_.Write("stereo snapshot %s left_mean=%.2f,%.2f,%.2f right_mean=%.2f,%.2f,%.2f", ok ? "written" : "failed",
                       ml.r, ml.g, ml.b, mr.r, mr.g, mr.b);
        }
        for (int i = 0; i < 2; ++i) {
            if (staging[i] && mapped[i].pData) ctx->Unmap(staging[i], 0);
            if (staging[i]) staging[i]->Release();
        }
        ctx->Release();
        device->Release();
        return ok;
    }

    void SetStereoSuppressed(bool suppressed) override {
        if (shimBound && shim.setSuppressed) shim.setSuppressed(suppressed ? 1 : 0);
        else suppressPending = suppressed;
    }

    std::string NvapiReport() override {
        if (!shimBound || !shim.report) return GetModuleHandleW(L"nvapi64.dll") ? "nvapi64.dll: loaded, not ours\n"
                                                                              : "nvapi64.dll: never loaded by the game\n";
        std::string buf(8192, '\0');
        buf.resize(static_cast<std::size_t>(shim.report(buf.data(), static_cast<int>(buf.size()))));
        return buf;
    }

    void WriteReport(std::string_view name, std::string_view text) override {
        if (logDir.empty()) return;
        if (std::FILE* f = std::fopen((logDir + "\\" + std::string(name)).c_str(), "wb")) {
            std::fwrite(text.data(), 1, text.size(), f);
            std::fclose(f);
        }
    }

    bool PunkBusterLoaded() override {
        HMODULE modules[1024];
        DWORD needed = 0;
        if (!EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed)) return false;
        const DWORD count = std::min<DWORD>(needed / sizeof(HMODULE), 1024);
        for (DWORD i = 0; i < count; ++i) {
            char name[MAX_PATH]{};
            if (!GetModuleBaseNameA(GetCurrentProcess(), modules[i], name, MAX_PATH)) continue;
            if (_strnicmp(name, "pbcl", 4) == 0 || _strnicmp(name, "pbag", 4) == 0) return true;
        }
        return false;
    }

    // Level/server signals arrive with RE task R5 (ClientGameContext, ClientLevel.Name).
    // Level name [[+0x27A1918]+0x50] and isMultiplayer [[[[+0x2670D80]+0x28]+0x38]+0xA0],
    // filtered as the live sessions require (Bf4SessionSignals.h). Read four times a second.
    SessionObservation ObserveGame() override {
        if (exeSha256 != kAnalyzedBuildSha256) return {};
        const ULONGLONG now = GetTickCount64();
        if (haveObservation_ && now - lastObservationMs_ < 250) return lastObservation_;
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        std::uintptr_t info = 0, name = 0, ctx = 0, level = 0, setup = 0;
        std::uint8_t mp = 0xFF;
        const bool readable = ReadChecked(base + kLevelInfoRva, &info, 8) && info;
        std::string levelName;
        if (readable && ReadChecked(info + 0x50, &name, 8)) levelName = ReadCStringChecked(name);
        if (ReadChecked(base + kClientGameContextRva, &ctx, 8) && ctx && ReadChecked(ctx + 0x28, &level, 8) && level &&
            ReadChecked(level + 0x38, &setup, 8) && setup)
            ReadChecked(setup + 0xA0, &mp, 1);
        lastObservation_ = sessionSignals_.Update(readable, levelName, mp == 0xFF ? -1 : (mp ? 1 : 0), now);
        lastObservationMs_ = now;
        haveObservation_ = true;
        return lastObservation_;
    }

    void ApplyPointer(std::span<const PointerEvent> events) override {
        if (developer)
            for (const auto& e : events)
                if (e.type == PointerEvent::Type::ButtonDown || e.type == PointerEvent::Type::ButtonUp) {
                    RECT c{};
                    GetClientRect(window, &c);
                    log_.Write("laser click %s at %d,%d (client %ldx%ld)", e.type == PointerEvent::Type::ButtonDown ? "down" : "up",
                               e.x, e.y, static_cast<long>(c.right), static_cast<long>(c.bottom));
                }
        pointer.Apply(window, events);
    }
    void ApplyPad(const interaction::PadState& pad, bool active) override { padSink.Apply(pad, active); }

    bool GameAim(AimAngle& aim, std::uint64_t& sample) override {
        if (!viewOverride.Installed()) return false;
        const auto st = viewOverride.GetStats();
        if (!st.gameSamples) return false;
        aim = CameraAim(st.gameBack);
        sample = st.gameSamples;
        return std::isfinite(aim.yaw) && std::isfinite(aim.pitch);
    }

    float GameFovY() override { return viewOverride.Installed() ? viewOverride.GetStats().gameFovY : 0.0f; }

    // Relative mouse look for controller aiming; only into the focused game window (it
    // reads the mouse as raw input).
    void SendMouse(int dx, int dy) override {
        if (!window || GetForegroundWindow() != window) return;
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dx = dx;
        input.mi.dy = dy;
        input.mi.dwFlags = MOUSEEVENTF_MOVE;
        SendInput(1, &input, sizeof(input));
        ++mouseSends;
    }
    std::uint64_t mouseSends = 0;

    // Holster gestures: BF4's own keys (scan codes), pressed only into the focused game
    // window; every key this sent down goes up again on the next change or at detach.
    void SetGameKeys(const std::array<std::uint16_t, 2>& held) override {
        const bool focused = window && GetForegroundWindow() == window;
        for (auto& k : keysDown) {
            if (!k || (focused && (k == held[0] || k == held[1]))) continue;
            SendKey(k, false);
            k = 0;
        }
        if (!focused) return;
        for (const auto want : held) {
            if (!want || keysDown[0] == want || keysDown[1] == want) continue;
            auto& slot = keysDown[0] ? keysDown[1] : keysDown[0];
            if (slot) continue;
            SendKey(want, true);
            slot = want;
            ++keySends;
        }
    }
    static void SendKey(std::uint16_t scan, bool down) {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = scan;
        input.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
        SendInput(1, &input, sizeof(input));
    }
    std::array<std::uint16_t, 2> keysDown{};
    std::uint64_t keySends = 0;

    // Controller pulses through the XR host's haptic policy (it checks the packet, the
    // reference space and the 100 ms window itself).
    void Haptic(unsigned hand, bool strong, std::uint64_t inputGeneration, std::uint64_t space) override {
        if (!frames || !inputGeneration || !space || hand > 1) return;
        LARGE_INTEGER q{}, frequency{};
        if (!QueryPerformanceCounter(&q) || !QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) return;
        const std::int64_t now = q.QuadPart / frequency.QuadPart * 1000000000LL +
                                 (q.QuadPart % frequency.QuadPart) * 1000000000LL / frequency.QuadPart;
        interaction::FeedbackEvent e;
        e.id = ++hapticId;
        e.inputSequence = inputGeneration;
        e.space = space;
        e.observedNs = now;
        e.deadlineNs = now + 80000000LL;
        e.kind = strong ? interaction::FeedbackKind::ReloadApplied : interaction::FeedbackKind::ReloadCapture;
        e.hand = hand;
        if (frames->Haptic(e)) ++hapticsSent;
    }
    std::uint64_t hapticId = 0, hapticsSent = 0;

    bool CurrentWorld(WorldRequest& out) override { return frames && frames->CurrentWorld(out); }

    bool PublishFrame(const FrameMeta& meta) override {
        if (!swapchain || !frames) return false;
        if (meta.eye == 0 || meta.eye == 1) { // engine stereo pair: publish the captured eye
            ID3D11Texture2D* eye = eyes[meta.eye];
            if (!eye) return false;
            ID3D11Device* device = nullptr;
            eye->GetDevice(&device);
            ID3D11DeviceContext* ctx = nullptr;
            device->GetImmediateContext(&ctx);
            const bool ok = frames->Publish(device, ctx, eye, meta);
            ctx->Release();
            device->Release();
            return ok;
        }
        FrameMeta m = meta;
        if (m.world) {
            // The world lane only while the game renders world views: the override is put
            // in place as soon as the core wants the world (it forwards untouched without a
            // pose) and its submit count shows whether views arrive. Loading screens submit
            // none, so they stay on the theater panel instead of a black headset.
            if (!viewOverride.Installed() && core) InstallViewOverride();
            const auto st = viewOverride.GetStats();
            const ULONGLONG now = GetTickCount64();
            if (st.submits != lastSubmits_) {
                lastSubmits_ = st.submits;
                lastSubmitMs_ = now;
            }
            m.world = viewOverride.Installed() && now - lastSubmitMs_ < 300;
            // What the latest view submits carried: the world link labels frames with it.
            m.poseId = st.poseId;
            m.poseRun = st.poseRun;
            m.renderFovY = st.renderedFovY;
            m.frameRotation = st.frameRotation; // recorded with st.poseId by the same submit
            m.aimValid = st.aimInView;
            m.aimTanX = st.aimTanX;
            m.aimTanY = st.aimTanY;
        }
        if (m.world) {
            const float aspect = ReadViewAspect();
            if (aspect > 0.2f && aspect < 5.0f) lastViewAspect = aspect;
        }
        m.renderAspect = lastViewAspect;
        const float aspect = lastViewAspect > 0 ? lastViewAspect : (meta.height ? float(meta.width) / float(meta.height) : 0.0f);
        const float tanX = m.renderFovY > 0 ? std::tan(m.renderFovY * 0.5f) * aspect : 0.0f;
        UpdateEngineStereo(m, meta, tanX);
        ID3D11Device* device = nullptr;
        ID3D11Texture2D* back = nullptr;
        bool ok = false;
        if (SUCCEEDED(swapchain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device))) &&
            SUCCEEDED(swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back)))) {
            ID3D11DeviceContext* ctx = nullptr;
            device->GetImmediateContext(&ctx);
            DepthCapture::Frame depth;
            if (ctx && m.depthStereo && !depthCapture.Installed() && !depthInstallTried) {
                depthInstallTried = true;
                const bool installed = depthCapture.Install(ctx);
                log_.Write("depth stereo: depth clear observer %s", installed ? "installed" : "refused (not d3d11.dll's, or hook failed)");
            }
            if (ctx && depthCapture.Installed() && meta.height &&
                depthCapture.TakeFrame(ctx, float(meta.width) / float(meta.height), depth) && m.depthStereo) {
                const auto st = viewOverride.GetStats();
                m.depth = depth.texture.Get();
                m.depthFormat = depth.format;
                m.depthClear = depth.clear;
                m.nearZ = st.nearZ;
                m.farZ = st.farZ;
                if (!depthSeen && developer) { // evidence: what the depth looks like, once
                    depthSeen = true;
                    depthSnapshotPending = true;
                }
            }
            ok = frames->Publish(device, ctx, back, m);
            if (ctx && depthSnapshotPending && depthCapture.Installed()) {
                depthSnapshotPending = false;
                const auto st = viewOverride.GetStats();
                const bool written = depthCapture.WriteBmp(ctx, logDir + "\\vr-snapshot-depth.bmp", st.nearZ, st.farZ);
                log_.Write("depth snapshot %s (%s, near=%.3f far=%.1f)", written ? "written: vr-snapshot-depth.bmp" : "not written",
                           depthCapture.Describe().c_str(), st.nearZ, st.farZ);
            }
            if (ctx) ctx->Release();
        }
        if (back) back->Release();
        if (device) device->Release();
        return ok;
    }

    // RE task R3: head-tracked main view through a copy of the GameRenderer vtable with
    // slot 5 replaced (Bf4ViewOverride.h). Only a pose the XR host asked to see (a world
    // lane lease, poseId != 0) is applied: the world link sends the frame back labelled
    // with exactly that pose once the submits show it carried (FrameMeta poseId/poseRun).
    // The FOV is the one both eyes need, written as the game's own FOV setter does.
    bool ApplyCamera(std::span<const CameraRequest> requests) override {
        if (requests.size() != 1 || requests[0].eye >= 0 || !requests[0].poseId || !core) return false;
        if (!viewOverride.Installed() && !InstallViewOverride()) return false;
        float fov = requests[0].renderFovY;
        if (lastViewAspect > 0.2f && lastViewAspect < 5.0f) // the aspect the engine used last frame
            fov = RequiredVerticalFov(requests[0].eyeFov[0], requests[0].eyeFov[1], lastViewAspect);
        ViewOverride::GunAimSettings gun;
        gun.enabled = requests[0].gunFollowsAim;
        gun.eye[0] = requests[0].eyeFov[0];
        gun.eye[1] = requests[0].eyeFov[1];
        gun.aspect = lastViewAspect;
        gun.budget = requests[0].gunFovBudget;
        gun.handHeld = requests[0].gunInHand;
        viewOverride.SetGunAim(gun);
        viewOverride.SetHand(requests[0].handInHead, requests[0].handValid);
        viewOverride.SetLocal(requests[0].local, /*keepGamePitch=*/false, requests[0].poseId, fov, requests[0].parentYaw);
        gunInHandFrame = requests[0].gunInHand ? presentCount : 0;
        laneFrame = presentCount;
        return true;
    }

    // First thing each Present, before the frame is captured: while the head-tracked view is
    // on, a plus-shaped mask at the centre (where BF4 draws its crosshair and hit marker, the
    // head's centre in VR) comes from the scene as it was before the HUD. The theater view keeps
    // BF4's crosshair.
    void HudTick() {
        ++presentCount;
        // BF4 builds its pad list only on a device change; one is posted at the menus if it has
        // not enumerated yet, so the stand-in pad (the VR controllers) exists from the start.
        if (presentCount == kControllerScanFrame && ControllerScan::RequestEnumeration(window))
            log_.Write("controller scan: device change posted (BF4 had not enumerated its controllers yet)");
        if (presentCount % 600 == 0) {
            log_.Write("%s", ControllerScan::Describe().c_str());
            log_.Write("%s", XInputSink::Describe().c_str());
        }
        const bool lane = laneFrame && presentCount - laneFrame <= 3;
        if (!lane && !hudSplit.Installed()) return;
        if (!hudSplit.Installed()) {
            if (hudInstallTried) return;
            hudInstallTried = true;
            ID3D11Device* device = nullptr;
            ID3D11DeviceContext* ctx = nullptr;
            if (swapchain && SUCCEEDED(swapchain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device)))) {
                device->GetImmediateContext(&ctx);
                if (ctx) hudSplit.Install(ctx);
                if (ctx) ctx->Release();
                device->Release();
            }
            log_.Write("crosshair out of the head-tracked view: hooks %s", hudSplit.Installed() ? "installed" : "not installed");
            if (!hudSplit.Installed()) return;
        }
        ID3D11Texture2D* back = nullptr;
        if (swapchain) swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back));
        hudSplit.OnPresent(back, lane, kCrosshairMask);
        if (back) back->Release();
        if (presentCount % 600 == 0) log_.Write("%s", hudSplit.Describe().c_str());
    }
    static constexpr std::uint64_t kControllerScanFrame = 600; // ~5-10 s after adoption, at the menus
    // Of the frame's height: the hip crosshair spans ~0.12; a full-auto burst spread its arms along
    // the axes to ~0.20 end to end (live, session 41), out of the 0.20 box used before. Arms of
    // 0.30 leave margin; longer ones hid an objective marker near the eye line (session 42).
    static constexpr HudSplit::CentreMask kCrosshairMask{0.10f, 0.30f, 0.04f};

    // Gun in the hand: the first-person pass gets the render's projection and is moved to the
    // hand of its own frame (Bf4FirstPersonView.h). Called each Present; off a few frames after
    // the world lane stops asking (the theater view shows BF4's own pass).
    void FirstPersonTick() {
        const bool wanted = gunInHandFrame && presentCount - gunInHandFrame <= 3 && viewOverride.Installed();
        if (!wanted) {
            if (firstPersonView.Installed() && firstPersonOn) {
                firstPersonView.SetFrame({});
                firstPersonOn = false;
                log_.Write("gun in the hand: off (%s)", firstPersonView.Describe().c_str());
            }
            return;
        }
        if (!firstPersonView.Installed()) {
            if (firstPersonInstallTried) return;
            firstPersonInstallTried = true;
            ID3D11Device* device = nullptr;
            ID3D11DeviceContext* ctx = nullptr;
            if (swapchain && SUCCEEDED(swapchain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device)))) {
                device->GetImmediateContext(&ctx);
                if (ctx) firstPersonView.Install(ctx);
                if (ctx) ctx->Release();
                device->Release();
            }
            log_.Write("gun in the hand: first-person pass hooks %s", firstPersonView.Installed() ? "installed" : "not installed");
            if (!firstPersonView.Installed()) return;
        }
        const auto st = viewOverride.GetStats();
        FirstPersonView::Frame frame;
        frame.enabled = st.renderedFovY > 0.1f && st.gameFovY > 0.1f && lastViewAspect > 0.2f;
        frame.gameFovY = st.gameFovY;
        frame.renderKy = frame.enabled ? 1.0f / std::tan(st.renderedFovY * 0.5f) : 0.0f;
        frame.renderKx = frame.enabled ? frame.renderKy / lastViewAspect : 0.0f;
        frame.handMode = true;
        const auto recent = viewOverride.RecentHands();
        for (std::size_t i = 0; i < recent.size() && i < frame.hands.size(); ++i) {
            std::memcpy(frame.hands[i].rotation, recent[i].rotation, sizeof(frame.hands[i].rotation));
            std::memcpy(frame.hands[i].gameRotation, recent[i].gameRotation, sizeof(frame.hands[i].gameRotation));
            frame.hands[i].hand = recent[i].hand;
            frame.hands[i].valid = recent[i].valid;
        }
        firstPersonView.SetFrame(frame);
        if (!firstPersonOn) log_.Write("gun in the hand: on");
        if (presentCount % 600 == 0) log_.Write("%s", firstPersonView.Describe().c_str());
        firstPersonOn = frame.enabled;
    }

    bool InstallViewOverride() {
        if (viewInstallRefused || exeSha256 != kAnalyzedBuildSha256) return false;
        const auto* global = core->Bindings().Find(names::kGameRenderer);
        const auto* submit = core->Bindings().Find(names::kRenderViewUpdate);
        if (!global || !submit || global->status != frostbite::ScanStatus::Ok || submit->status != frostbite::ScanStatus::Ok)
            return false;
        std::uintptr_t object = 0, vtable = 0;
        if (!ReadChecked(global->address, &object, 8) || !object || !ReadChecked(object, &vtable, 8))
            return false; // renderer not created yet: try again on a later frame
        std::uintptr_t slots[1 + kRendererVtableSlots] = {};
        const std::size_t tableBytes = sizeof(slots);
        if (!InGameImage(vtable - 8, tableBytes) || !ReadChecked(vtable - 8, slots, tableBytes) ||
            !InGameImage(submit->address, 1)) {
            log_.Write("view override refused: renderer vtable %p is not bf4.exe's", reinterpret_cast<void*>(vtable));
            viewInstallRefused = true;
            return false;
        }
        const auto r = viewOverride.Install(reinterpret_cast<void*>(object), submit->address);
        log_.Write("view override install: %s (renderer %p, slot5 %p, expected %p)",
                   r == ViewOverride::InstallResult::Ok ? "ok" :
                   r == ViewOverride::InstallResult::UnexpectedSubmit ? "slot 5 is not the bound submit" : "refused",
                   reinterpret_cast<void*>(object), reinterpret_cast<void*>(slots[1 + kRendererSubmitSlot]),
                   reinterpret_cast<void*>(submit->address));
        if (r != ViewOverride::InstallResult::Ok) viewInstallRefused = true;
        return r == ViewOverride::InstallResult::Ok;
    }

    void RemoveViewOverride() {
        if (!viewOverride.Installed()) return;
        std::uintptr_t current = 0; // the global as it is now: the object may be gone
        const auto* global = core ? core->Bindings().Find(names::kGameRenderer) : nullptr;
        if (global && global->status == frostbite::ScanStatus::Ok) ReadChecked(global->address, &current, 8);
        const auto r = viewOverride.Remove(reinterpret_cast<void*>(current));
        log_.Write("view override removed: %s", r == ViewOverride::RemoveResult::Restored ? "vtable restored" :
                   r == ViewOverride::RemoveResult::ObjectGone ? "renderer gone, nothing written" :
                   r == ViewOverride::RemoveResult::ForeignVtable ? "vtable owned by someone else, left alone" : "not installed");
    }

    std::string DescribeViewOverride() const {
        const auto st = viewOverride.GetStats();
        char line[320];
        std::snprintf(line, sizeof(line),
                      "view_override=%s submits=%llu applied=%llu restored=%llu foreign=%llu stale=%llu not_rigid=%llu "
                      "fov_applied=%llu game_fov=%.3f rendered_fov=%.3f pose=%llu run=%u copies=%u copies_differ=%.4f\n",
                      viewOverride.Installed() ? "on" : (viewInstallRefused ? "refused" : "off"),
                      static_cast<unsigned long long>(st.submits), static_cast<unsigned long long>(st.applied),
                      static_cast<unsigned long long>(st.restored), static_cast<unsigned long long>(st.foreign),
                      static_cast<unsigned long long>(st.stale), static_cast<unsigned long long>(st.notRigid),
                      static_cast<unsigned long long>(st.fovApplied), st.gameFovY, st.renderedFovY,
                      static_cast<unsigned long long>(st.poseId), st.poseRun, viewOverride.CopyMask(), st.copiesDiffer);
        return line;
    }

    // RE task R6: assert the reload input concept for exactly one tick.
    void RequestNativeReload() override {}

    void DetachAll(std::string_view reason) override {
        for (auto& k : keysDown)
            if (k) { SendKey(k, false); k = 0; }
        pointer.Uninstall();
        padSink.Deactivate();
        engineStereo.Disable();
        ControllerScan::Remove(); // vtable entries back; its code hook goes with the worker's MH_DisableHook below
        firstPersonView.SetFrame({}); // every bind passes through again (the hooks go with the rest)
        gunInHandFrame = 0;
        laneFrame = 0;
        if (eyeLoop.Installed()) log_.Write("eye loop override removed: %s", eyeLoop.Remove() ? "vtable restored" : "vtable not ours, left alone");
        RemoveViewOverride();
        log_.Write("detaching all hooks: %.*s", static_cast<int>(reason.size()), reason.data());
        // Never patch code from inside the Present detour; a worker does it. MinHook
        // suspends threads and fixes instruction pointers while it unpatches.
        HANDLE t = CreateThread(nullptr, 0, [](LPVOID) -> DWORD { MH_DisableHook(MH_ALL_HOOKS); return 0; }, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }

    void Log(std::string_view line) override {
        log_.Write("%.*s", static_cast<int>(line.size()), line.data());
        // "Mark this moment" also saves what the headset shows (developer evidence).
        if (developer && frames && line.rfind("FEEDBACK MARK", 0) == 0) {
            frames->RequestSnapshot(logDir);
            depthSnapshotPending = depthCapture.Installed();
            log_.Write("snapshot requested: vr-snapshot-frame.bmp / vr-snapshot-eyes.bmp in the log folder");
        }
    }

private:
    FileLog& log_;
};

// ---- Adapter instance ----------------------------------------------------------------------
// Region scan for the two-eye loop object: its class vtable first and its per-eye array in
// its inline storage. SEH-guarded reads: a region can be released while it is scanned.
int ScanRegionForEyeLoop(std::uintptr_t start, std::size_t bytes, std::uint64_t vtable, std::uintptr_t* found, int maxFound) {
    int n = 0;
    __try {
        const auto* q = reinterpret_cast<const std::uint64_t*>(start);
        const std::size_t count = bytes / 8;
        for (std::size_t i = 0; i + kEyeArrayCapacity / 8 < count && n < maxFound; ++i) {
            if (q[i] != vtable) continue;
            const std::uintptr_t object = start + i * 8, storage = object + kEyeArrayInline;
            if (q[i + kEyeArrayBegin / 8] == storage && q[i + kEyeArrayCapacity / 8] == storage + kEyeEntryBytes)
                found[n++] = object;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return n;
}

void Win32Platform::StartEyeLoopScan() {
    eyeLoopScan.store(1);
    HANDLE t = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
        auto* self = static_cast<Win32Platform*>(p);
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        const std::uint64_t vtable = base + kEyeLoopClassRva;
        std::uintptr_t first = 0;
        int total = 0;
        std::uint64_t scanned = 0;
        for (std::uintptr_t at = 0x10000;;) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQuery(reinterpret_cast<LPCVOID>(at), &mbi, sizeof(mbi))) break;
            const auto start = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
            if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && (mbi.Protect & PAGE_READWRITE) &&
                !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
                std::uintptr_t hits[4] = {};
                const int n = ScanRegionForEyeLoop(start, mbi.RegionSize, vtable, hits, 4);
                if (n && !first) first = hits[0];
                total += n;
                scanned += mbi.RegionSize;
            }
            at = start + mbi.RegionSize;
            if (at < start) break;
        }
        self->eyeLoopCandidates.store(total);
        self->eyeLoopFound.store(first);
        self->eyeLoopScannedMb.store(scanned >> 20);
        self->eyeLoopScan.store(2);
        return 0;
    }, this, 0, nullptr);
    if (t) CloseHandle(t);
    else eyeLoopScan.store(2); // no thread: no candidates, 3D stays off
}

struct Adapter {
    SessionInfo session;
    FileLog log;
    Win32Platform platform{log};
    std::unique_ptr<AdapterCore> core;
    std::mutex coreMutex; // Present and the shim's SetActiveEye callback both drive the core
    std::unique_ptr<IControllerSource> input;
    std::uint32_t width = 0, height = 0;
    std::mutex statusMutex;
    std::string status;
    std::atomic<bool> stopped{false};
    // Developer research channel (Bf4ResearchControl.h).
    std::uint64_t researchLastId = 0;
    ULONGLONG researchLastWrite = 0;
    std::deque<TimedPointerEvent> researchPointer;
    std::uint32_t researchWait = 0;
    unsigned researchKeyUp = 0, researchKeyFrames = 0;
    bool gameInFront = true; // the game window had the foreground at the last Present
    std::uint32_t depthProbeFrames = 0, mouseProbeFrames = 0, sendKeyFrames = 0, viewDumpFrames = 0;
    std::uint64_t depthProbeId = 0, mouseProbeId = 0, viewDumpId = 0;
    std::string viewDumpName;
    std::int32_t mouseProbeX = 0, mouseProbeY = 0;
    AimAngle mouseBefore{};
    std::uint16_t sendKeyScan = 0;
    ShaderCensus shaderCensus; // developer research: vertex shader reflection names
    DrawProbe drawProbe;       // developer research: one frame of draws (Bf4DrawProbe.h)
    std::uint64_t drawProbeId = 0;
    std::uint32_t drawProbeFrames = 0;
    std::string drawProbeName;
    FirstPersonView firstPersonView; // developer experiment `fpview <mode>` (Bf4FirstPersonView.h)
    int firstPersonMode = 0;
};

Adapter* g_adapter = nullptr;

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
PresentFn g_present = nullptr;
ResizeBuffersFn g_resizeBuffers = nullptr;

void PublishStatus(Adapter& a) {
    std::string s = a.core ? a.core->Status() : std::string("core not created\n");
    if (a.platform.frames) s += a.platform.frames->Describe();
    s += a.platform.DescribeViewOverride();
    if (a.core && a.session.stereo != StereoSetting::Off) a.platform.WriteReport("stereo-probe.txt", a.core->StereoReport());
    std::lock_guard<std::mutex> lock(a.statusMutex);
    a.status = std::move(s);
}

bool Adopt(Adapter& a, IDXGISwapChain* sc) {
    if (a.platform.swapchain) return a.platform.swapchain == sc;
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(sc->GetDesc(&desc)) || !desc.OutputWindow) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(desc.OutputWindow, &pid);
    if (pid != GetCurrentProcessId() || GetAncestor(desc.OutputWindow, GA_ROOT) != desc.OutputWindow) return false;
    if (desc.BufferDesc.Width < 640 || desc.BufferDesc.Height < 360) return false; // helper swapchains
    a.platform.swapchain = sc;
    a.platform.window = desc.OutputWindow;
    a.width = desc.BufferDesc.Width;
    a.height = desc.BufferDesc.Height;
    a.log.Write("adopted swapchain %ux%u fmt=%u windowed=%d", a.width, a.height,
                static_cast<unsigned>(desc.BufferDesc.Format), static_cast<int>(desc.Windowed));
    if (a.session.developerFeatures) Diagnostics::WatchDeviceChanges(desc.OutputWindow);
    a.core->OnSwapchainAdopted(true);
    // Developer research: reflect every vertex shader created from now on (the level's are
    // created when it loads). Observes CreateVertexShader only.
    if (a.session.developerFeatures && !a.session.researchDumpDir.empty()) {
        ID3D11Device* device = nullptr;
        if (SUCCEEDED(sc->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device))) && device) {
            a.log.Write("shader census: %s", a.shaderCensus.Install(device) ? "installed" : "not installed");
            device->Release();
        }
    }
    PublishStatus(a);
    return true;
}

void ResearchTick(Adapter& a); // developer research channel, defined below
void CameraProbeTick(Adapter& a); // read-only camera discovery, defined below
void SessionProbeTick(Adapter& a); // read-only session-signal discovery, defined below
void StereoProbeTick(Adapter& a); // read-only stereo route evidence, defined below
void FiringCensusTick(Adapter& a); // read-only weapon-firing census, defined below

void OnPresent(Adapter& a, IDXGISwapChain* sc) {
    if (!Adopt(a, sc)) return;
    Diagnostics::FramePresented();
    if (!a.stopped && !a.core->Detached()) { // research and probes end with any lockdown
        ResearchTick(a);
        CameraProbeTick(a);
        SessionProbeTick(a);
        StereoProbeTick(a);
        FiringCensusTick(a);
    }
    PlatformFrame f;
    f.tickMs = GetTickCount64();
    f.backbufferWidth = a.width;
    f.backbufferHeight = a.height;
    RECT client{};
    GetClientRect(a.platform.window, &client);
    f.clientWidth = client.right - client.left;
    f.clientHeight = client.bottom - client.top;
    const bool inFront = a.platform.window && GetForegroundWindow() == a.platform.window;
    if (inFront) { // Win32 state only, no game memory
        CURSORINFO cursor{};
        cursor.cbSize = sizeof(cursor);
        f.gameCursor = GetCursorInfo(&cursor) && (cursor.flags & CURSOR_SHOWING) && cursor.hCursor;
    }
    if (a.platform.window && inFront != a.gameInFront) { // focus changes explain stalls and lost input
        a.gameInFront = inFront;
        a.log.Write("game window %s; %s", inFront ? "in front again" : "lost the foreground",
                    Diagnostics::DescribeForeground().c_str());
    }
    ControllerSnapshot in;
    a.input->Poll(in);
    a.platform.TryBindShim();
    std::lock_guard<std::mutex> lock(a.coreMutex);
    if (!a.core->Detached()) a.platform.HudTick();
    a.core->OnFrame(f, in);
    if (!a.core->Detached()) a.platform.FirstPersonTick();
    if (a.core->Detached()) {
        a.stopped = true;
        PublishStatus(a);
    } else if ((a.core->Stats().frames % 30) == 0) {
        PublishStatus(a);
    }
    // Theater link health in the log (captured/published/failed/discarded), every ~600 frames:
    // the only way to see from a live session whether the game's frames reach the XR host.
    if (a.platform.frames && a.core->Stats().frames % 600 == 0) {
        std::string d = a.platform.frames->Describe();
        while (!d.empty() && (d.back() == '\n' || d.back() == '\r')) d.pop_back();
        a.log.Write("%s frames=%llu backbuffer=%ux%u client=%ldx%ld", d.c_str(),
                    static_cast<unsigned long long>(a.core->Stats().frames), f.backbufferWidth, f.backbufferHeight,
                    static_cast<long>(f.clientWidth), static_cast<long>(f.clientHeight));
        std::string v = a.platform.DescribeViewOverride(); // world lane: pose runs and FOV in the same log
        while (!v.empty() && (v.back() == '\n' || v.back() == '\r')) v.pop_back();
        a.log.Write("%s stereo=%s %s", v.c_str(), std::string(StereoModeName(a.core->Stereo())).c_str(),
                    a.platform.DescribeEngineStereo().c_str());
        if (a.platform.depthCapture.Installed()) a.log.Write("%s", a.platform.depthCapture.Describe().c_str());
        const std::string status = a.core->Status();
        const auto at = status.find("controller_aim=");
        if (at != std::string::npos)
            a.log.Write("%s mouse_sends=%llu", status.substr(at, status.find('\n', at) - at).c_str(),
                        static_cast<unsigned long long>(a.platform.mouseSends));
        const auto crouchAt = status.find(" crouch=");
        const std::string crouch = crouchAt == std::string::npos ? std::string() : status.substr(crouchAt, status.find('\n', crouchAt) - crouchAt);
        a.log.Write("%s key_sends=%llu haptics_sent=%llu%s", a.core->Gestures().Describe().c_str(),
                    static_cast<unsigned long long>(a.platform.keySends), static_cast<unsigned long long>(a.platform.hapticsSent),
                    crouch.c_str());
    }
}

HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain* sc, UINT sync, UINT flags) {
    if (g_adapter && !g_adapter->stopped.load(std::memory_order_relaxed)) OnPresent(*g_adapter, sc);
    return g_present(sc, sync, flags);
}

HRESULT STDMETHODCALLTYPE HookResizeBuffers(IDXGISwapChain* sc, UINT count, UINT w, UINT h, DXGI_FORMAT fmt, UINT flags) {
    const bool ours = g_adapter && g_adapter->platform.swapchain == sc;
    if (ours && g_adapter->platform.frames) g_adapter->platform.frames->ReleaseSwapchainResources();
    const HRESULT hr = g_resizeBuffers(sc, count, w, h, fmt, flags);
    if (ours && SUCCEEDED(hr)) {
        DXGI_SWAP_CHAIN_DESC desc{};
        if (SUCCEEDED(sc->GetDesc(&desc))) {
            g_adapter->width = desc.BufferDesc.Width;
            g_adapter->height = desc.BufferDesc.Height;
        }
    }
    return hr;
}

// Throwaway device/swapchain only to read the shared IDXGISwapChain vtable.
bool HookSwapchainVtable(FileLog& log) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"FvrBf4VtableProbe";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    IDXGISwapChain* sc = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    HRESULT hr = E_FAIL;
    for (D3D_DRIVER_TYPE type : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
        hr = D3D11CreateDeviceAndSwapChain(nullptr, type, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev, nullptr, &ctx);
        if (SUCCEEDED(hr)) break;
    }
    bool ok = false;
    if (SUCCEEDED(hr)) {
        void** vtable = *reinterpret_cast<void***>(sc);
        ok = MH_CreateHook(vtable[8], reinterpret_cast<LPVOID>(&HookPresent), reinterpret_cast<LPVOID*>(&g_present)) == MH_OK &&
             MH_CreateHook(vtable[13], reinterpret_cast<LPVOID>(&HookResizeBuffers), reinterpret_cast<LPVOID*>(&g_resizeBuffers)) == MH_OK &&
             MH_EnableHook(vtable[8]) == MH_OK && MH_EnableHook(vtable[13]) == MH_OK;
    }
    log.Write("swapchain vtable hook: hr=0x%08lx ok=%d", static_cast<unsigned long>(hr), static_cast<int>(ok));
    if (ctx) ctx->Release();
    if (dev) dev->Release();
    if (sc) sc->Release();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return ok;
}

// Read-only identity + binding scan of the running bf4.exe.
void Identify(Adapter& a, BuildIdentity& build, BindingTable& bindings) {
    const auto* base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
    build = IdentifyBuild(std::span<const std::uint8_t>(base, 4096), a.session.exeSha256);
    a.log.Write("exe: %s (%s) sha256=%s", std::string(BuildVerdictName(build.verdict)).c_str(), build.label.c_str(),
                a.session.exeSha256.c_str());
    if (build.verdict != BuildVerdict::Enrolled && build.verdict != BuildVerdict::UnknownX64) return;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (std::memcmp(sec->Name, ".text", 5) != 0) continue;
        const std::uint8_t* code = base + sec->VirtualAddress;
        bindings.Resolve(std::span<const std::uint8_t>(code, sec->Misc.VirtualSize), reinterpret_cast<std::uint64_t>(code));
    }
    for (const auto& b : bindings.All())
        a.log.Write("binding %s: %s", std::string(b.name).c_str(), std::string(frostbite::ScanStatusName(b.status)).c_str());
}

// Developer research: one read-only copy of the mapped bf4.exe image (bf4.exe is
// encrypted on disk; by the time the dxgi proxy loads us its code is decrypted). Pages
// that are not committed+readable are written as zeros, never touched. Offline RE only.
void WriteResearchImage(Adapter& a) {
    if (!a.session.developerFeatures || a.session.researchDumpDir.empty()) return;
    const auto* base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const std::size_t size = nt->OptionalHeader.SizeOfImage;
    if (size == 0 || size > (512u << 20)) {
        a.log.Write("research image: refused, SizeOfImage=%zu", size);
        return;
    }
    CreateDirectoryA(a.session.researchDumpDir.c_str(), nullptr);
    const std::string path = a.session.researchDumpDir + "\\bf4-image.bin";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        a.log.Write("research image: cannot write %s", path.c_str());
        return;
    }
    std::vector<std::uint8_t> zeros(64 * 1024, 0);
    std::size_t copied = 0, zeroed = 0;
    for (std::size_t off = 0; off < size;) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(base + off, &mbi, sizeof(mbi))) break;
        const std::size_t regionEnd = static_cast<const std::uint8_t*>(mbi.BaseAddress) - base + mbi.RegionSize;
        const std::size_t n = std::min(regionEnd, size) - off;
        const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                               PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY;
        if (mbi.State == MEM_COMMIT && (mbi.Protect & readable) && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
            std::fwrite(base + off, 1, n, f);
            copied += n;
        } else {
            for (std::size_t left = n; left;) {
                const std::size_t k = std::min(left, zeros.size());
                std::fwrite(zeros.data(), 1, k, f);
                left -= k;
            }
            zeroed += n;
        }
        off += n;
    }
    std::fclose(f);
    a.log.Write("research image: %s base=%p size=%zu copied=%zu zeroed=%zu", path.c_str(), static_cast<const void*>(base),
                size, copied, zeroed);
}

// ---- Read-only camera probe (RE task R3) ---------------------------------------------
// Addresses come from the static analysis of ONE build (docs/BF4-CAMERA-RE.md); any other
// executable hash skips the probe. Every read is VirtualQuery-checked; nothing is written.
constexpr std::uintptr_t kRendererGlobalRva = 0x2672378;

bool ReadChecked(std::uintptr_t address, void* out, std::size_t size) {
    if (address < 0x10000) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
        (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || mbi.Protect == 0)
        return false;
    const auto end = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    if (address + size > end) return false;
    std::memcpy(out, reinterpret_cast<const void*>(address), size);
    return true;
}

void CameraProbeTick(Adapter& a) {
    if (!a.session.developerFeatures || a.session.exeSha256 != kAnalyzedBuildSha256 || !a.core) return;
    const std::uint64_t frame = a.core->Stats().frames;
    const std::uint64_t interval = frame < 3600 ? 120 : 600; // ~2 s for the first minute, then ~10 s
    if (frame == 0 || frame % interval) return;
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    std::uintptr_t object = 0, vtable = 0, getter = 0;
    std::uint8_t getterBytes[5] = {};
    const bool haveObject = ReadChecked(base + kRendererGlobalRva, &object, 8) && object;
    const bool haveVtable = haveObject && ReadChecked(object, &vtable, 8) && ReadChecked(vtable + 19 * 8, &getter, 8);
    const int getterOffset =
        haveVtable && ReadChecked(getter, getterBytes, sizeof(getterBytes)) ? FieldGetterOffset(getterBytes) : -1;
    // +0x60: the renderer's copy of this frame's views (entry 0 = main view, written by the
    // view setup +0x848B00); +0x70: the view state slot 19 returns on the analysed build.
    for (const std::uintptr_t field : {std::uintptr_t{0x60}, std::uintptr_t{0x70}}) {
        std::uintptr_t view = 0;
        std::uint8_t raw[0x460];
        if (!haveObject || !ReadChecked(object + field, &view, 8) || !view || !ReadChecked(view, raw, sizeof(raw))) {
            a.log.Write("camera probe: object=%p vtable=%d slot19=+0x%x field=+0x%x view=%p (unreadable)",
                        reinterpret_cast<void*>(object), static_cast<int>(haveVtable), getterOffset,
                        static_cast<unsigned>(field), reinterpret_cast<void*>(view));
            continue;
        }
        const ViewSample s = ParseViewSample(raw);
        auto t = [](const Matrix4f& m) { return m.m.data() + 12; };
        a.log.Write("camera probe: object=%p slot19=+0x%x field=+0x%x view=%p | +00 rigid=%d t=(%.2f %.2f %.2f) "
                    "fwd=(%.2f %.2f %.2f) | +40 rigid=%d t=(%.2f %.2f %.2f) | +420 rigid=%d | fov=%.2f near=%.3f "
                    "far=%.1f aspect=%.3f plausible=%d",
                    reinterpret_cast<void*>(object), getterOffset, static_cast<unsigned>(field),
                    reinterpret_cast<void*>(view), static_cast<int>(IsRigidTransform(s.at00)), t(s.at00)[0],
                    t(s.at00)[1], t(s.at00)[2], s.at00.m[8], s.at00.m[9], s.at00.m[10],
                    static_cast<int>(IsRigidTransform(s.at40)), t(s.at40)[0], t(s.at40)[1], t(s.at40)[2],
                    static_cast<int>(IsRigidTransform(s.at420)), s.fovY, s.nearPlane, s.farPlane, s.aspect,
                    static_cast<int>(PlausibleProjection(s)));
    }
}

// ---- Read-only session-signal probe (RE task R5) --------------------------------------
// Static candidates (docs/BF4-SESSION-RE.md): the status message producer +0x10438A0
// takes its level name from [[+0x27A1918]+0x50] and its isMultiplayer byte from
// [[[[+0x2670D80]+0x28]+0x38]+0xA0] (default multiplayer when a link is null). Logged
// only; nothing consumes these until a live session confirms them.
std::string ReadCStringChecked(std::uintptr_t address, std::size_t maxLength) {
    std::string out;
    char c = 0;
    for (std::size_t i = 0; i < maxLength && address && ReadChecked(address + i, &c, 1) && c; ++i)
        out.push_back(c >= 0x20 && c < 0x7F ? c : '?');
    return out;
}

// Read-only stereo evidence (docs/CURRENT-BF4.md "Stereo"): the render device the
// GameRenderer constructor stores in +0x2738080 (vtable +0x1DD1870), its settings at +0x80
// (RenderDevice.* reflection offsets), output 0's per-frame stereo fields (+0xC12FF0 writes
// them), and view 0's projection inputs (projection builder +0xB7EA10: type +0xB0, frustum
// offsets +0x90/+0x98/+0xA0, +0xC8..+0xD4, flags +0xD8/+0xD9, target +0xEA8). Logged at
// ~5 s, ~20 s and then every ~2 min in developer sessions.

void StereoProbeTick(Adapter& a) {
    if (!a.session.developerFeatures || a.session.exeSha256 != kAnalyzedBuildSha256 || !a.core) return;
    const std::uint64_t frame = a.core->Stats().frames;
    if (frame != 300 && frame != 1200 && (frame < 1200 || frame % 7200)) return;
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    std::uintptr_t device = 0, vtable = 0, settings = 0, outputs = 0;
    std::uint32_t outputCount = 0;
    const bool haveDevice = ReadChecked(base + kRenderDeviceGlobalRva, &device, 8) && device && ReadChecked(device, &vtable, 8);
    const bool expected = haveDevice && vtable == base + kRenderDeviceClassRva;
    if (!expected) {
        a.log.Write("stereo probe: device=%p vtable=+0x%llx (not the analysed render device)", reinterpret_cast<void*>(device),
                    static_cast<unsigned long long>(vtable ? vtable - base : 0));
        return;
    }
    ReadChecked(device + 0x80, &settings, 8);
    ReadChecked(device + 0x30, &outputCount, 4);
    ReadChecked(device + 0x38, &outputs, 8);
    std::uint8_t s[0x84] = {};
    const bool haveSettings = settings && ReadChecked(settings, s, sizeof(s));
    auto f32 = [](const std::uint8_t* p, std::size_t off) { float v; std::memcpy(&v, p + off, 4); return v; };
    auto u32 = [](const std::uint8_t* p, std::size_t off) { std::uint32_t v; std::memcpy(&v, p + off, 4); return v; };
    std::uint8_t o[0x90] = {};
    const bool haveOutput = outputs && outputCount && ReadChecked(outputs, o, sizeof(o));
    std::uint16_t ow = 0, oh = 0;
    if (haveOutput) { std::memcpy(&ow, o + 0x58, 2); std::memcpy(&oh, o + 0x5C, 2); }
    a.log.Write("stereo probe: device=%p outputs=%u settings=%p fullscreen=%ux%u render_ahead=%d stereo_depth=%.3f "
                "convergence_scale=%.3f separation_scale=%.3f stereo_enable=%u split_screen=%u | output0 %ux%u view_size=%ux%u mode=%u on=%u "
                "convergence=%.4f separation=%.4f",
                reinterpret_cast<void*>(device), outputCount, reinterpret_cast<void*>(settings),
                haveSettings ? u32(s, 0x2C) : 0, haveSettings ? u32(s, 0x28) : 0,
                haveSettings ? static_cast<int>(u32(s, 0x40)) : -1, haveSettings ? f32(s, 0x44) : 0.0f,
                haveSettings ? f32(s, 0x48) : 0.0f, haveSettings ? f32(s, 0x4C) : 0.0f, haveSettings ? s[0x7D] : 0xFF,
                haveSettings ? s[0x7E] : 0xFF, ow, oh, haveOutput ? u32(o, 0x60) : 0, haveOutput ? u32(o, 0x64) : 0,
                haveOutput ? u32(o, 0x7C) : 0xFFFFFFFF, haveOutput ? o[0x80] : 0xFF,
                haveOutput ? f32(o, 0x84) : 0.0f, haveOutput ? f32(o, 0x88) : 0.0f);
    std::uintptr_t renderer = 0, views = 0;
    std::uint8_t v[0xEB0] = {};
    if (ReadChecked(base + kRendererGlobalRva, &renderer, 8) && renderer && ReadChecked(renderer + 0x60, &views, 8) && views &&
        ReadChecked(views, v, sizeof(v))) {
        std::uintptr_t target = 0;
        std::memcpy(&target, v + 0xEA8, 8);
        std::uint32_t count = 0;
        ReadChecked(renderer + 0x58, &count, 4);
        a.log.Write("stereo probe: views=%u view0 type=%u fov=%.3f aspect=%.3f off90=(%.4f %.4f) off98=(%.4f %.4f) "
                    "offA0=(%.4f %.4f) c8=%.4f cc=%.4f d0=%.4f d4=%.4f d8=%u d9=%u secondary=%u target=%p",
                    count, u32(v, 0xB0), f32(v, 0xB4), f32(v, 0xC4), f32(v, 0x90), f32(v, 0x94), f32(v, 0x98), f32(v, 0x9C),
                    f32(v, 0xA0), f32(v, 0xA4), f32(v, 0xC8), f32(v, 0xCC), f32(v, 0xD0), f32(v, 0xD4), v[0xD8], v[0xD9],
                    v[0xEA0], reinterpret_cast<void*>(target));
    }
}

void SessionProbeTick(Adapter& a) {
    if (!a.session.developerFeatures || a.session.exeSha256 != kAnalyzedBuildSha256 || !a.core) return;
    const std::uint64_t frame = a.core->Stats().frames;
    const std::uint64_t interval = frame < 3600 ? 120 : 600;
    if (frame == 0 || frame % interval) return;
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    std::uintptr_t info = 0, name = 0, modeBegin = 0, ctx = 0, level = 0, setup = 0;
    std::uint8_t mp = 0xFF;
    const bool haveInfo = ReadChecked(base + kLevelInfoRva, &info, 8) && info;
    if (haveInfo) {
        ReadChecked(info + 0x50, &name, 8);
        ReadChecked(info + 0x58, &modeBegin, 8);
    }
    const bool haveCtx = ReadChecked(base + kClientGameContextRva, &ctx, 8) && ctx;
    if (haveCtx && ReadChecked(ctx + 0x28, &level, 8) && level && ReadChecked(level + 0x38, &setup, 8) && setup)
        ReadChecked(setup + 0xA0, &mp, 1);
    a.log.Write("session probe: level=\"%s\" mode=\"%s\" isMultiplayer=%d (info=%p ctx=%p ctx+28=%p +38=%p)",
                ReadCStringChecked(name).c_str(), ReadCStringChecked(modeBegin, 64).c_str(),
                mp == 0xFF ? -1 : static_cast<int>(mp), reinterpret_cast<void*>(info), reinterpret_cast<void*>(ctx),
                reinterpret_cast<void*>(level), reinterpret_cast<void*>(setup));
}

// ---- Read-only weapon-firing census (manual reload research P1) ------------------------
// docs/BF4-MANUAL-RELOAD.md 2.1: copies the game's private read/write memory with
// ReadProcessMemory (a region freed mid-scan fails the copy instead of faulting), keeps
// objects that pass the local rules and whose +0x130 is exactly their own
// FiringFunctionData + 0x220, and logs them. Low-priority thread, one census at a time,
// developer sessions on the analysed build only. Nothing is written.
std::atomic<bool> g_censusRunning{false};

bool ReadSelf(std::uint64_t address, void* out, std::size_t size) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), out, size, &got) && got == size;
}

struct ImageRanges { std::uintptr_t base = 0, end = 0, textBegin = 0, textEnd = 0; };

ImageRanges GameImageRanges() {
    ImageRanges r;
    r.base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(r.base + reinterpret_cast<const IMAGE_DOS_HEADER*>(r.base)->e_lfanew);
    r.end = r.base + nt->OptionalHeader.SizeOfImage;
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec)
        if (std::memcmp(sec->Name, ".text", 5) == 0) {
            r.textBegin = r.base + sec->VirtualAddress;
            r.textEnd = r.textBegin + sec->Misc.VirtualSize;
        }
    return r;
}

// A vtable: inside the image but outside code, and its first slot points into code.
bool LooksLikeVtable(const ImageRanges& r, std::uint64_t v) {
    if (v < r.base || v + 8 > r.end || (v >= r.textBegin && v < r.textEnd) || (v & 7)) return false;
    std::uint64_t slot0 = 0;
    return ReadSelf(v, &slot0, 8) && slot0 >= r.textBegin && slot0 < r.textEnd;
}

// Every 8-aligned qword in private read/write memory equal to one of `targets` (at most
// `perTarget` locations each), skipping `exclude` ranges (the caller's own copies) and this
// scan's buffer. Read-only, ReadProcessMemory-chunked; returns early on stop.
using AddressRange = std::pair<std::uintptr_t, std::uintptr_t>;
std::vector<std::pair<std::uint64_t, std::uint64_t>> FindPointers(const std::vector<std::uint64_t>& targets, std::size_t perTarget,
                                                                  const std::atomic<bool>& stop,
                                                                  const std::vector<AddressRange>& exclude) {
    std::vector<std::pair<std::uint64_t, std::uint64_t>> hits;
    if (targets.empty()) return hits;
    std::vector<std::size_t> count(targets.size(), 0);
    const auto bounds = std::minmax_element(targets.begin(), targets.end());
    const std::uint64_t lo = *bounds.first, hi = *bounds.second;
    constexpr std::size_t kChunk = 1u << 20;
    std::vector<std::uint8_t> buf(kChunk);
    std::vector<AddressRange> skip = exclude;
    skip.emplace_back(reinterpret_cast<std::uintptr_t>(buf.data()), reinterpret_cast<std::uintptr_t>(buf.data()) + buf.size());
    skip.emplace_back(reinterpret_cast<std::uintptr_t>(targets.data()),
                      reinterpret_cast<std::uintptr_t>(targets.data() + targets.size()));
    auto skipped = [&](std::uintptr_t a) {
        for (const auto& r : skip)
            if (a >= r.first && a < r.second) return true;
        return false;
    };
    for (std::uintptr_t at = 0x10000; !stop;) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<LPCVOID>(at), &mbi, sizeof(mbi))) break;
        const auto start = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        const std::uintptr_t end = start + mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && (mbi.Protect & PAGE_READWRITE) &&
            !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
            for (std::uintptr_t c = start; c < end && !stop; c += kChunk) {
                const std::size_t want = static_cast<std::size_t>(std::min<std::uintptr_t>(kChunk, end - c));
                if (!ReadSelf(c, buf.data(), want)) continue;
                for (std::size_t off = 0; off + 8 <= want; off += 8) {
                    std::uint64_t q;
                    std::memcpy(&q, buf.data() + off, 8);
                    if (q < lo || q > hi) continue;
                    for (std::size_t i = 0; i < targets.size(); ++i)
                        if (q == targets[i] && count[i] < perTarget && !skipped(c + off)) {
                            hits.emplace_back(targets[i], c + off);
                            ++count[i];
                        }
                }
            }
        at = end;
        if (at <= start) break;
    }
    return hits;
}

void RunFiringCensus(Adapter& a, const char* why) {
    if (a.session.exeSha256 != kAnalyzedBuildSha256 || g_censusRunning.exchange(true)) return;
    struct Job { Adapter* a; std::string why; };
    auto* job = new Job{&a, why};
    HANDLE t = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
        std::unique_ptr<Job> j(static_cast<Job*>(p));
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
        struct Entry { std::uint64_t address, vtable, data; FiringObjectFacts o; FiringDataFacts d; };
        std::vector<Entry> found;
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
        const std::uintptr_t imageEnd = base + nt->OptionalHeader.SizeOfImage;
        constexpr std::size_t kChunk = 1u << 20;
        std::vector<std::uint8_t> buf(kChunk + kFiringObjectBytes);
        std::uint8_t data[kFiringDataBytes];
        std::uint64_t scanned = 0, local = 0;
        const ULONGLONG t0 = GetTickCount64();
        bool aborted = false;
        for (std::uintptr_t at = 0x10000; found.size() < 512;) {
            if (j->a->stopped) { aborted = true; break; } // multiplayer lockdown: stop reading at once
            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQuery(reinterpret_cast<LPCVOID>(at), &mbi, sizeof(mbi))) break;
            const auto start = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
            const std::uintptr_t end = start + mbi.RegionSize;
            if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && (mbi.Protect & PAGE_READWRITE) &&
                !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
                for (std::uintptr_t c = start; c < end && found.size() < 512; c += kChunk) {
                    if (j->a->stopped) break; // the outer loop reports the abort
                    const std::size_t want = static_cast<std::size_t>(std::min<std::uintptr_t>(kChunk + kFiringObjectBytes, end - c));
                    if (want < kFiringObjectBytes || !ReadSelf(c, buf.data(), want)) continue;
                    scanned += std::min<std::size_t>(kChunk, want);
                    for (std::size_t off = 0; off < kChunk && off + kFiringObjectBytes <= want; off += 8) {
                        const auto o = ParseFiringObject(buf.data() + off);
                        if (!o) continue;
                        ++local;
                        const std::uint64_t dataAddr = o->ammoConfig - kAmmoConfigInFiringData;
                        std::uint64_t primaryFire = 0;
                        if (!ReadSelf(o->weaponFiringData + 0x10, &primaryFire, 8) || primaryFire != dataAddr) continue;
                        if (!ReadSelf(dataAddr, data, sizeof(data))) continue;
                        const auto d = ParseFiringData(data);
                        if (!d) continue;
                        std::uint64_t vt = 0;
                        std::memcpy(&vt, buf.data() + off, 8);
                        found.push_back({c + off, vt >= base && vt < imageEnd ? vt - base : 0, dataAddr, *o, *d});
                    }
                }
            }
            at = end;
            if (at <= start) break;
        }
        std::fill(buf.begin(), buf.end(), std::uint8_t{0}); // no stale copies for the pointer passes
        buf.clear();
        buf.shrink_to_fit();
        Adapter& a = *j->a;
        if (aborted) {
            a.log.Write("firing census (%s): aborted, adapter stopped", j->why.c_str());
            g_censusRunning = false;
            return 0;
        }
        std::size_t player = 0;
        for (const auto& e : found) player += !e.o.UnlimitedAmmo();
        a.log.Write("firing census (%s): %zu objects (%zu without unlimited ammo), %llu local matches, %lluMB in %llums",
                    j->why.c_str(), found.size(), player, static_cast<unsigned long long>(local),
                    static_cast<unsigned long long>(scanned >> 20), static_cast<unsigned long long>(GetTickCount64() - t0));
        // Limited-ammo objects first: the player's weapons are among them; AI carry unlimited ammo.
        std::stable_sort(found.begin(), found.end(), [](const Entry& x, const Entry& y) {
            return !x.o.UnlimitedAmmo() && y.o.UnlimitedAmmo();
        });
        for (std::size_t i = 0; i < found.size() && i < 32; ++i) {
            const auto& e = found[i];
            a.log.Write("  firing %llx vt=+%llx data=%llx state=%d->%d loaded=%d reserve=%d cap=%d mags=%d reloadType=%d "
                        "fireLogic=%d mode=%d step=%d mult=%.2f thr=%.2f time=%.2f flags=%02x/%02x%s",
                        static_cast<unsigned long long>(e.address), static_cast<unsigned long long>(e.vtable),
                        static_cast<unsigned long long>(e.data), e.o.state, e.o.nextState, e.o.loaded, e.o.reserve, e.d.capacity,
                        e.d.magazines, e.d.reloadType, e.d.fireLogic, e.o.fireMode, e.o.reloadStep, e.o.reloadMultiplier,
                        e.d.threshold, e.d.reloadTime, e.o.flags208, e.o.flags209, e.o.UnlimitedAmmo() ? " unlimited" : "");
        }
        // Same weapon data, same counts, limited ammo, different objects: a client/server pair candidate.
        for (std::size_t i = 0; i < found.size(); ++i)
            for (std::size_t k = i + 1; k < found.size(); ++k)
                if (found[i].data == found[k].data && !found[i].o.UnlimitedAmmo() && !found[k].o.UnlimitedAmmo() &&
                    found[i].o.loaded == found[k].o.loaded && found[i].o.reserve == found[k].o.reserve)
                    a.log.Write("  pair candidate: %llx (vt +%llx) and %llx (vt +%llx), data %llx, loaded %d reserve %d",
                                static_cast<unsigned long long>(found[i].address), static_cast<unsigned long long>(found[i].vtable),
                                static_cast<unsigned long long>(found[k].address), static_cast<unsigned long long>(found[k].vtable),
                                static_cast<unsigned long long>(found[i].data), found[i].o.loaded, found[i].o.reserve);
        // Ownership: who holds each limited-ammo firing object (client weapon at +0x8, server
        // weapon at +0xA0, told apart by their vtables), then who holds those weapons, so one
        // session yields the next links of the player -> weapon chain (RE task R8).
        const ImageRanges image = GameImageRanges();
        std::vector<std::uint64_t> firing;
        for (const auto& e : found)
            if (!e.o.UnlimitedAmmo() && firing.size() < 8) firing.push_back(e.address);
        std::vector<std::uint64_t> owners;
        std::vector<FiringHolder> ownerKinds;
        const std::vector<AddressRange> own{
            {reinterpret_cast<std::uintptr_t>(found.data()), reinterpret_cast<std::uintptr_t>(found.data() + found.size())}};
        for (const std::uint64_t target : firing) {
            std::size_t client = 0, server = 0, other = 0;
            std::string others;
            for (const auto& hit : FindPointers({target}, 4096, a.stopped, own)) { // never cap before the real holders
                const std::uint64_t at = hit.second;
                std::uint64_t clientSlot = 0, serverSlot = 0;
                ReadSelf(at - kClientWeaponFiringOffset, &clientSlot, 8);
                ReadSelf(at - kServerWeaponFiringOffset, &serverSlot, 8);
                const auto kind = ClassifyFiringHolder(clientSlot, serverSlot, image.base);
                if (kind == FiringHolder::Unknown) {
                    if (++other <= 4) {
                        char b[24];
                        std::snprintf(b, sizeof(b), " %llx", static_cast<unsigned long long>(at));
                        others += b;
                    }
                    continue;
                }
                const bool clientSide = kind != FiringHolder::ServerWeapon;
                const std::uint64_t owner = at - (clientSide ? kClientWeaponFiringOffset : kServerWeaponFiringOffset);
                ++(clientSide ? client : server);
                if (owners.size() < 8) { owners.push_back(owner); ownerKinds.push_back(kind); }
                a.log.Write("  held: firing %llx -> %s %llx", static_cast<unsigned long long>(target), FiringHolderName(kind),
                            static_cast<unsigned long long>(owner));
            }
            a.log.Write("  holders of firing %llx: %zu client weapon, %zu server weapon, %zu other:%s",
                        static_cast<unsigned long long>(target), client, server, other, others.c_str());
        }
        // Next link: the client weapon entity holding class A (+0xA8) or B (+0xA0), and which
        // one it has active (+0x98); then, generically, whoever holds those entities.
        std::vector<AddressRange> ownBooks = own;
        ownBooks.emplace_back(reinterpret_cast<std::uintptr_t>(owners.data()), reinterpret_cast<std::uintptr_t>(owners.data() + owners.size()));
        auto readQword = [](std::uint64_t address) {
            std::uint64_t q = 0;
            ReadSelf(address, &q, 8);
            return q;
        };
        auto guessObject = [&](std::uint64_t at, std::uint64_t& objectStart, std::uint64_t& vtable) {
            objectStart = vtable = 0;
            for (std::uint64_t p = at & ~std::uint64_t{7}; p + 0x1000 > at && p >= 0x10000; p -= 8) {
                std::uint64_t q = 0;
                if (!ReadSelf(p, &q, 8)) break;
                if (LooksLikeVtable(image, q)) { objectStart = p; vtable = q; break; }
            }
        };
        std::vector<std::uint64_t> parents;
        std::size_t holderLines = 0;
        for (const auto& hit : FindPointers(owners, 4096, a.stopped, ownBooks)) {
            const std::uint64_t owner = hit.first, at = hit.second;
            const auto kindAt = std::find(owners.begin(), owners.end(), owner) - owners.begin();
            const FiringHolder kind = ownerKinds[static_cast<std::size_t>(kindAt)];
            if (const std::uint64_t parent = ClientWeaponParent(kind, at, image.base, readQword)) {
                const std::uint64_t active = readQword(parent + kParentActiveOffset);
                if (std::find(parents.begin(), parents.end(), parent) == parents.end() && parents.size() < 8) parents.push_back(parent);
                a.log.Write("  client weapon entity %llx holds %s %llx; active (+0x98) = %llx (A %llx, B %llx)",
                            static_cast<unsigned long long>(parent), FiringHolderName(kind), static_cast<unsigned long long>(owner),
                            static_cast<unsigned long long>(active),
                            static_cast<unsigned long long>(readQword(parent + kParentHoldsAOffset)),
                            static_cast<unsigned long long>(readQword(parent + kParentHoldsBOffset)));
                continue;
            }
            std::uint64_t objectStart = 0, vtable = 0;
            guessObject(at, objectStart, vtable);
            if (!objectStart || ++holderLines > 16) continue; // stack slots and other non-object holders; bounded log
            a.log.Write("  weapon %llx held at %llx: nearest vtable %llx (vt +%llx) at -%llx (a guess)", static_cast<unsigned long long>(owner),
                        static_cast<unsigned long long>(at), static_cast<unsigned long long>(objectStart),
                        static_cast<unsigned long long>(vtable - image.base), static_cast<unsigned long long>(at - objectStart));
        }
        ownBooks.emplace_back(reinterpret_cast<std::uintptr_t>(parents.data()), reinterpret_cast<std::uintptr_t>(parents.data() + parents.size()));
        holderLines = 0;
        for (const auto& hit : FindPointers(parents, 4096, a.stopped, ownBooks)) {
            std::uint64_t objectStart = 0, vtable = 0;
            guessObject(hit.second, objectStart, vtable);
            if (!objectStart || ++holderLines > 16) continue;
            a.log.Write("  weapon entity %llx held at %llx: nearest vtable %llx (vt +%llx) at -%llx (a guess)",
                        static_cast<unsigned long long>(hit.first), static_cast<unsigned long long>(hit.second),
                        static_cast<unsigned long long>(objectStart), static_cast<unsigned long long>(vtable - image.base),
                        static_cast<unsigned long long>(hit.second - objectStart));
        }
        a.log.Write("firing census (%s): done in %llums", j->why.c_str(), static_cast<unsigned long long>(GetTickCount64() - t0));
        g_censusRunning = false;
        return 0;
    }, job, 0, nullptr);
    if (t) CloseHandle(t);
    else { delete job; g_censusRunning = false; }
}

// Automatic in developer sessions: ~30 s in, then every ~2 minutes.
void FiringCensusTick(Adapter& a) {
    if (!a.session.developerFeatures || !a.core) return;
    const std::uint64_t frame = a.core->Stats().frames;
    if (frame == 1800 || (frame > 1800 && frame % 7200 == 0)) RunFiringCensus(a, "periodic");
}

void ResearchAck(Adapter& a, std::uint64_t id, const char* result) {
    const std::string path = a.session.researchDumpDir + "\\ack.txt";
    if (std::FILE* f = std::fopen(path.c_str(), "ab")) {
        std::fprintf(f, "%llu %s frame=%llu\n", static_cast<unsigned long long>(id), result,
                     static_cast<unsigned long long>(a.core ? a.core->Stats().frames : 0));
        std::fclose(f);
    }
    a.log.Write("research %llu: %s", static_cast<unsigned long long>(id), result);
}

void ResearchTick(Adapter& a) {
    if (!a.session.developerFeatures || a.session.researchDumpDir.empty() || !a.platform.window) return;
    HWND hwnd = a.platform.window;
    // 0. Experiment fpview: the first-person pass's view constants, set per frame.
    if (a.firstPersonMode && a.firstPersonView.Installed()) {
        const auto st = a.platform.viewOverride.GetStats();
        FirstPersonView::Frame frame;
        frame.enabled = st.renderedFovY > 0.1f && st.gameFovY > 0.1f && a.height;
        frame.gameFovY = st.gameFovY;
        frame.renderKy = frame.enabled ? 1.0f / std::tan(st.renderedFovY * 0.5f) : 0.0f;
        frame.renderKx = a.height ? frame.renderKy * float(a.height) / float(a.width) : 0.0f;
        Mat4 m = Identity4();
        if (a.firstPersonMode == 2) m[7] = -0.10f; // 10 cm down (camera space: +y up)
        if (a.firstPersonMode == 3) { m[3] = 0.15f; m[7] = -0.10f; m[11] = 0.10f; } // right, down, closer
        frame.transform = m;
        a.firstPersonView.SetFrame(frame);
    }
    // 0. Follow-ups of depthprobe / mouse / sendkey (per frame).
    if (a.sendKeyFrames && --a.sendKeyFrames == 0) Win32Platform::SendKey(a.sendKeyScan, false);
    if (a.mouseProbeFrames && --a.mouseProbeFrames == 0) {
        AimAngle after{};
        float n = 0, f = 0;
        char msg[240];
        if (a.platform.ReadGameView(after, n, f)) {
            const float dyaw = WrapPi(after.yaw - a.mouseBefore.yaw), dpitch = after.pitch - a.mouseBefore.pitch;
            std::snprintf(msg, sizeof(msg), "mouse %d %d: yaw %.4f -> %.4f (%.6f rad/count) pitch %.4f -> %.4f (%.6f rad/count)",
                          a.mouseProbeX, a.mouseProbeY, a.mouseBefore.yaw, after.yaw, a.mouseProbeX ? dyaw / a.mouseProbeX : 0.0f,
                          a.mouseBefore.pitch, after.pitch, a.mouseProbeY ? dpitch / a.mouseProbeY : 0.0f);
        } else {
            std::snprintf(msg, sizeof(msg), "mouse %d %d: camera not readable afterwards", a.mouseProbeX, a.mouseProbeY);
        }
        a.log.Write("research %s", msg);
        ResearchAck(a, a.mouseProbeId, msg);
    }
    if (a.drawProbeFrames) { // research drawprobe: one whole frame between two Presents
        if (a.drawProbe.OnPresent()) {
            a.drawProbeFrames = 0;
            std::string report = a.drawProbe.TakeReport();
            ID3D11Texture2D* back = nullptr;
            if (a.platform.swapchain &&
                SUCCEEDED(a.platform.swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back))) && back) {
                char line[64];
                std::snprintf(line, sizeof(line), "backbuffer @%llx\n",
                              static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(back) & 0xFFFFFFull));
                report = line + report;
                back->Release();
            }
            const std::string file = a.session.researchDumpDir + "\\" + a.drawProbeName + ".txt";
            bool ok = false;
            if (std::FILE* f = std::fopen(file.c_str(), "wb")) {
                ok = std::fwrite(report.data(), 1, report.size(), f) == report.size();
                std::fclose(f);
            }
            const std::string msg = "drawprobe " + a.drawProbeName + (ok ? ": written " : ": write failed ") +
                                    report.substr(0, report.find('\n'));
            a.log.Write("research %s", msg.c_str());
            ResearchAck(a, a.drawProbeId, msg.c_str());
        } else if (--a.drawProbeFrames == 0) {
            DrawProbe::state.store(static_cast<int>(DrawProbe::State::Idle));
            ResearchAck(a, a.drawProbeId, "drawprobe: no frame recorded");
        }
    }
    if (a.viewDumpFrames) { // research viewdump: the copy is taken by the next submit
        std::array<std::uint8_t, kViewBlockBytes> block{};
        std::uint32_t count = 0;
        if (a.platform.viewOverride.TakeBlockDump(block, count)) {
            a.viewDumpFrames = 0;
            const std::string path = a.session.researchDumpDir + "\\" + a.viewDumpName + ".bin";
            bool ok = false;
            if (std::FILE* f = std::fopen(path.c_str(), "wb")) {
                ok = std::fwrite(block.data(), 1, block.size(), f) == block.size();
                std::fclose(f);
            }
            char msg[160];
            std::snprintf(msg, sizeof(msg), "viewdump %s: %s (%u views submitted, block 0x%zx bytes)", a.viewDumpName.c_str(),
                          ok ? "written" : "write failed", count, block.size());
            a.log.Write("research %s", msg);
            ResearchAck(a, a.viewDumpId, msg);
        } else if (--a.viewDumpFrames == 0) {
            ResearchAck(a, a.viewDumpId, "viewdump: no view submit through the override (is it installed?)");
        }
    }
    if (a.depthProbeFrames && --a.depthProbeFrames == 0) {
        AimAngle unused{};
        float n = 0, f = 0;
        a.platform.ReadGameView(unused, n, f);
        bool written = false;
        std::string depth;
        ID3D11Device* device = nullptr;
        if (a.platform.swapchain && SUCCEEDED(a.platform.swapchain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device)))) {
            ID3D11DeviceContext* ctx = nullptr;
            device->GetImmediateContext(&ctx);
            if (ctx) {
                written = a.platform.depthCapture.WriteBmp(ctx, a.session.logDir + "\\vr-snapshot-depth.bmp", n, f, &depth);
                ctx->Release();
            }
            device->Release();
        }
        char planes[96];
        std::snprintf(planes, sizeof(planes), " near=%.3f far=%.1f snapshot=%s", n, f, written ? "vr-snapshot-depth.bmp" : "none");
        const std::string msg = a.platform.depthCapture.Describe() + planes + (depth.empty() ? "" : " " + depth);
        a.log.Write("research depthprobe: %s", msg.c_str());
        ResearchAck(a, a.depthProbeId, msg.c_str());
    }
    // 1. Pending pointer/key events (one step per frame budget).
    if (a.researchKeyFrames && --a.researchKeyFrames == 0) {
        PostMessageW(hwnd, WM_KEYUP, a.researchKeyUp, 0xC0000001 | (MapVirtualKeyW(a.researchKeyUp, MAPVK_VK_TO_VSC) << 16));
    }
    if (!a.researchPointer.empty()) {
        if (a.researchWait) { --a.researchWait; return; }
        const PointerEvent e = a.researchPointer.front().event;
        a.researchPointer.pop_front();
        a.platform.pointer.Apply(hwnd, std::span<const PointerEvent>(&e, 1));
        if (!a.researchPointer.empty()) a.researchWait = a.researchPointer.front().frameDelay;
        return;
    }
    // 2. New commands: look at the file every ~15 frames, run one command per tick.
    static std::uint64_t tick = 0;
    if (++tick % 15) return;
    const std::string path = a.session.researchDumpDir + "\\cmd.txt";
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fa)) return;
    const ULONGLONG written = (static_cast<ULONGLONG>(fa.ftLastWriteTime.dwHighDateTime) << 32) | fa.ftLastWriteTime.dwLowDateTime;
    if (written == a.researchLastWrite && a.researchPointer.empty()) return;
    std::string text;
    if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
        char buf[4096];
        std::size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0 && text.size() < 65536) text.append(buf, n);
        std::fclose(f);
    }
    const auto commands = ParseResearchCommands(text, a.researchLastId);
    if (commands.empty()) {
        a.researchLastWrite = written;
        return;
    }
    const ResearchCommand& c = commands.front(); // one per tick; the rest on later ticks
    a.researchLastId = c.id;
    RECT client{};
    GetClientRect(hwnd, &client);
    switch (c.kind) {
    case ResearchCommand::Kind::Snap: {
        const bool ok = a.platform.WriteBackbufferBmp(a.session.researchDumpDir + "\\" + c.arg + ".bmp");
        ResearchAck(a, c.id, ok ? ("snap " + c.arg).c_str() : "snap failed");
        break;
    }
    case ResearchCommand::Kind::AimTrace: {
        std::string csv;
        {
            std::lock_guard<std::mutex> lock(a.coreMutex);
            csv = a.core->AimTraceCsv();
        }
        const std::string path = a.session.researchDumpDir + "\\" + c.arg + ".csv";
        bool ok = false;
        if (std::FILE* f = std::fopen(path.c_str(), "wb")) {
            ok = std::fwrite(csv.data(), 1, csv.size(), f) == csv.size();
            std::fclose(f);
        }
        ResearchAck(a, c.id, (std::string("aimtrace ") + c.arg + (ok ? ": written" : ": write failed")).c_str());
        break;
    }
    case ResearchCommand::Kind::ShaderNames: {
        const std::string report = a.shaderCensus.Report();
        const std::string file = a.session.researchDumpDir + "\\shader-names.txt";
        bool ok = false;
        if (std::FILE* f = std::fopen(file.c_str(), "wb")) {
            ok = std::fwrite(report.data(), 1, report.size(), f) == report.size();
            std::fclose(f);
        }
        ResearchAck(a, c.id, (a.shaderCensus.Describe() + (ok ? " written" : " write failed")).c_str());
        break;
    }
    case ResearchCommand::Kind::ShaderDump: {
        const std::string dir = a.session.researchDumpDir + "\\shaders";
        CreateDirectoryA(dir.c_str(), nullptr);
        const int n = a.shaderCensus.Dump(dir, c.arg, 64);
        ResearchAck(a, c.id, ("shaderdump " + c.arg + ": " + std::to_string(n) + " written").c_str());
        break;
    }
    case ResearchCommand::Kind::FirstPersonView: {
        bool ok = a.firstPersonView.Installed();
        ID3D11Device* device = nullptr;
        if (!ok && c.x && a.platform.swapchain &&
            SUCCEEDED(a.platform.swapchain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device)))) {
            ID3D11DeviceContext* ctx = nullptr;
            device->GetImmediateContext(&ctx);
            if (ctx) {
                ok = a.firstPersonView.Install(ctx);
                ctx->Release();
            }
            device->Release();
        }
        a.firstPersonMode = ok ? c.x : 0;
        if (!a.firstPersonMode) a.firstPersonView.SetFrame({}); // off: every bind passes through
        const std::string msg = "fpview " + std::to_string(a.firstPersonMode) + " " + a.firstPersonView.Describe();
        a.log.Write("research %s", msg.c_str());
        ResearchAck(a, c.id, msg.c_str());
        break;
    }
    case ResearchCommand::Kind::DrawProbe: {
        bool ok = a.drawProbe.Installed();
        ID3D11Device* device = nullptr;
        if (!ok && a.platform.swapchain &&
            SUCCEEDED(a.platform.swapchain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device)))) {
            ID3D11DeviceContext* ctx = nullptr;
            device->GetImmediateContext(&ctx);
            if (ctx) {
                ok = a.drawProbe.Install(ctx, a.shaderCensus.Installed() ? &a.shaderCensus : nullptr);
                ctx->Release();
            }
            device->Release();
        }
        if (ok) {
            a.drawProbe.Start();
            a.drawProbeFrames = 10;
            a.drawProbeId = c.id;
            a.drawProbeName = c.arg;
        } else {
            ResearchAck(a, c.id, "drawprobe: hooks not installed");
        }
        break;
    }
    case ResearchCommand::Kind::ViewDump: {
        a.platform.viewOverride.RequestBlockDump();
        a.viewDumpFrames = 120;
        a.viewDumpId = c.id;
        a.viewDumpName = c.arg;
        break;
    }
    case ResearchCommand::Kind::Click:
    case ResearchCommand::Kind::Move: {
        for (const auto& e : PointerSequence(c.u, c.v, client.right - client.left, client.bottom - client.top,
                                             c.kind == ResearchCommand::Kind::Click))
            a.researchPointer.push_back(e);
        a.researchWait = 0;
        char msg[64];
        std::snprintf(msg, sizeof(msg), "%s %.3f %.3f", c.kind == ResearchCommand::Kind::Click ? "click" : "move", c.u, c.v);
        ResearchAck(a, c.id, msg);
        break;
    }
    case ResearchCommand::Kind::Key: {
        const unsigned vk = ResearchKeyCode(c.arg);
        const LPARAM down = 1 | (MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) << 16);
        PostMessageW(hwnd, WM_KEYDOWN, vk, down);
        if (vk == 0x0D || vk == 0x20) PostMessageW(hwnd, WM_CHAR, vk, down);
        if (vk == 0xC0) PostMessageW(hwnd, WM_CHAR, '`', down);
        a.researchKeyUp = vk;
        a.researchKeyFrames = 6;
        ResearchAck(a, c.id, ("key " + c.arg).c_str());
        break;
    }
    case ResearchCommand::Kind::DumpImage:
        WriteResearchImage(a);
        ResearchAck(a, c.id, "dumpimage");
        break;
    case ResearchCommand::Kind::ScanQ: {
        // Background scan of committed read/write memory plus the exe image: never blocks
        // the render thread. Read-only; results are addresses only.
        struct Job { Adapter* a; std::uint64_t id; std::uint64_t rva; };
        auto* job = new Job{&a, c.id, c.value};
        HANDLE t = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
            std::unique_ptr<Job> j(static_cast<Job*>(p));
            const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            const std::uint64_t needle = base + j->rva;
            std::string found;
            int hits = 0;
            std::uint64_t scanned = 0;
            for (std::uintptr_t at = 0x10000; hits < 16;) {
                MEMORY_BASIC_INFORMATION mbi{};
                if (!VirtualQuery(reinterpret_cast<LPCVOID>(at), &mbi, sizeof(mbi))) break;
                const auto start = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
                const bool rw = mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY);
                if (mbi.State == MEM_COMMIT && rw && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
                    const auto* q = reinterpret_cast<const std::uint64_t*>(start);
                    const std::size_t n = mbi.RegionSize / 8;
                    for (std::size_t i = 0; i < n && hits < 16; ++i)
                        if (q[i] == needle) {
                            char addr[32];
                            std::snprintf(addr, sizeof(addr), " %llx", static_cast<unsigned long long>(start + i * 8));
                            found += addr;
                            ++hits;
                        }
                    scanned += mbi.RegionSize;
                }
                at = start + mbi.RegionSize;
                if (at < start) break;
            }
            char msg[96];
            std::snprintf(msg, sizeof(msg), "scanq %llx hits=%d scanned=%lluMB:", static_cast<unsigned long long>(j->rva), hits,
                          static_cast<unsigned long long>(scanned >> 20));
            ResearchAck(*j->a, j->id, (msg + found).c_str());
            return 0;
        }, job, 0, nullptr);
        if (t) CloseHandle(t);
        else { delete job; ResearchAck(a, c.id, "scanq: no thread"); }
        break;
    }
    case ResearchCommand::Kind::Census:
        RunFiringCensus(a, "research");
        ResearchAck(a, c.id, "census started (results in the adapter log)");
        break;
    case ResearchCommand::Kind::Peek: {
        MEMORY_BASIC_INFORMATION mbi{};
        const auto addr = static_cast<std::uintptr_t>(c.value);
        const bool readable = VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT &&
                              !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
                              addr + c.length <= reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (!readable) { ResearchAck(a, c.id, "peek: not readable"); break; }
        std::string hex = "peek";
        char b[8];
        for (std::uint32_t i = 0; i < c.length; ++i) {
            std::snprintf(b, sizeof(b), "%s%02x", i % 8 ? "" : " ", reinterpret_cast<const std::uint8_t*>(addr)[i]);
            hex += b;
        }
        ResearchAck(a, c.id, hex.c_str());
        break;
    }
    case ResearchCommand::Kind::Type: {
        for (char ch : c.arg) PostMessageW(hwnd, WM_CHAR, static_cast<WPARAM>(ch == '_' ? ' ' : ch), 1);
        ResearchAck(a, c.id, ("type " + c.arg).c_str());
        break;
    }
    case ResearchCommand::Kind::DepthProbe: {
        bool ok = a.platform.depthCapture.Installed();
        ID3D11Device* device = nullptr;
        if (!ok && a.platform.swapchain &&
            SUCCEEDED(a.platform.swapchain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device)))) {
            ID3D11DeviceContext* ctx = nullptr;
            device->GetImmediateContext(&ctx);
            if (ctx) {
                ok = a.platform.depthCapture.Install(ctx);
                ctx->Release();
            }
            device->Release();
        }
        if (ok) {
            a.depthProbeFrames = 120;
            a.depthProbeId = c.id;
            a.log.Write("research depthprobe: depth clear observer installed; report in ~120 frames");
        } else {
            ResearchAck(a, c.id, "depthprobe: refused (not d3d11.dll's function, or the hook failed)");
        }
        break;
    }
    case ResearchCommand::Kind::Mouse: {
        float n = 0, f = 0;
        if (GetForegroundWindow() != hwnd) { ResearchAck(a, c.id, "mouse: the game window is not focused"); break; }
        if (!a.platform.ReadGameView(a.mouseBefore, n, f)) { ResearchAck(a, c.id, "mouse: camera not readable"); break; }
        a.platform.SendMouse(c.x, c.y);
        a.mouseProbeX = c.x;
        a.mouseProbeY = c.y;
        a.mouseProbeId = c.id;
        a.mouseProbeFrames = 20;
        break;
    }
    case ResearchCommand::Kind::SendKey: {
        if (GetForegroundWindow() != hwnd) { ResearchAck(a, c.id, "sendkey: the game window is not focused"); break; }
        if (a.sendKeyFrames) Win32Platform::SendKey(a.sendKeyScan, false);
        a.sendKeyScan = static_cast<std::uint16_t>(c.value);
        Win32Platform::SendKey(a.sendKeyScan, true);
        a.sendKeyFrames = 6;
        char msg[48];
        std::snprintf(msg, sizeof(msg), "sendkey 0x%02x", static_cast<unsigned>(c.value));
        ResearchAck(a, c.id, msg);
        break;
    }
    case ResearchCommand::Kind::ViewCopies: {
        a.platform.viewOverride.SetCopyMask(static_cast<std::uint32_t>(c.x));
        char msg[48];
        std::snprintf(msg, sizeof(msg), "viewcopies %u", a.platform.viewOverride.CopyMask());
        a.log.Write("research %s", msg);
        ResearchAck(a, c.id, msg);
        break;
    }
    case ResearchCommand::Kind::Setting: {
        bool ok = false;
        {
            std::lock_guard<std::mutex> lock(a.coreMutex);
            ok = a.core->SetMenuSetting(c.arg, c.x);
        }
        ResearchAck(a, c.id, ((ok ? "setting " : "setting refused: ") + c.arg + " " + std::to_string(c.x)).c_str());
        break;
    }
    case ResearchCommand::Kind::Unknown:
        ResearchAck(a, c.id, ("rejected: " + c.arg).c_str());
        break;
    }
}

} // namespace
} // namespace fvr::bf4

// ---- Exports ------------------------------------------------------------------------------
extern "C" __declspec(dllexport) int __cdecl FvrBf4_Start(const char* sessionText) {
    using namespace fvr::bf4;
    if (g_adapter) return 1;
    auto info = ParseSessionInfo(sessionText ? sessionText : "");
    if (!info) return -1;

    auto* a = new Adapter(); // lives for the process: hooks may run until exit
    a->session = std::move(*info);
    a->log.Open(a->session.logDir);
    a->log.Write("FrostbiteVR BF4 adapter start (protocol %d, reload=%s, developer=%d)", a->session.protocol,
                 std::string(fvr::interaction::ReloadModeName(a->session.reloadMode)).c_str(),
                 static_cast<int>(a->session.developerFeatures));
    a->log.Write("stereo=%s route=%s", a->session.stereo == StereoSetting::Off ? "off" : a->session.stereo == StereoSetting::Probe ? "probe" : "on",
                 a->session.stereoRoute == StereoRoute::Split ? "split-screen" : "nvapi");

    BuildIdentity build;
    BindingTable bindings(DefaultBindingSpecs());
    Identify(*a, build, bindings);
    if (a->session.developerFeatures && !a->session.researchDumpDir.empty()) {
        CreateDirectoryA(a->session.researchDumpDir.c_str(), nullptr);
        // Commands already in the file belong to earlier sessions: never replay them.
        std::string earlier;
        if (std::FILE* f = std::fopen((a->session.researchDumpDir + "\\cmd.txt").c_str(), "rb")) {
            char buf[4096];
            std::size_t n;
            while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0 && earlier.size() < 65536) earlier.append(buf, n);
            std::fclose(f);
        }
        a->researchLastId = LastResearchCommandId(earlier);
        a->log.Write("research channel: %s\\cmd.txt (commands after id %llu)", a->session.researchDumpDir.c_str(),
                     static_cast<unsigned long long>(a->researchLastId));
    }
    a->core = std::make_unique<AdapterCore>(a->session, build, std::move(bindings), a->platform);
    g_adapter = a;
    if (a->core->Detached()) { // wrong architecture: never hook anything
        PublishStatus(*a);
        return -2;
    }
    a->platform.frames = CreateFrameSink(a->session);
    a->input = CreateControllerSource(a->session);
    a->platform.logDir = a->session.logDir;
    a->platform.stereoSetting = a->session.stereo;
    a->platform.stereoRoute = a->session.stereoRoute;
    a->platform.exeSha256 = a->session.exeSha256;
    a->platform.core = a->core.get();
    a->platform.coreMutex = &a->coreMutex;
    a->platform.developer = a->session.developerFeatures;
    if (a->session.developerFeatures && !a->session.logDir.empty()) {
        Adapter* self = a;
        Diagnostics::Start([self](const std::string& line) { self->log.Write("%s", line.c_str()); },
                           a->session.logDir + "\\faults.log");
    }
    {
        const std::string marker = a->session.logDir + "\\stereo-armed.txt";
        if (GetFileAttributesA(marker.c_str()) != INVALID_FILE_ATTRIBUTES) {
            a->platform.stereoCrashGuard = true;
            std::remove(marker.c_str());
            a->core->SetWorldStereo(false);
            a->log.Write("the last session ended with BF4 split 3D on (an older build): it stays off (unsupported)");
        }
    }
    if (MH_Initialize() != MH_OK) {
        a->log.Write("MinHook init failed");
        return -3;
    }
    // Runs inside the game's first CreateDXGIFactory call (dxgi proxy), i.e. before BF4
    // has looked at any adapter, so an opted-in vendor report applies from the start.
    // Only the 3D Vision route needs the NVIDIA report; split-screen works on any GPU.
    a->platform.gpu = QueryGpuVendor(a->session.stereo != StereoSetting::Off && a->session.stereoRoute == StereoRoute::Nvapi &&
                                     a->session.spoofGpuVendor);
    a->log.Write("gpu vendor 0x%04x, nvidia report %s", a->platform.gpu.realVendorId,
                 a->platform.gpu.spoofInstalled ? "installed (stereo probe)" : "off");
    if (!a->platform.pointer.Install()) a->log.Write("pointer hooks unavailable; game-menu pointer will not move the cursor");
    a->log.Write("virtual gamepad (XInput) hooks: %s", a->platform.padSink.Install() ? "installed" : "unavailable");
    {
        // BF4's controller scan (Bf4ControllerScan.h): its WMI IsXInputDevice froze the game for
        // seconds per device change, and with no XInput pad attached it never polled the VR pad.
        void* check = nullptr;
        const auto* binding = a->core->Bindings().Find(names::kIsXInputDevice);
        if (binding && binding->status == fvr::frostbite::ScanStatus::Ok) {
            void* fn = reinterpret_cast<void*>(binding->address);
            if (ControllerScan::LooksLikeWmiCheck(fn)) check = fn;
            else a->log.Write("controller scan: Input.IsXInputDevice matched but its WMI call is not where expected; BF4's own check stays");
        }
        if (a->session.developerFeatures) {
            ControllerScan::SetLog([](const char* line) { if (g_adapter) g_adapter->log.Write("%s", line); });
            // Developer experiments: a file in the research folder sets how Steam's pads count.
            const auto flag = [&](const char* name) {
                const std::string file = a->session.researchDumpDir + "\\" + name;
                return !a->session.researchDumpDir.empty() && GetFileAttributesA(file.c_str()) != INVALID_FILE_ATTRIBUTES;
            };
            if (flag("controller-scan-hide-steam")) ControllerScan::SetSteamPads(ControllerScan::SteamPads::Hidden);
            else if (flag("controller-scan-steam-as-di")) ControllerScan::SetSteamPads(ControllerScan::SteamPads::AsDirectInput);
            if (flag("controller-scan-hide-steam") || flag("controller-scan-steam-as-di"))
                a->log.Write("controller scan: Steam's virtual pads %s (developer experiment)",
                             flag("controller-scan-hide-steam") ? "hidden" : "as DirectInput pads");
        }
        ControllerScan::Install(check);
        a->log.Write("%s", ControllerScan::Describe().c_str());
    }
    const bool hooked = HookSwapchainVtable(a->log);
    if (!hooked) a->core->OnSwapchainAdopted(false);
    PublishStatus(*a);
    return hooked ? 0 : -4;
}

// Plain-text status for the emulator, launcher diagnostics and feedback reports.
extern "C" __declspec(dllexport) int __cdecl FvrBf4_Status(char* buffer, int size) {
    using namespace fvr::bf4;
    if (!g_adapter || !buffer || size <= 0) return 0;
    std::lock_guard<std::mutex> lock(g_adapter->statusMutex);
    const std::size_t n = std::min<std::size_t>(g_adapter->status.size(), static_cast<std::size_t>(size - 1));
    std::memcpy(buffer, g_adapter->status.data(), n);
    buffer[n] = '\0';
    return static_cast<int>(n);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
