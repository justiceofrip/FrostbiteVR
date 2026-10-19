#include "Bf4PointerInjector.h"

#include <algorithm>

namespace fvr::bf4 {

using Type = PointerEvent::Type;

namespace {
struct PixelPoint { int x = 0, y = 0; };
// Normalised panel u,v (0..1, top-left origin) to client pixels, clamped inside.
PixelPoint UvToPixel(float u, float v, int width, int height) {
    PixelPoint p;
    p.x = std::min(width - 1, static_cast<int>(std::clamp(u, 0.0f, 1.0f) * static_cast<float>(width)));
    p.y = std::min(height - 1, static_cast<int>(std::clamp(v, 0.0f, 1.0f) * static_cast<float>(height)));
    return p;
}
} // namespace

void PointerInjector::ForceRelease(std::vector<PointerEvent>& out) {
    if (down_) {
        out.push_back({Type::ButtonUp, x_, y_});
        down_ = false;
    }
    if (active_) {
        out.push_back({Type::Deactivate, x_, y_});
        active_ = false;
    }
}

void PointerInjector::Update(const GamePointer& p, int w, int h, std::vector<PointerEvent>& out) {
    out.clear();
    if (!p.active || w <= 0 || h <= 0) {
        ForceRelease(out);
        return;
    }
    const auto px = UvToPixel(p.u, p.v, w, h);
    if (!active_ || px.x != x_ || px.y != y_) out.push_back({Type::Move, px.x, px.y});
    active_ = true;
    x_ = px.x;
    y_ = px.y;
    if (p.down && !down_) {
        out.push_back({Type::ButtonDown, x_, y_});
        down_ = true;
    } else if (!p.down && down_) {
        out.push_back({Type::ButtonUp, x_, y_});
        down_ = false;
        ++clicks_;
    }
}

} // namespace fvr::bf4
