#include "Bf4ShaderCensus.h"

#include <MinHook.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <set>

namespace fvr::bf4 {

namespace {
using Microsoft::WRL::ComPtr;
using CreateVsFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*,
                                               ID3D11VertexShader**);
constexpr unsigned kCreateVertexShaderSlot = 12; // ID3D11Device::CreateVertexShader
constexpr std::size_t kMaxCodeBytes = 64u << 20; // bytecode kept for dumps, at most
// IID_ID3D11ShaderReflection (d3d11shader.h), defined here so no dxguid.lib is needed.
constexpr GUID kIidShaderReflection{0x8d536ca1, 0x0cca, 0x4956, {0xa8, 0x37, 0x78, 0x69, 0x63, 0x75, 0x55, 0x84}};
std::atomic<ShaderCensus*> g_census{nullptr};
CreateVsFn g_original = nullptr;

HRESULT STDMETHODCALLTYPE CreateVsHook(ID3D11Device* device, const void* code, SIZE_T size, ID3D11ClassLinkage* linkage,
                                       ID3D11VertexShader** shader) {
    const HRESULT hr = g_original(device, code, size, linkage, shader);
    if (SUCCEEDED(hr) && code && size >= 32)
        if (ShaderCensus* self = g_census.load(std::memory_order_acquire)) self->OnCreate(code, size, shader ? *shader : nullptr);
    return hr;
}

bool InD3D11(void* function) {
    HMODULE owner = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(function), &owner) &&
           owner == GetModuleHandleW(L"d3d11.dll");
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
} // namespace

ShaderCensus::~ShaderCensus() {
    ShaderCensus* expected = this;
    g_census.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
}

bool ShaderCensus::Install(ID3D11Device* device) {
    if (installed_) return true;
    if (!device || g_census.load(std::memory_order_acquire)) return false;
    void* entry = (*reinterpret_cast<void***>(device))[kCreateVertexShaderSlot];
    if (!entry || !InD3D11(entry)) return false; // someone else's function (an overlay): leave it alone
    g_census.store(this, std::memory_order_release);
    if (MH_CreateHook(entry, reinterpret_cast<LPVOID>(&CreateVsHook), reinterpret_cast<LPVOID*>(&g_original)) != MH_OK) {
        g_census.store(nullptr, std::memory_order_release);
        return false;
    }
    if (MH_EnableHook(entry) != MH_OK) {
        MH_RemoveHook(entry);
        g_census.store(nullptr, std::memory_order_release);
        return false;
    }
    entry_ = entry;
    installed_ = true;
    return true;
}

void ShaderCensus::Remove() {
    if (!installed_) return;
    MH_DisableHook(entry_);
    MH_RemoveHook(entry_);
    entry_ = nullptr;
    ShaderCensus* expected = this;
    g_census.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
    installed_ = false;
}

std::string ShaderCensus::Layout(const void* code, SIZE_T size) {
    ComPtr<ID3D11ShaderReflection> r;
    if (FAILED(D3DReflect(code, size, kIidShaderReflection, reinterpret_cast<void**>(r.GetAddressOf()))) || !r)
        return {};
    D3D11_SHADER_DESC desc{};
    if (FAILED(r->GetDesc(&desc))) return {};
    std::string out;
    char buf[160];
    for (UINT i = 0; i < desc.ConstantBuffers; ++i) {
        ID3D11ShaderReflectionConstantBuffer* cb = r->GetConstantBufferByIndex(i);
        D3D11_SHADER_BUFFER_DESC cd{};
        if (!cb || FAILED(cb->GetDesc(&cd))) continue;
        std::snprintf(buf, sizeof(buf), "cb %s[%u]{", cd.Name ? cd.Name : "?", cd.Size);
        out += buf;
        for (UINT v = 0; v < cd.Variables; ++v) {
            ID3D11ShaderReflectionVariable* var = cb->GetVariableByIndex(v);
            D3D11_SHADER_VARIABLE_DESC vd{};
            if (!var || FAILED(var->GetDesc(&vd))) continue;
            std::snprintf(buf, sizeof(buf), "%s%s@%u:%u", v ? " " : "", vd.Name ? vd.Name : "?", vd.StartOffset, vd.Size);
            out += buf;
        }
        out += "} ";
    }
    for (UINT i = 0; i < desc.BoundResources; ++i) {
        D3D11_SHADER_INPUT_BIND_DESC bd{};
        if (FAILED(r->GetResourceBindingDesc(i, &bd))) continue;
        std::snprintf(buf, sizeof(buf), "res %s:%u@%u ", bd.Name ? bd.Name : "?", static_cast<unsigned>(bd.Type), bd.BindPoint);
        out += buf;
    }
    std::snprintf(buf, sizeof(buf), "in=%u out=%u instr=%u", desc.InputParameters, desc.OutputParameters,
                  desc.InstructionCount);
    out += buf;
    return out;
}

void ShaderCensus::OnCreate(const void* code, SIZE_T size, const void* shader) {
    const std::string layout = Layout(code, size);
    std::set<std::string> names;
    if (!layout.empty()) { // names: tokens before '[' / '@' / ':'
        std::size_t p = 0;
        while (p < layout.size()) {
            const std::size_t end = layout.find_first_of(" {}", p);
            std::string tok = layout.substr(p, end == std::string::npos ? std::string::npos : end - p);
            p = end == std::string::npos ? layout.size() : end + 1;
            if (tok.empty() || tok == "cb" || tok == "res" || tok.rfind("in=", 0) == 0 || tok.rfind("out=", 0) == 0 ||
                tok.rfind("instr=", 0) == 0)
                continue;
            const std::size_t cut = tok.find_first_of("[@:");
            names.insert(tok.substr(0, cut));
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    ++shaders_;
    if (layout.empty()) {
        ++unreflected_;
        return;
    }
    Entry& e = layouts_[layout];
    if (e.count == 0) {
        e.id = static_cast<int>(ids_.size());
        ids_.push_back(&layouts_.find(layout)->first);
    }
    if (shader) byShader_[shader] = e.id;
    if (e.count++ == 0 && codeBytes_ + size <= kMaxCodeBytes) {
        const auto* b = static_cast<const std::uint8_t*>(code);
        e.code.assign(b, b + size);
        codeBytes_ += size;
    }
    for (const auto& n : names) ++names_[n];
}

std::string ShaderCensus::Report() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string out = "shaders=" + std::to_string(shaders_) + " unreflected=" + std::to_string(unreflected_) +
                      " layouts=" + std::to_string(layouts_.size()) + "\n== names (shaders using each)\n";
    std::vector<std::pair<std::uint64_t, std::string>> byCount;
    for (const auto& [n, c] : names_) byCount.emplace_back(c, n);
    std::sort(byCount.rbegin(), byCount.rend());
    for (const auto& [c, n] : byCount) out += std::to_string(c) + " " + n + "\n";
    out += "== layouts (shaders using each)\n";
    std::vector<std::pair<std::uint64_t, const std::string*>> layouts;
    for (const auto& [l, e] : layouts_) layouts.emplace_back(e.count, &l);
    std::stable_sort(layouts.begin(), layouts.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    int index = 0;
    for (const auto& [c, l] : layouts)
        out += "[" + std::to_string(index++) + "] " + std::to_string(c) + " #" + std::to_string(layouts_.at(*l).id) + " " + *l + "\n";
    return out;
}

int ShaderCensus::Dump(const std::string& dir, const std::string& needle, int limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string want = Lower(needle);
    std::vector<std::pair<std::uint64_t, const std::pair<const std::string, Entry>*>> layouts;
    for (const auto& kv : layouts_) layouts.emplace_back(kv.second.count, &kv);
    std::stable_sort(layouts.begin(), layouts.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    int written = 0, index = 0;
    for (const auto& [c, kv] : layouts) {
        const int at = index++; // the same index as in Report()
        if (written >= limit) break;
        if (kv->second.code.empty() || Lower(kv->first).find(want) == std::string::npos) continue;
        const std::string path = dir + "\\vs-" + std::to_string(at) + ".dxbc";
        if (std::FILE* f = std::fopen(path.c_str(), "wb")) {
            if (std::fwrite(kv->second.code.data(), 1, kv->second.code.size(), f) == kv->second.code.size()) ++written;
            std::fclose(f);
        }
    }
    return written;
}

int ShaderCensus::LayoutOf(const void* shader) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = byShader_.find(shader);
    return it == byShader_.end() ? -1 : it->second;
}

std::string ShaderCensus::LayoutText(int id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return id >= 0 && static_cast<std::size_t>(id) < ids_.size() ? *ids_[static_cast<std::size_t>(id)] : std::string();
}

std::string ShaderCensus::Describe() const {
    std::lock_guard<std::mutex> lock(mutex_);
    char line[160];
    std::snprintf(line, sizeof(line), "shader_census installed=%d shaders=%llu unreflected=%llu layouts=%zu kept_bytes=%zu",
                  static_cast<int>(installed_), static_cast<unsigned long long>(shaders_),
                  static_cast<unsigned long long>(unreflected_), layouts_.size(), codeBytes_);
    return line;
}

} // namespace fvr::bf4
