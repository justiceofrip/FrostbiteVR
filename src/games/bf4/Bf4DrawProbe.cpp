#include "Bf4DrawProbe.h"

#include "Bf4ShaderCensus.h"

#include <wrl/client.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace fvr::bf4 {

std::atomic<int> DrawProbe::state{static_cast<int>(DrawProbe::State::Idle)};

namespace {
using Microsoft::WRL::ComPtr;

// Mapped upload memory read after the fact: guard against a pointer the driver retired.
bool SafeCopy(void* dst, const void* src, std::size_t n) {
    __try {
        std::memcpy(dst, src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ConstantBuffer(ID3D11Resource* resource, D3D11_BUFFER_DESC& desc) {
    if (!resource) return false;
    D3D11_RESOURCE_DIMENSION dim{};
    resource->GetType(&dim);
    if (dim != D3D11_RESOURCE_DIMENSION_BUFFER) return false;
    static_cast<ID3D11Buffer*>(resource)->GetDesc(&desc);
    return (desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0;
}

// "name@offset:size" inside a layout text; false when absent.
bool Field(const std::string& layout, const char* name, std::uint32_t& offset, std::uint32_t& size) {
    const std::string key = std::string(name) + "@";
    std::size_t at = 0;
    while ((at = layout.find(key, at)) != std::string::npos) {
        if (at == 0 || layout[at - 1] == ' ' || layout[at - 1] == '{') {
            unsigned o = 0, s = 0;
            if (std::sscanf(layout.c_str() + at + key.size(), "%u:%u", &o, &s) == 2) {
                offset = o;
                size = s;
                return true;
            }
        }
        at += key.size();
    }
    return false;
}

float Distance(const float* a, const float* b) {
    const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return std::sqrt(x * x + y * y + z * z);
}
} // namespace

DrawProbe::~DrawProbe() { Remove(); }

bool DrawProbe::Install(ID3D11DeviceContext* immediate, const ShaderCensus* census) {
    if (installed_) return true;
    census_ = census;
    if (!ContextHooks::Install(immediate) || !ContextHooks::Add(this)) return false;
    installed_ = true;
    return true;
}

void DrawProbe::Remove() {
    state.store(static_cast<int>(State::Idle), std::memory_order_release);
    if (installed_) ContextHooks::Remove(this);
    installed_ = false;
}

void DrawProbe::Start(std::uint32_t maxDraws) {
    std::lock_guard<std::mutex> lock(mutex_);
    maxDraws_ = maxDraws;
    ready_ = false;
    report_.clear();
    state.store(static_cast<int>(State::Armed), std::memory_order_release);
}

bool DrawProbe::OnPresent() {
    const int s = state.load(std::memory_order_acquire);
    if (s == static_cast<int>(State::Armed)) {
        std::lock_guard<std::mutex> lock(mutex_);
        draws_ = 0;
        contents_.clear();
        contexts_.clear();
        targets_.clear();
        cameraPos_.clear();
        perLayout_.clear();
        lines_.clear();
        state.store(static_cast<int>(State::Capturing), std::memory_order_release);
        return false;
    }
    if (s != static_cast<int>(State::Capturing)) return false;
    state.store(static_cast<int>(State::Idle), std::memory_order_release);
    std::lock_guard<std::mutex> lock(mutex_);
    char head[160];
    std::snprintf(head, sizeof(head), "draws=%u contexts=%zu (c0 = the first context seen drawing)\n== draws per layout\n",
                  draws_, contexts_.size());
    report_ = head;
    for (const auto& [layout, n] : perLayout_) {
        report_ += "L" + std::to_string(layout) + " " + std::to_string(n);
        if (census_ && layout >= 0) report_ += " " + census_->LayoutText(layout).substr(0, 400);
        report_ += "\n";
    }
    report_ += "== draws\n" + lines_;
    lines_.clear();
    contents_.clear();
    ready_ = true;
    return true;
}

std::string DrawProbe::TakeReport() {
    std::lock_guard<std::mutex> lock(mutex_);
    ready_ = false;
    return std::move(report_);
}

int DrawProbe::ContextId(ID3D11DeviceContext* context) {
    const auto it = contexts_.find(context);
    if (it != contexts_.end()) return it->second;
    const int id = static_cast<int>(contexts_.size());
    contexts_[context] = id;
    return id;
}

bool DrawProbe::Capturing() { return state.load(std::memory_order_acquire) == static_cast<int>(State::Capturing); }

void DrawProbe::OnMap(ID3D11DeviceContext* context, ID3D11Resource* resource, D3D11_MAP type, void* data) {
    if (!Capturing()) return;
    D3D11_BUFFER_DESC d{};
    if (!ConstantBuffer(resource, d)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    Content& c = contents_[{context, resource}];
    c.mapped = data;
    c.size = d.ByteWidth;
    c.how = type == D3D11_MAP_WRITE_DISCARD ? "discard" : type == D3D11_MAP_WRITE_NO_OVERWRITE ? "nooverwrite" : "map";
    c.copy.clear();
}

void DrawProbe::OnUnmap(ID3D11DeviceContext* context, ID3D11Resource* resource) {
    if (!Capturing()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = contents_.find({context, resource});
    if (it == contents_.end() || !it->second.mapped || it->second.size > 65536) return;
    it->second.copy.resize(it->second.size);
    if (!SafeCopy(it->second.copy.data(), it->second.mapped, it->second.size)) it->second.copy.clear();
}

void DrawProbe::OnUpdate(ID3D11DeviceContext* context, ID3D11Resource* resource, const D3D11_BOX* box, const void* source) {
    if (!Capturing()) return;
    D3D11_BUFFER_DESC d{};
    if (!source || !ConstantBuffer(resource, d) || d.ByteWidth > 65536) return;
    std::lock_guard<std::mutex> lock(mutex_);
    Content& c = contents_[{context, resource}];
    c.mapped = nullptr;
    c.size = d.ByteWidth;
    c.how = box ? "update-box" : "update";
    c.copy.resize(d.ByteWidth);
    const std::uint32_t left = box ? std::min<std::uint32_t>(box->left, d.ByteWidth) : 0;
    const std::uint32_t right = box ? std::min<std::uint32_t>(box->right, d.ByteWidth) : d.ByteWidth;
    if (right > left && !SafeCopy(c.copy.data() + left, source, right - left)) c.copy.clear();
}

bool DrawProbe::Bytes(ID3D11DeviceContext* context, ID3D11Buffer* buffer, std::uint32_t offset, std::uint32_t size,
                      std::vector<std::uint8_t>& out) {
    auto it = contents_.find({context, buffer});
    if (it == contents_.end()) { // filled on another context (e.g. the immediate one)
        for (auto i = contents_.begin(); i != contents_.end(); ++i)
            if (i->first.second == buffer) it = i;
    }
    if (it == contents_.end()) return false;
    const Content& c = it->second;
    if (offset >= c.size) return false;
    size = std::min(size, c.size - offset);
    out.resize(size);
    if (!c.copy.empty()) {
        std::memcpy(out.data(), c.copy.data() + offset, size);
        return true;
    }
    return c.mapped && SafeCopy(out.data(), static_cast<const std::uint8_t*>(c.mapped) + offset, size);
}

std::string DrawProbe::Target(ID3D11View* view) {
    if (!view) return "-";
    const auto it = targets_.find(view);
    if (it != targets_.end()) return it->second;
    std::string text = "?";
    ComPtr<ID3D11Resource> r;
    view->GetResource(&r);
    ComPtr<ID3D11Texture2D> t;
    if (r && SUCCEEDED(r.As(&t))) {
        D3D11_TEXTURE2D_DESC d{};
        t->GetDesc(&d);
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%ux%u/%u%s@%llx", d.Width, d.Height, static_cast<unsigned>(d.Format),
                      d.SampleDesc.Count > 1 ? "ms" : "",
                      static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(t.Get()) & 0xFFFFFFull));
        text = buf;
    }
    targets_[view] = text;
    return text;
}

void DrawProbe::OnDraw(ID3D11DeviceContext* context, const char* kind, UINT count, UINT instances) {
    if (!Capturing()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (draws_ >= maxDraws_) return;
    const std::uint32_t seq = draws_++;
    const int ctx = ContextId(context);
    ID3D11VertexShader* vs = nullptr;
    context->VSGetShader(&vs, nullptr, nullptr);
    const int layout = census_ && vs ? census_->LayoutOf(vs) : -1;
    if (vs) vs->Release();
    ++perLayout_[layout];
    ID3D11Buffer* cbs[3]{};
    UINT first[3]{}, num[3]{};
    ComPtr<ID3D11DeviceContext1> c1;
    if (SUCCEEDED(context->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(c1.GetAddressOf()))) && c1)
        c1->VSGetConstantBuffers1(0, 3, cbs, first, num);
    else
        context->VSGetConstantBuffers(0, 3, cbs);
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    context->OMGetRenderTargets(1, &rtv, &dsv);
    std::string line;
    char tmp[256];
    auto add = [&](int k) {
        if (k > 0) line.append(tmp, std::min<std::size_t>(static_cast<std::size_t>(k), sizeof(tmp) - 1));
    };
    add(std::snprintf(tmp, sizeof(tmp), "%u c%d %s n=%u i=%u L%d rt=%s ds=%s", seq, ctx, kind, count, instances, layout,
                      Target(rtv).c_str(), Target(dsv).c_str()));
    if (rtv) rtv->Release();
    if (dsv) dsv->Release();
    {
        ID3D11ShaderResourceView* srvs[4]{};
        context->PSGetShaderResources(0, 4, srvs);
        bool any = false;
        for (int k = 0; k < 4; ++k) {
            if (!srvs[k]) continue;
            if (!any) line += " ps=[";
            any = true;
            add(std::snprintf(tmp, sizeof(tmp), "%d:%s ", k, Target(srvs[k]).c_str()));
            srvs[k]->Release();
        }
        if (any) line += "]";
    }
    const std::string text = census_ && layout >= 0 ? census_->LayoutText(layout) : std::string();
    std::vector<std::uint8_t> bytes;
    // viewConstants (b2): the camera position the draw is rendered from.
    std::uint32_t off = 0, size = 0;
    if (cbs[2] && Field(text, "cameraPos", off, size) &&
        Bytes(context, cbs[2], first[2] * 16 + off, 12, bytes) && bytes.size() == 12) {
        std::vector<float> cam(3);
        std::memcpy(cam.data(), bytes.data(), 12);
        auto& last = cameraPos_[context];
        if (last != cam) {
            add(std::snprintf(tmp, sizeof(tmp), " cam=(%.2f %.2f %.2f)", cam[0], cam[1], cam[2]));
            last = cam;
            std::vector<std::uint8_t> block;
            D3D11_BUFFER_DESC bd{};
            cbs[2]->GetDesc(&bd);
            const auto it = contents_.find({context, cbs[2]});
            add(std::snprintf(tmp, sizeof(tmp), " b2=%p+%u/%u how=%s", static_cast<void*>(cbs[2]), first[2] * 16, bd.ByteWidth,
                              it != contents_.end() ? it->second.how : "?"));
            if (Bytes(context, cbs[2], first[2] * 16, 368, block) && block.size() == 368) {
                const float* v = reinterpret_cast<const float*>(block.data());
                line += " viewConstants=[";
                for (int k = 0; k < 92; ++k) add(std::snprintf(tmp, sizeof(tmp), k ? " %.5g" : "%.5g", v[k]));
                line += "]";
            }
        }
    }
    const auto camIt = cameraPos_.find(context);
    const float* cam = camIt != cameraPos_.end() && camIt->second.size() == 3 ? camIt->second.data() : nullptr;
    const float origin[3]{0, 0, 0};
    // functionConstants (b0): bone palette (3 float4 rows per bone, translation in .w) and/or
    // world matrix: how far from the camera (world space) or the origin (camera-relative).
    // Only the bound part of the palette is the draw's (BF4 binds fewer constants than the
    // 60-bone declaration; what lies past them belongs to other draws).
    const bool skinned = cbs[0] && Field(text, "boneVectors", off, size);
    if (skinned && num[0] && num[0] * 16 > off) size = std::min<std::uint32_t>(size, num[0] * 16 - off) / 48 * 48;
    if (skinned && size >= 48 && Bytes(context, cbs[0], first[0] * 16 + off, size, bytes) && bytes.size() >= 48) {
        const std::size_t bones = bytes.size() / 48;
        const float* f = reinterpret_cast<const float*>(bytes.data());
        float camMin = 1e30f, camMax = 0, orgMin = 1e30f, orgMax = 0;
        for (std::size_t b = 0; b < bones; ++b) {
            const float t[3]{f[b * 12 + 3], f[b * 12 + 7], f[b * 12 + 11]};
            const float o = Distance(t, origin);
            orgMin = std::min(orgMin, o);
            orgMax = std::max(orgMax, o);
            if (cam) {
                const float c = Distance(t, cam);
                camMin = std::min(camMin, c);
                camMax = std::max(camMax, c);
            }
        }
        add(std::snprintf(tmp, sizeof(tmp), " b0=%u@%u bones=%zu cam[%.2f..%.2f] org[%.2f..%.2f]", size, first[0] * 16 + off,
                          bones, cam ? camMin : -1.0f, cam ? camMax : -1.0f, orgMin, orgMax));
        if ((cam && camMax < 3.0f) || orgMax < 3.0f)
            add(std::snprintf(tmp, sizeof(tmp), " bone0=[%.3f %.3f %.3f %.3f|%.3f %.3f %.3f %.3f|%.3f %.3f %.3f %.3f]", f[0],
                              f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11]));
        if (cam && camMin < 1.0f) { // a first-person mesh: every bone's translation (its skeleton's pose)
            line += " bones_t=[";
            for (std::size_t b = 0; b < bones; ++b)
                add(std::snprintf(tmp, sizeof(tmp), b ? " %.3f,%.3f,%.3f" : "%.3f,%.3f,%.3f", f[b * 12 + 3], f[b * 12 + 7],
                                  f[b * 12 + 11]));
            line += "]";
            // ...and the whole palette (3 rows of 4 per bone), for the arms' skeleton offline.
            line += " palette=[";
            for (std::size_t k = 0; k < bones * 12; ++k) add(std::snprintf(tmp, sizeof(tmp), k ? " %.6g" : "%.6g", f[k]));
            line += "]";
        }
    } else if (cbs[0] && Field(text, "worldMatrix", off, size) && size == 64 &&
               Bytes(context, cbs[0], first[0] * 16 + off, 64, bytes) && bytes.size() == 64) {
        const float* m = reinterpret_cast<const float*>(bytes.data());
        const float rows[3]{m[3], m[7], m[11]}, cols[3]{m[12], m[13], m[14]};
        add(std::snprintf(tmp, sizeof(tmp), " world t_w=(%.2f %.2f %.2f) t_row3=(%.2f %.2f %.2f)", rows[0], rows[1], rows[2],
                          cols[0], cols[1], cols[2]));
        if (cam) add(std::snprintf(tmp, sizeof(tmp), " cam_dist=%.2f/%.2f", Distance(rows, cam), Distance(cols, cam)));
        if (cam && Distance(rows, cam) < 3.0f) // near the camera (first-person meshes): the whole matrix
            add(std::snprintf(tmp, sizeof(tmp), " m=[%.4f %.4f %.4f %.4f|%.4f %.4f %.4f %.4f|%.4f %.4f %.4f %.4f|%.4f %.4f %.4f %.4f]",
                              m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14],
                              m[15]));
    }
    for (ID3D11Buffer* b : cbs)
        if (b) b->Release();
    lines_ += line;
    lines_ += '\n';
}

} // namespace fvr::bf4
