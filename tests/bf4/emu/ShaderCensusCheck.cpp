// The developer shader census on the emulator's device: the CreateVertexShader observer sees
// a shader created after it is installed and names its constant buffer and variables. And the
// draw probe: one frame's draw with its bone palette and camera position read back.
#include "../../../src/games/bf4/Bf4DrawProbe.h"
#include "../../../src/games/bf4/Bf4FirstPersonView.h"
#include "../../../src/games/bf4/Bf4HudSplit.h"
#include "../../../src/games/bf4/Bf4ShaderCensus.h"

#include <MinHook.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

bool CheckShaderCensus(ID3D11Device* device, const char*& why) {
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return why = "MinHook", false;
    const char* source =
        "cbuffer PerView : register(b0) { float4x4 viewProj; float fpScale; };\n"
        "float4 main(float3 p : POSITION) : SV_Position { return mul(viewProj, float4(p * fpScale, 1)); }\n";
    ComPtr<ID3DBlob> code, errors;
    if (FAILED(D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &code, &errors)))
        return why = "compile the test shader", false;
    fvr::bf4::ShaderCensus census;
    if (!census.Install(device)) return why = "install the CreateVertexShader observer", false;
    ComPtr<ID3D11VertexShader> shader;
    const HRESULT hr = device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader);
    const std::string report = census.Report();
    census.Remove();
    if (FAILED(hr) || !shader) return why = "the creation call failed through the observer", false;
    if (report.find("shaders=1 unreflected=0 layouts=1") == std::string::npos) return why = "shader not counted", false;
    if (report.find("cb PerView[80]{viewProj@0:64 fpScale@64:4}") == std::string::npos)
        return why = "constant buffer layout not reflected", false;
    if (report.find("\n1 fpScale\n") == std::string::npos) return why = "variable name not counted", false;
    // Removed: later shaders are not observed.
    ComPtr<ID3D11VertexShader> after;
    if (FAILED(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &after)))
        return why = "creation after removal", false;
    if (census.Report().find("shaders=1 ") == std::string::npos) return why = "observed after removal", false;
    return true;
}

bool CheckDrawProbe(ID3D11Device* device, const char*& why) {
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return why = "MinHook", false;
    ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(&context);
    // BF4's names: bones in functionConstants (b0), the camera in viewConstants (b2).
    const char* source =
        "cbuffer functionConstants : register(b0) { float4 boneVectors[6]; };\n"
        "cbuffer viewConstants : register(b2) { float4x4 viewProj; float3 cameraPos; };\n"
        "float4 main(uint id : SV_VertexID) : SV_Position {\n"
        "  float4 p = float4(id, 0, 0, 1);\n"
        "  float3 w = float3(dot(boneVectors[0], p), dot(boneVectors[1], p), dot(boneVectors[2], p));\n"
        "  w += boneVectors[3].xyz + boneVectors[4].xyz + boneVectors[5].xyz;\n"
        "  return mul(viewProj, float4(w - cameraPos, 1)); }\n";
    ComPtr<ID3DBlob> code, errors;
    if (FAILED(D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &code, &errors)))
        return why = "compile the skinned test shader", false;
    fvr::bf4::ShaderCensus census;
    if (!census.Install(device)) return why = "install the census", false;
    ComPtr<ID3D11VertexShader> shader;
    device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader);
    census.Remove();
    if (!shader || census.LayoutOf(shader.Get()) != 0) return why = "the shader's layout id", false;
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = 96;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ComPtr<ID3D11Buffer> bones, view;
    if (FAILED(device->CreateBuffer(&bd, nullptr, &bones))) return why = "bone buffer", false;
    bd.ByteWidth = 80;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.CPUAccessFlags = 0;
    if (FAILED(device->CreateBuffer(&bd, nullptr, &view))) return why = "view buffer", false;
    std::string report;
    {
        fvr::bf4::DrawProbe probe;
        if (!probe.Install(context.Get(), &census)) return why = "install the draw probe", false;
        probe.Start();
        probe.OnPresent(); // armed -> recording
        // Two bones: translations (1 2 3) and (0.1 0.2 0.3); the camera at (1 2 3).
        const float palette[24] = {1, 0, 0, 1, 0, 1, 0, 2, 0, 0, 1, 3, 1, 0, 0, 0.1f, 0, 1, 0, 0.2f, 0, 0, 1, 0.3f};
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(context->Map(bones.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return why = "map", false;
        std::memcpy(m.pData, palette, sizeof(palette));
        context->Unmap(bones.Get(), 0);
        float constants[20] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 2, 3, 0};
        context->UpdateSubresource(view.Get(), 0, nullptr, constants, 0, 0);
        ID3D11Buffer* bound[3] = {bones.Get(), nullptr, view.Get()};
        context->VSSetConstantBuffers(0, 3, bound);
        context->VSSetShader(shader.Get(), nullptr, 0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->Draw(3, 0);
        if (!probe.OnPresent()) return why = "frame not completed", false;
        report = probe.TakeReport();
        probe.Remove();
        context->VSSetShader(nullptr, nullptr, 0);
        ID3D11Buffer* none[3]{};
        context->VSSetConstantBuffers(0, 3, none);
    }
    if (report.find("draws=1 ") == std::string::npos) return why = "draw not recorded", false;
    if (report.find(" d n=3 i=1 L0 ") == std::string::npos) return why = "draw kind/count/layout", false;
    if (report.find(" cam=(1.00 2.00 3.00)") == std::string::npos) return why = "camera position from b2", false;
    if (report.find(" bones=2 cam[0.00..3.37] org[0.37..3.74]") == std::string::npos) return why = "bone translations from b0", false;
    return true;
}

namespace {
// A viewConstants block as BF4 lays it out: identity rotation, the camera at `cam`, projected
// with scale (kx, ky) (BF4's infinite-far perspective, near 0.06).
void MakeViewBlock(std::uint8_t* block, float kx, float ky, const float cam[3]) {
    using L = fvr::bf4::ViewConstantsLayout;
    std::memset(block, 0, L::kSize);
    const fvr::bf4::Mat4 view{1, 0, 0, -cam[0], 0, 1, 0, -cam[1], 0, 0, 1, -cam[2], 0, 0, 0, 1};
    const fvr::bf4::Mat4 proj{kx, 0, 0, 0, 0, ky, 0, 0, 0, 0, -1, -0.06f, 0, 0, -1, 0};
    fvr::bf4::Mat4 rotation = view;
    rotation[3] = rotation[7] = rotation[11] = 0;
    std::memcpy(block + L::kViewMatrix, view.data(), 64);
    std::memcpy(block + L::kProjMatrix, proj.data(), 64);
    const auto vp = fvr::bf4::Mul(proj, view), cr = fvr::bf4::Mul(proj, rotation);
    std::memcpy(block + L::kViewProjMatrix, vp.data(), 64);
    std::memcpy(block + L::kCrViewProjMatrix, cr.data(), 64);
    const float k[4] = {kx, ky, 1, -0.06f};
    std::memcpy(block + L::kProjectionK, k, 16);
    std::memcpy(block + L::kCameraPos, cam, 12);
}

bool ReadBuffer(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Buffer* buffer, std::vector<std::uint8_t>& out) {
    D3D11_BUFFER_DESC d{};
    buffer->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;
    if (FAILED(device->CreateBuffer(&d, nullptr, &staging))) return false;
    context->CopyResource(staging.Get(), buffer);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
    out.assign(static_cast<const std::uint8_t*>(m.pData), static_cast<const std::uint8_t*>(m.pData) + d.ByteWidth);
    context->Unmap(staging.Get(), 0);
    return true;
}

float FloatAt(const std::vector<std::uint8_t>& b, std::size_t offset) {
    float f = 0;
    std::memcpy(&f, b.data() + offset, 4);
    return f;
}
} // namespace

bool CheckFirstPersonView(ID3D11Device* device, const char*& why) {
    using L = fvr::bf4::ViewConstantsLayout;
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return why = "MinHook", false;
    ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(&context);
    ComPtr<ID3D11DeviceContext1> context1;
    if (FAILED(context.As(&context1))) return why = "no ID3D11DeviceContext1", false;
    // BF4's ring: the first-person block at 256 bytes (game FOV 45 deg: ky 2.4142), the world's
    // at 1024 (the render's 110 deg: ky 0.6982).
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = 4096;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ComPtr<ID3D11Buffer> ring;
    if (FAILED(device->CreateBuffer(&bd, nullptr, &ring))) return why = "ring buffer", false;
    const float gameKy = 1.0f / std::tan(0.785398f * 0.5f), renderKy = 0.6982f, aspect = 16.0f / 9.0f;
    const float fpCam[3] = {0.3f, 2.1f, 0.9f}, worldCam[3] = {-600, 383, -273};
    fvr::bf4::FirstPersonView view;
    if (!view.Install(context.Get())) return why = "install", false;
    fvr::bf4::FirstPersonView::Frame frame;
    frame.enabled = true;
    frame.gameFovY = 0.785398f;
    frame.renderKy = renderKy;
    frame.renderKx = renderKy / aspect;
    frame.transform[7] = -0.10f; // 10 cm down
    view.SetFrame(frame);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(context->Map(ring.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return why = "map the ring", false;
    MakeViewBlock(static_cast<std::uint8_t*>(m.pData) + 256, gameKy / aspect, gameKy, fpCam);
    MakeViewBlock(static_cast<std::uint8_t*>(m.pData) + 1024, renderKy / aspect, renderKy, worldCam);
    context->Unmap(ring.Get(), 0);
    ID3D11Buffer* bind[1] = {ring.Get()};
    const UINT fpFirst[1] = {16}, worldFirst[1] = {64}, count[1] = {32};
    std::vector<std::uint8_t> got;
    bool ok = true;
    // The first-person bind: our buffer at 0, with the render's projection and the view 10 cm lower.
    context1->VSSetConstantBuffers1(2, 1, bind, fpFirst, count);
    ID3D11Buffer* bound = nullptr;
    UINT boundFirst = 99, boundCount = 0;
    context1->VSGetConstantBuffers1(2, 1, &bound, &boundFirst, &boundCount);
    if (!bound || bound == ring.Get() || boundFirst != 0) ok = (why = "first-person bind not replaced", false);
    else if (!ReadBuffer(device, context.Get(), bound, got)) ok = (why = "read our buffer back", false);
    else if (std::fabs(FloatAt(got, L::kProjMatrix + 5 * 4) - renderKy) > 1e-5f ||
             std::fabs(FloatAt(got, L::kProjMatrix) - renderKy / aspect) > 1e-5f)
        ok = (why = "projection not the render's", false);
    else if (std::fabs(FloatAt(got, L::kViewMatrix + 7 * 4) - (-2.2f)) > 1e-5f)
        ok = (why = "view not moved 10 cm (camera space)", false);
    else if (std::fabs(FloatAt(got, L::kCameraPos + 4) - 2.2f) > 1e-5f || std::fabs(FloatAt(got, L::kCameraPos) - 0.3f) > 1e-5f)
        ok = (why = "camera position not consistent", false);
    else if (std::fabs(FloatAt(got, L::kViewProjMatrix + 7 * 4) - renderKy * -2.2f) > 1e-4f)
        ok = (why = "viewProj not consistent", false);
    if (bound) bound->Release();
    // The world bind passes through as BF4 made it.
    if (ok) {
        context1->VSSetConstantBuffers1(2, 1, bind, worldFirst, count);
        bound = nullptr;
        context1->VSGetConstantBuffers1(2, 1, &bound, &boundFirst, &boundCount);
        if (bound != ring.Get() || boundFirst != 64) ok = (why = "world bind changed", false);
        if (bound) bound->Release();
    }
    // The pixel stage too.
    if (ok) {
        context1->PSSetConstantBuffers1(2, 1, bind, fpFirst, count);
        bound = nullptr;
        context1->PSGetConstantBuffers1(2, 1, &bound, &boundFirst, &boundCount);
        if (!bound || bound == ring.Get()) ok = (why = "pixel-stage bind not replaced", false);
        if (bound) bound->Release();
    }
    // Off: everything passes through.
    if (ok) {
        view.SetFrame({});
        context1->VSSetConstantBuffers1(2, 1, bind, fpFirst, count);
        bound = nullptr;
        context1->VSGetConstantBuffers1(2, 1, &bound, &boundFirst, &boundCount);
        if (bound != ring.Get() || boundFirst != 16) ok = (why = "replaced while off", false);
        if (bound) bound->Release();
    }
    const auto st = view.GetStats();
    if (ok && (st.matched != 2 || st.replaced != 2)) ok = (why = "match/replace counts", false);
    // Gun in the hand: the hand recorded with this frame's rotation (identity here) moves the
    // rig so BF4's grip-at-rest lands on it; a hand recorded with another rotation is not used.
    if (ok) {
        fvr::bf4::FirstPersonView::Frame hand = frame;
        hand.handMode = true;
        auto& h = hand.hands[0];
        // BF4's first-person view takes the game camera's rotation (identity here); the render's
        // is another (90 deg) and must not be the key.
        const float identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}, renderTurn[9] = {0, 0, -1, 0, 1, 0, 1, 0, 0};
        std::memcpy(h.gameRotation, identity, sizeof(identity));
        std::memcpy(h.rotation, renderTurn, sizeof(renderTurn));
        h.hand.position = {0.2f, -0.3f, -0.4f};
        h.hand.orientation = {0, 0, 0, 1};
        h.valid = true;
        hand.gripAtRest.position = {0.07f, -0.1f, -0.2f};
        hand.gripAtRest.orientation = {0, 0, 0, 1};
        view.SetFrame(hand);
        // A new frame: BF4 writes its ring again (switching off forgot the old addresses).
        if (FAILED(context->Map(ring.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return why = "map the ring again", false;
        MakeViewBlock(static_cast<std::uint8_t*>(m.pData) + 256, gameKy / aspect, gameKy, fpCam);
        MakeViewBlock(static_cast<std::uint8_t*>(m.pData) + 1024, renderKy / aspect, renderKy, worldCam);
        // Two rigid meshes near the first-person camera: the larger one is the receiver.
        const fvr::bf4::Mat4 receiver{1, 0, 0, 0.3f, 0, 1, 0, 2.0f, 0, 0, 1, 1.2f, 0, 0, 0, 1};
        const fvr::bf4::Mat4 sight{1, 0, 0, 0.3f, 0, 1, 0, 2.1f, 0, 0, 1, 1.0f, 0, 0, 0, 1};
        std::memcpy(static_cast<std::uint8_t*>(m.pData) + 1536, receiver.data(), 64);
        std::memcpy(static_cast<std::uint8_t*>(m.pData) + 1792, sight.data(), 64);
        context->Unmap(ring.Get(), 0);
        context1->VSSetConstantBuffers1(2, 1, bind, fpFirst, count);
        bound = nullptr;
        context1->VSGetConstantBuffers1(2, 1, &bound, &boundFirst, &boundCount);
        const auto rest = hand.gripAtRest.position;
        if (!bound || bound == ring.Get()) ok = (why = "hand mode: not replaced", false);
        else if (!ReadBuffer(device, context.Get(), bound, got)) ok = (why = "hand mode: read back", false);
        else if (std::fabs(FloatAt(got, L::kViewMatrix + 3 * 4) - (-0.3f + 0.2f - rest.x)) > 1e-5f ||
                 std::fabs(FloatAt(got, L::kViewMatrix + 7 * 4) - (-2.1f - 0.3f - rest.y)) > 1e-5f ||
                 std::fabs(FloatAt(got, L::kViewMatrix + 11 * 4) - (-0.9f - 0.4f - rest.z)) > 1e-5f)
            ok = (why = "hand mode: the grip at rest not moved onto the hand", false);
        if (bound) bound->Release();
        const float turned[9] = {0, 0, -1, 0, 1, 0, 1, 0, 0}; // another frame's game rotation (90 deg)
        std::memcpy(h.gameRotation, turned, sizeof(turned));
        std::memcpy(h.rotation, identity, sizeof(identity)); // even with the render's matching
        view.SetFrame(hand);
        if (ok) {
            context1->VSSetConstantBuffers1(2, 1, bind, fpFirst, count);
            bound = nullptr;
            context1->VSGetConstantBuffers1(2, 1, &bound, &boundFirst, &boundCount);
            if (bound != ring.Get()) ok = (why = "hand mode: another frame's hand used", false);
            if (bound) bound->Release();
        }
        const auto hs = view.GetStats();
        if (ok && hs.handMatched != 1) ok = (why = "hand mode: match counts", false);
        // The pass (begun by the matched bind above) draws the sight (300 indices) and the
        // receiver (900); the world's view ends it: the receiver, in camera space, is the weapon.
        if (ok) {
            std::memcpy(h.gameRotation, identity, sizeof(identity)); // this frame's again: a pass begins
            view.SetFrame(hand);
            context1->VSSetConstantBuffers1(2, 1, bind, fpFirst, count);
            const UINT receiverFirst[1] = {96}, sightFirst[1] = {112}, sixteen[1] = {16};
            context1->VSSetConstantBuffers1(0, 1, bind, sightFirst, sixteen);
            context->Draw(300, 0);
            context1->VSSetConstantBuffers1(0, 1, bind, receiverFirst, sixteen);
            context->Draw(900, 0);
            context1->VSSetConstantBuffers1(2, 1, bind, worldFirst, count);
            const auto ws = view.GetStats();
            if (ws.weaponFound != 1 || std::fabs(ws.weapon[0]) > 1e-5f || std::fabs(ws.weapon[1] + 0.1f) > 1e-5f ||
                std::fabs(ws.weapon[2] - 0.3f) > 1e-5f)
                ok = (why = "receiver tracking: the largest rigid mesh in camera space", false);
            ID3D11Buffer* none0[1]{};
            context->VSSetConstantBuffers(0, 1, none0);
        }
    }
    {
        const float camera[3] = {0.3f, 2.1f, 0.9f};
        const fvr::bf4::Mat4 close{1, 0, 0, 0.3f, 0, 1, 0, 2.0f, 0, 0, 1, 1.2f, 0, 0, 0, 1};
        fvr::bf4::Mat4 distant = close, scaled = close, projective = close;
        distant[11] = 9.0f;
        scaled[0] = 2.0f;
        projective[14] = -1.0f;
        if (ok && (!fvr::bf4::FirstPersonView::WeaponPart(close, camera) || fvr::bf4::FirstPersonView::WeaponPart(distant, camera) ||
                   fvr::bf4::FirstPersonView::WeaponPart(scaled, camera) ||
                   fvr::bf4::FirstPersonView::WeaponPart(projective, camera)))
            ok = (why = "weapon part test", false);
    }
    // Alternate eyes: this frame's main view goes to one eye (the left here: the camera 32 mm
    // left), the first-person pass with it; after the Present the next frame's to the other.
    if (ok) {
        using FPV = fvr::bf4::FirstPersonView;
        FPV::Frame eyes = frame;
        eyes.transform = fvr::bf4::Identity4();
        eyes.handMode = false;
        eyes.alternateEyes = true;
        eyes.halfEye = 0.032f;
        view.OnPresent(); // a frame boundary: nothing moved yet, the eye stays
        view.SetFrame(eyes);
        auto worldViewX = [&](float& viewX, float& cameraX) {
            if (FAILED(context->Map(ring.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return false;
            MakeViewBlock(static_cast<std::uint8_t*>(m.pData) + 256, gameKy / aspect, gameKy, fpCam);
            MakeViewBlock(static_cast<std::uint8_t*>(m.pData) + 1024, renderKy / aspect, renderKy, worldCam);
            context->Unmap(ring.Get(), 0);
            context1->VSSetConstantBuffers1(2, 1, bind, worldFirst, count);
            ID3D11Buffer* b = nullptr;
            UINT f = 0, c = 0;
            context1->VSGetConstantBuffers1(2, 1, &b, &f, &c);
            const bool read = b && b != ring.Get() && ReadBuffer(device, context.Get(), b, got);
            if (b) b->Release();
            if (!read) return false;
            viewX = FloatAt(got, L::kViewMatrix + 3 * 4);
            cameraX = FloatAt(got, L::kCameraPos);
            return std::fabs(FloatAt(got, L::kProjMatrix + 5 * 4) - renderKy) < 1e-6f; // its own projection kept
        };
        float viewX = 0, cameraX = 0;
        if (view.FrameEye() != -1) ok = (why = "alternate eyes: an eye reported before any view moved", false);
        else if (!worldViewX(viewX, cameraX)) ok = (why = "alternate eyes: the main view not moved", false);
        else if (std::fabs(cameraX - (worldCam[0] - 0.032f)) > 1e-4f || std::fabs(viewX - (600.0f + 0.032f)) > 1e-4f)
            ok = (why = "alternate eyes: the main view not at the left eye", false);
        else if (view.FrameEye() != 0) ok = (why = "alternate eyes: the frame's eye not reported", false);
        if (ok) {
            // The first-person pass is BF4's in this mode (no hand): passes through.
            context1->VSSetConstantBuffers1(2, 1, bind, fpFirst, count);
            ID3D11Buffer* b = nullptr;
            UINT f = 0, c = 0;
            context1->VSGetConstantBuffers1(2, 1, &b, &f, &c);
            if (b != ring.Get()) ok = (why = "alternate eyes: BF4's first-person pass changed without the hand", false);
            if (b) b->Release();
        }
        if (ok) {
            view.OnPresent();
            if (!worldViewX(viewX, cameraX) || std::fabs(cameraX - (worldCam[0] + 0.032f)) > 1e-4f || view.FrameEye() != 1)
                ok = (why = "alternate eyes: the next frame not at the right eye", false);
        }
        if (ok) { // with the hand: the rig is seen from this frame's eye as well
            view.OnPresent(); // the left eye again
            FPV::Frame both = eyes;
            both.handMode = true;
            auto& h = both.hands[0];
            const float identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
            std::memcpy(h.gameRotation, identity, sizeof(identity));
            h.hand.position = {0.2f, -0.3f, -0.4f};
            h.hand.orientation = {0, 0, 0, 1};
            h.valid = true;
            both.gripAtRest.position = {0.07f, -0.1f, -0.2f};
            both.gripAtRest.orientation = {0, 0, 0, 1};
            view.SetFrame(both);
            if (FAILED(context->Map(ring.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return why = "map the ring (eyes)", false;
            MakeViewBlock(static_cast<std::uint8_t*>(m.pData) + 256, gameKy / aspect, gameKy, fpCam);
            context->Unmap(ring.Get(), 0);
            context1->VSSetConstantBuffers1(2, 1, bind, fpFirst, count);
            ID3D11Buffer* b = nullptr;
            UINT f = 0, c = 0;
            context1->VSGetConstantBuffers1(2, 1, &b, &f, &c);
            // Without the eye: -0.3 + 0.2 - 0.07 (as in hand mode above); from the left eye 32 mm more.
            if (!b || b == ring.Get() || !ReadBuffer(device, context.Get(), b, got) ||
                std::fabs(FloatAt(got, L::kViewMatrix + 3 * 4) - (-0.3f + 0.2f - 0.07f + 0.032f)) > 1e-5f)
                ok = (why = "alternate eyes: the hand-held rig not seen from the frame's eye", false);
            if (b) b->Release();
        }
        const auto es = view.GetStats();
        if (ok && (es.shiftedViews < 2 || es.shiftedFrames < 2)) ok = (why = "alternate eyes: counts", false);
        if (ok && (FPV::EyeShift(1, 0.03f)[3] != -0.03f || FPV::EyeShift(0, 0.03f)[3] != 0.03f))
            ok = (why = "alternate eyes: eye shift", false);
    }
    view.Remove();
    ID3D11Buffer* none[1]{};
    context->VSSetConstantBuffers(2, 1, none);
    context->PSSetConstantBuffers(2, 1, none);
    return ok;
}

namespace {
fvr::bf4::Mat4 RotationAbout(fvr::math::Vec3 axis, float radians) {
    fvr::math::Pose p;
    p.orientation = fvr::math::FromAxisAngle(axis, radians);
    return fvr::bf4::FromPose(p);
}
// A rigid S with S(j) = p, turned by r about j.
fvr::bf4::Mat4 About(fvr::math::Vec3 j, fvr::math::Vec3 p, const fvr::bf4::Mat4& r) {
    using namespace fvr::bf4;
    return Mul(Translation(p), Mul(r, Translation(j * -1.0f)));
}
// A rigid S with S(ja) = pa and S(jb) = pb, twisted about that segment.
fvr::bf4::Mat4 Segment(fvr::math::Vec3 ja, fvr::math::Vec3 jb, fvr::math::Vec3 pa, fvr::math::Vec3 pb, float twist) {
    using namespace fvr::bf4;
    return About(ja, pa, Mul(RotationAbout(pb - pa, twist), RotationBetween(jb - ja, pb - pa)));
}
fvr::math::Vec3 MapBone(const float* b, fvr::math::Vec3 p) {
    return {b[0] * p.x + b[1] * p.y + b[2] * p.z + b[3], b[4] * p.x + b[5] * p.y + b[6] * p.z + b[7],
            b[8] * p.x + b[9] * p.y + b[10] * p.z + b[11]};
}
} // namespace

// Arms from the body (Bf4ArmIk.h): in the first-person pass, a bone palette bound at b0 comes
// back as ours with each arm's shoulder where the body puts it and the wrist still on the gun.
bool CheckFirstPersonArms(ID3D11Device* device, const char*& why) {
    using namespace fvr::bf4;
    using fvr::math::Vec3;
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return why = "MinHook", false;
    ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(&context);
    ComPtr<ID3D11DeviceContext1> context1;
    if (FAILED(context.As(&context1))) return why = "no ID3D11DeviceContext1", false;
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = 4096;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ComPtr<ID3D11Buffer> ring;
    if (FAILED(device->CreateBuffer(&bd, nullptr, &ring))) return why = "ring buffer", false;
    const float gameKy = 1.0f / std::tan(0.785398f * 0.5f), renderKy = 0.6982f, aspect = 16.0f / 9.0f;
    const float fpCam[3] = {0.3f, 2.1f, 0.9f};
    // The arms posed in the pass's world: shoulders 20 cm below the first-person camera, elbows
    // bent, wrists in front (bones: clavicle, upper, upper twist, forearm, hand per arm; then
    // fillers up to 24 bones, a palette's size).
    const ArmBindJoints bind = Bf4ArmBindJoints();
    std::vector<float> palette;
    auto put = [&](const Mat4& m) { palette.insert(palette.end(), m.begin(), m.begin() + 12); };
    Vec3 s[2], e[2], w[2];
    for (int side = 0; side < 2; ++side) {
        const float x = side == 0 ? -1.0f : 1.0f;
        const float lu = fvr::math::Length(bind.elbow[side] - bind.shoulder[side]);
        const float lf = fvr::math::Length(bind.wrist[side] - bind.elbow[side]);
        s[side] = Vec3{fpCam[0], fpCam[1], fpCam[2]} + Vec3{0.12f * x, -0.2f, 0.05f};
        e[side] = s[side] + fvr::math::Normalize({0.1f * x, -0.6f, -0.8f}) * lu;
        w[side] = e[side] + fvr::math::Normalize({-0.3f * x, 0.2f, -0.93f}) * lf;
        put(About(bind.shoulder[side], s[side], RotationAbout({0, 0, 1}, 0.35f * x)));
        put(Segment(bind.shoulder[side], bind.elbow[side], s[side], e[side], 0.0f));
        put(Segment(bind.shoulder[side], bind.elbow[side], s[side], e[side], 0.5f));
        put(Segment(bind.elbow[side], bind.wrist[side], e[side], w[side], 0.2f));
        put(About(bind.wrist[side], w[side], RotationAbout({1, 0.3f, 0}, 0.6f)));
    }
    for (int k = 0; palette.size() < 24 * 12; ++k) put(Translation({0.01f * k, 5.0f, 0}));
    FirstPersonView view;
    if (!view.Install(context.Get())) return why = "install", false;
    FirstPersonView::Frame frame;
    frame.enabled = true;
    frame.gameFovY = 0.785398f;
    frame.renderKy = renderKy;
    frame.renderKx = renderKy / aspect;
    frame.handMode = true;
    frame.armIk = true;
    auto& h = frame.hands[0];
    const float identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::memcpy(h.gameRotation, identity, sizeof(identity));
    std::memcpy(h.rotation, identity, sizeof(identity)); // the head level, the body facing as it does
    frame.gripAtRest.position = {0.07f, -0.1f, -0.2f};
    frame.gripAtRest.orientation = {0, 0, 0, 1};
    h.hand.position = {0.12f, -0.1f, -0.2f}; // the gun 5 cm right of where BF4 holds it
    h.hand.orientation = {0, 0, 0, 1};
    h.valid = true;
    view.SetFrame(frame);
    D3D11_MAPPED_SUBRESOURCE m{}; // BF4 fills its ring (seen by the view once it is on)
    if (FAILED(context->Map(ring.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return why = "map the ring", false;
    MakeViewBlock(static_cast<std::uint8_t*>(m.pData) + 256, gameKy / aspect, gameKy, fpCam);
    std::memcpy(static_cast<std::uint8_t*>(m.pData) + 2048, palette.data(), palette.size() * sizeof(float));
    context->Unmap(ring.Get(), 0);
    ID3D11Buffer* bind1[1] = {ring.Get()};
    const UINT fpFirst[1] = {16}, count[1] = {32}, paletteFirst[1] = {128}, paletteCount[1] = {80};
    bool ok = true;
    context1->VSSetConstantBuffers1(2, 1, bind1, fpFirst, count); // the pass begins
    context1->VSSetConstantBuffers1(0, 1, bind1, paletteFirst, paletteCount);
    ID3D11Buffer* bound = nullptr;
    UINT boundFirst = 99, boundCount = 0;
    context1->VSGetConstantBuffers1(0, 1, &bound, &boundFirst, &boundCount);
    std::vector<std::uint8_t> got;
    if (!bound || bound == ring.Get() || boundFirst != 0) ok = (why = "palette not rebound", false);
    else if (!ReadBuffer(device, context.Get(), bound, got)) ok = (why = "read the palette back", false);
    if (bound) bound->Release();
    if (ok) {
        // Head space = move (5 cm right) x first-person view (camera at fpCam, unturned). The
        // body is the head level: the shoulders stay where BF4's view puts them (5 cm left of
        // the moved rig); the wrists move with the gun.
        const float* out = reinterpret_cast<const float*>(got.data());
        const Mat4 headFromPass = Mul(Translation({0.05f, 0, 0}), Translation({-fpCam[0], -fpCam[1], -fpCam[2]}));
        for (int side = 0; side < 2 && ok; ++side) {
            const float* b = out + side * 5 * 12;
            const Vec3 shoulder = TransformPoint(headFromPass, s[side]) - Vec3{0.05f, 0, 0};
            const Vec3 wrist = TransformPoint(headFromPass, w[side]);
            for (int bone : {0, 1, 2})
                if (fvr::math::Length(TransformPoint(headFromPass, MapBone(b + bone * 12, bind.shoulder[side])) - shoulder) > 1e-3f)
                    ok = (why = "a shoulder not at the body", false);
            if (ok && fvr::math::Length(TransformPoint(headFromPass, MapBone(b + 3 * 12, bind.wrist[side])) - wrist) > 1e-3f)
                ok = (why = "a forearm not reaching the wrist on the gun", false);
            if (ok && fvr::math::Length(MapBone(b + 1 * 12, bind.elbow[side]) - MapBone(b + 3 * 12, bind.elbow[side])) > 1e-3f)
                ok = (why = "an elbow not joined", false);
            if (ok && std::memcmp(b + 4 * 12, palette.data() + (side * 5 + 4) * 12, 12 * sizeof(float)) != 0)
                ok = (why = "a hand moved off the gun", false);
        }
        if (ok && std::memcmp(out + 10 * 12, palette.data() + 10 * 12, 14 * 12 * sizeof(float)) != 0)
            ok = (why = "other bones changed", false);
    }
    const auto st = view.GetStats();
    if (ok && (st.armPalettes != 1 || st.armRewritten != 1 || st.armBones != 8))
        ok = (why = "arms: palette counts", false);
    // A small b0 bind (a rigid mesh's world matrix) passes through.
    if (ok) {
        const UINT rigidFirst[1] = {96}, sixteen[1] = {16};
        context1->VSSetConstantBuffers1(0, 1, bind1, rigidFirst, sixteen);
        bound = nullptr;
        context1->VSGetConstantBuffers1(0, 1, &bound, &boundFirst, &boundCount);
        if (bound != ring.Get() || boundFirst != 96) ok = (why = "a rigid mesh's bind changed", false);
        if (bound) bound->Release();
    }
    // Option off: the palette passes through.
    if (ok) {
        frame.armIk = false;
        view.SetFrame(frame);
        if (FAILED(context->Map(ring.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return why = "map the ring again", false;
        MakeViewBlock(static_cast<std::uint8_t*>(m.pData) + 256, gameKy / aspect, gameKy, fpCam);
        std::memcpy(static_cast<std::uint8_t*>(m.pData) + 2048, palette.data(), palette.size() * sizeof(float));
        context->Unmap(ring.Get(), 0);
        context1->VSSetConstantBuffers1(2, 1, bind1, fpFirst, count);
        context1->VSSetConstantBuffers1(0, 1, bind1, paletteFirst, paletteCount);
        bound = nullptr;
        context1->VSGetConstantBuffers1(0, 1, &bound, &boundFirst, &boundCount);
        if (bound != ring.Get() || boundFirst != 128) ok = (why = "palette rebound with the option off", false);
        if (bound) bound->Release();
    }
    // The body from the head's axes: level, the head's heading.
    if (ok) {
        const float down[9] = {1, 0, 0, 0, 0.7071068f, -0.7071068f, 0, 0.7071068f, 0.7071068f}; // looking 45 deg down
        const Mat4 b = FirstPersonView::BodyInHead(down);
        const Vec3 bodyUp = TransformVector(b, {0, 1, 0}), bodyBack = TransformVector(b, {0, 0, 1});
        if (std::fabs(bodyUp.y - 0.7071068f) > 1e-4f || std::fabs(bodyUp.z - 0.7071068f) > 1e-4f ||
            std::fabs(bodyBack.y + 0.7071068f) > 1e-4f || std::fabs(bodyBack.x) > 1e-4f)
            ok = (why = "body frame from a lowered head", false);
    }
    view.Remove();
    ID3D11Buffer* none[1]{};
    context->VSSetConstantBuffers(0, 1, none);
    context->VSSetConstantBuffers(2, 1, none);
    return ok;
}

// The grip from BF4's right hand: a pass whose arms palette holds the right hand bone; the next
// first-person view is moved so that hand x GripInRightHandBind lands on the VR hand.
bool CheckFirstPersonAnchor(ID3D11Device* device, const char*& why) {
    using namespace fvr::bf4;
    using fvr::math::Vec3;
    using L = ViewConstantsLayout;
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return why = "MinHook", false;
    ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(&context);
    ComPtr<ID3D11DeviceContext1> context1;
    if (FAILED(context.As(&context1))) return why = "no ID3D11DeviceContext1", false;
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = 4096;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ComPtr<ID3D11Buffer> ring;
    if (FAILED(device->CreateBuffer(&bd, nullptr, &ring))) return why = "ring buffer", false;
    const float gameKy = 1.0f / std::tan(0.785398f * 0.5f), renderKy = 0.6982f, aspect = 16.0f / 9.0f;
    const float fpCam[3] = {0.3f, 2.1f, 0.9f}, worldCam[3] = {-600, 383, -273};
    // The arms posed by the camera (as in CheckFirstPersonArms); the right hand is bone 9.
    const ArmBindJoints bind = Bf4ArmBindJoints();
    std::vector<float> palette;
    auto put = [&](const Mat4& m) { palette.insert(palette.end(), m.begin(), m.begin() + 12); };
    Mat4 rightHand{};
    for (int side = 0; side < 2; ++side) {
        const float x = side == 0 ? -1.0f : 1.0f;
        const float lu = fvr::math::Length(bind.elbow[side] - bind.shoulder[side]);
        const float lf = fvr::math::Length(bind.wrist[side] - bind.elbow[side]);
        const Vec3 s = Vec3{fpCam[0], fpCam[1], fpCam[2]} + Vec3{0.12f * x, -0.2f, 0.05f};
        const Vec3 e = s + fvr::math::Normalize({0.1f * x, -0.6f, -0.8f}) * lu;
        const Vec3 w = e + fvr::math::Normalize({-0.3f * x, 0.2f, -0.93f}) * lf;
        put(About(bind.shoulder[side], s, RotationAbout({0, 0, 1}, 0.35f * x)));
        put(Segment(bind.shoulder[side], bind.elbow[side], s, e, 0.0f));
        put(Segment(bind.shoulder[side], bind.elbow[side], s, e, 0.5f));
        put(Segment(bind.elbow[side], bind.wrist[side], e, w, 0.2f));
        const Mat4 hand = About(bind.wrist[side], w, RotationAbout({1, 0.3f, 0}, 0.6f));
        put(hand);
        if (side == 1) rightHand = hand;
    }
    for (int k = 0; palette.size() < 24 * 12; ++k) put(Translation({0.01f * k, 5.0f, 0}));
    FirstPersonView view;
    if (!view.Install(context.Get())) return why = "install", false;
    FirstPersonView::Frame frame;
    frame.enabled = true;
    frame.gameFovY = 0.785398f;
    frame.renderKy = renderKy;
    frame.renderKx = renderKy / aspect;
    frame.handMode = true;
    frame.armIk = false; // the grip comes from the hand with or without the arms
    auto& h = frame.hands[0];
    const float identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::memcpy(h.gameRotation, identity, sizeof(identity));
    std::memcpy(h.rotation, identity, sizeof(identity));
    h.hand.position = {0.15f, -0.2f, -0.3f};
    h.hand.orientation = fvr::math::FromAxisAngle({0, 1, 0}, 0.3f);
    h.valid = true;
    view.SetFrame(frame);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(context->Map(ring.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return why = "map the ring", false;
    MakeViewBlock(static_cast<std::uint8_t*>(m.pData) + 256, gameKy / aspect, gameKy, fpCam);
    MakeViewBlock(static_cast<std::uint8_t*>(m.pData) + 1024, renderKy / aspect, renderKy, worldCam);
    std::memcpy(static_cast<std::uint8_t*>(m.pData) + 2048, palette.data(), palette.size() * sizeof(float));
    context->Unmap(ring.Get(), 0);
    ID3D11Buffer* bind1[1] = {ring.Get()};
    const UINT fpFirst[1] = {16}, worldFirst[1] = {64}, count[1] = {32}, paletteFirst[1] = {128}, paletteCount[1] = {80};
    context1->VSSetConstantBuffers1(2, 1, bind1, fpFirst, count);           // a pass: the grip at rest
    context1->VSSetConstantBuffers1(0, 1, bind1, paletteFirst, paletteCount); // its arms: the right hand
    context1->VSSetConstantBuffers1(2, 1, bind1, worldFirst, count);        // the world: the pass ends
    context1->VSSetConstantBuffers1(2, 1, bind1, fpFirst, count);           // the next pass
    ID3D11Buffer* bound = nullptr;
    UINT boundFirst = 99, boundCount = 0;
    context1->VSGetConstantBuffers1(2, 1, &bound, &boundFirst, &boundCount);
    std::vector<std::uint8_t> got;
    bool ok = true;
    if (!bound || bound == ring.Get()) ok = (why = "first-person view not replaced", false);
    else if (!ReadBuffer(device, context.Get(), bound, got)) ok = (why = "read back", false);
    if (bound) bound->Release();
    if (ok) {
        // Expected: move = hand x (view x right hand x grip-in-hand)^-1, drawn as move x view.
        const Mat4 fpView{1, 0, 0, -fpCam[0], 0, 1, 0, -fpCam[1], 0, 0, 1, -fpCam[2], 0, 0, 0, 1};
        const Mat4 grip = Mul(Mul(fpView, rightHand), GripInRightHandBind());
        const Mat4 expected = Mul(Mul(FromPose(h.hand), InverseRigid(grip)), fpView);
        float worst = 0;
        for (int k = 0; k < 12; ++k) {
            float v = 0;
            std::memcpy(&v, got.data() + L::kViewMatrix + k * 4, 4);
            worst = std::fmax(worst, std::fabs(v - expected[k]));
        }
        if (worst > 1e-4f) ok = (why = "the view does not hold the hand's grip", false);
    }
    const auto st = view.GetStats();
    if (ok && (st.anchorFound != 1 || st.anchorUsed != 1)) ok = (why = "anchor counts", false);
    view.Remove();
    ID3D11Buffer* none[1]{};
    context->VSSetConstantBuffers(0, 1, none);
    context->VSSetConstantBuffers(2, 1, none);
    return ok;
}

bool CheckHudSplit(ID3D11Device* device, const char*& why) {
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return why = "MinHook", false;
    ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(&context);
    auto texture = [&](UINT w, UINT h, UINT bind, ComPtr<ID3D11Texture2D>& t) {
        D3D11_TEXTURE2D_DESC d{};
        d.Width = w;
        d.Height = h;
        d.MipLevels = d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = bind;
        return SUCCEEDED(device->CreateTexture2D(&d, nullptr, &t));
    };
    // The swapchain's buffer (100x100), the scene's render target and a UI atlas (no RT bind).
    ComPtr<ID3D11Texture2D> back, scene, atlas;
    if (!texture(100, 100, D3D11_BIND_RENDER_TARGET, back) ||
        !texture(140, 140, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, scene) ||
        !texture(64, 64, D3D11_BIND_SHADER_RESOURCE, atlas))
        return why = "textures", false;
    ComPtr<ID3D11RenderTargetView> backView;
    ComPtr<ID3D11ShaderResourceView> sceneView, atlasView;
    if (FAILED(device->CreateRenderTargetView(back.Get(), nullptr, &backView)) ||
        FAILED(device->CreateShaderResourceView(scene.Get(), nullptr, &sceneView)) ||
        FAILED(device->CreateShaderResourceView(atlas.Get(), nullptr, &atlasView)))
        return why = "views", false;
    bool ok = true;
    {
        fvr::bf4::HudSplit hud;
        if (!hud.Install(context.Get())) return why = "install", false;
        hud.OnPresent(back.Get(), false); // the previous frame: learns the back buffer
        const float blue[4] = {0, 0, 1, 1}, red[4] = {1, 0, 0, 1};
        ID3D11RenderTargetView* target[1] = {backView.Get()};
        context->OMSetRenderTargets(1, target, nullptr);
        context->ClearRenderTargetView(backView.Get(), blue); // what the composite leaves: the scene
        ID3D11ShaderResourceView* srv[1] = {sceneView.Get()};
        context->PSSetShaderResources(0, 1, srv);
        context->Draw(3, 0); // the composite (reads a render target)
        srv[0] = atlasView.Get();
        context->PSSetShaderResources(0, 1, srv);
        context->Draw(3, 0); // the HUD's first draw: the scene is copied before it
        context->ClearRenderTargetView(backView.Get(), red); // the HUD (everywhere, here)
        // The mask back from the scene: a 20x20 centre square, bands 60 long and 6 thick.
        hud.OnPresent(back.Get(), true, fvr::bf4::HudSplit::CentreMask{0.2f, 0.6f, 0.06f});
        const auto st = hud.GetStats();
        if (st.copies != 1 || st.restores != 1) ok = (why = "copy/restore counts", false);
        ID3D11ShaderResourceView* none[1]{};
        context->PSSetShaderResources(0, 1, none);
        ID3D11RenderTargetView* noTarget[1]{};
        context->OMSetRenderTargets(1, noTarget, nullptr);
        hud.Remove();
    }
    if (ok) {
        D3D11_TEXTURE2D_DESC d{};
        back->GetDesc(&d);
        d.Usage = D3D11_USAGE_STAGING;
        d.BindFlags = 0;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(device->CreateTexture2D(&d, nullptr, &staging))) return why = "staging", false;
        context->CopyResource(staging.Get(), back.Get());
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return why = "map", false;
        auto pixel = [&](int x, int y) { return static_cast<const std::uint32_t*>(m.pData)[y * (m.RowPitch / 4) + x]; };
        const std::uint32_t centre = pixel(50, 50), edge = pixel(41, 58), armX = pixel(22, 50), armY = pixel(50, 78),
                            outside = pixel(5, 5), offAxis = pixel(25, 44), corner = pixel(39, 39);
        context->Unmap(staging.Get(), 0);
        if (centre != 0xFFFF0000u || edge != 0xFFFF0000u) ok = (why = "the centre square is not the scene", false); // blue (ABGR)
        else if (armX != 0xFFFF0000u || armY != 0xFFFF0000u) ok = (why = "the crosshair's arms are not the scene", false);
        else if (outside != 0xFF0000FFu || offAxis != 0xFF0000FFu || corner != 0xFF0000FFu)
            ok = (why = "outside the mask changed", false); // red
    }
    return ok;
}
