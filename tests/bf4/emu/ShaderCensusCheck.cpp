// The developer shader census on the emulator's device: the CreateVertexShader observer sees
// a shader created after it is installed and names its constant buffer and variables. And the
// draw probe: one frame's draw with its bone palette and camera position read back.
#include "../../../src/games/bf4/Bf4DrawProbe.h"
#include "../../../src/games/bf4/Bf4FirstPersonView.h"
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
    view.Remove();
    ID3D11Buffer* none[1]{};
    context->VSSetConstantBuffers(2, 1, none);
    context->PSSetConstantBuffers(2, 1, none);
    return ok;
}
