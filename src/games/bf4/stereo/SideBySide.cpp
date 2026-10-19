#include "SideBySide.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace fvr::bf4::stereo {

namespace {
void Put16(std::vector<std::uint8_t>& b, std::uint16_t v) { b.push_back(v & 0xFF); b.push_back(v >> 8); }
void Put32(std::vector<std::uint8_t>& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((v >> (8 * i)) & 0xFF); }

void Sample(const ImageView& im, int x, int y, int k, int& r, int& g, int& b) {
    r = g = b = 0;
    for (int dy = 0; dy < k; ++dy)
        for (int dx = 0; dx < k; ++dx) {
            const std::uint8_t* p = im.pixels + (y * k + dy) * im.rowPitch + (x * k + dx) * 4;
            r += im.rgba ? p[0] : p[2];
            g += p[1];
            b += im.rgba ? p[2] : p[0];
        }
    const int n = k * k;
    r /= n; g /= n; b /= n;
}
} // namespace

MeanColor Mean(const ImageView& im) {
    MeanColor m;
    if (!im.pixels || im.width <= 0 || im.height <= 0) return m;
    double r = 0, g = 0, b = 0;
    for (int y = 0; y < im.height; ++y)
        for (int x = 0; x < im.width; ++x) {
            const std::uint8_t* p = im.pixels + y * im.rowPitch + x * 4;
            r += im.rgba ? p[0] : p[2];
            g += p[1];
            b += im.rgba ? p[2] : p[0];
        }
    const double n = static_cast<double>(im.width) * im.height * 255.0;
    m.r = static_cast<float>(r / n); m.g = static_cast<float>(g / n); m.b = static_cast<float>(b / n);
    return m;
}

bool WriteSideBySideBmp(const std::string& path, const ImageView& left, const ImageView& right, int k) {
    if (k < 1 || !left.pixels || !right.pixels || left.width != right.width || left.height != right.height) return false;
    const int w = left.width / k, h = left.height / k;
    if (w <= 0 || h <= 0) return false;
    const int outW = w * 2;
    const int stride = (outW * 3 + 3) & ~3;
    std::vector<std::uint8_t> file;
    file.reserve(54 + static_cast<std::size_t>(stride) * h);
    file.push_back('B'); file.push_back('M');
    Put32(file, 54 + static_cast<std::uint32_t>(stride) * h); Put32(file, 0); Put32(file, 54);
    Put32(file, 40); Put32(file, static_cast<std::uint32_t>(outW)); Put32(file, static_cast<std::uint32_t>(h));
    Put16(file, 1); Put16(file, 24); Put32(file, 0); Put32(file, static_cast<std::uint32_t>(stride) * h);
    Put32(file, 2835); Put32(file, 2835); Put32(file, 0); Put32(file, 0);
    for (int row = h - 1; row >= 0; --row) { // BMP is bottom-up
        const std::size_t start = file.size();
        for (int x = 0; x < outW; ++x) {
            int r, g, b;
            Sample(x < w ? left : right, x < w ? x : x - w, row, k, r, g, b);
            file.push_back(static_cast<std::uint8_t>(b));
            file.push_back(static_cast<std::uint8_t>(g));
            file.push_back(static_cast<std::uint8_t>(r));
        }
        while (file.size() - start < static_cast<std::size_t>(stride)) file.push_back(0);
    }
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(file.data(), 1, file.size(), f) == file.size();
    std::fclose(f);
    return ok;
}

ImageView LeftHalf(const ImageView& full) {
    ImageView h = full;
    h.width = full.width / 2;
    return h;
}

ImageView RightHalf(const ImageView& full) {
    ImageView h = full;
    h.width = full.width / 2;
    if (full.pixels) h.pixels = full.pixels + static_cast<std::size_t>(full.width - h.width) * 4;
    return h;
}

namespace {
float Luma(const ImageView& im, int x, int y) {
    const std::uint8_t* p = im.pixels + static_cast<std::size_t>(y) * im.rowPitch + static_cast<std::size_t>(x) * 4;
    const int r = im.rgba ? p[0] : p[2], b = im.rgba ? p[2] : p[0];
    return (0.299f * r + 0.587f * p[1] + 0.114f * b) / 255.0f;
}
} // namespace

SplitAnalysis AnalyzeSplitFrame(const ImageView& full, int step, int maxShift) {
    SplitAnalysis a;
    if (!full.pixels || full.width < 64 || full.height < 16 || step < 1) return a;
    const ImageView l = LeftHalf(full), r = RightHalf(full);
    // Normalised cross-correlation of 16x3-sample patches, best horizontal shift within
    // +-maxShift: brightness/contrast independent (dark levels), and a whole patch only
    // correlates with the same view seen from the other eye.
    constexpr int kCols = 16, kRows = 3, kN = kCols * kRows;
    const int margin = maxShift + 1;
    double corrSum = 0;
    std::size_t textured = 0, patches = 0;
    float lv[kN], rv[kN];
    for (int y0 = step / 2; y0 + (kRows - 1) * step < l.height; y0 += kRows * step) {
        for (int x0 = margin; x0 + kCols * step < l.width - margin; x0 += kCols * step) {
            ++patches;
            double mean = 0, var = 0;
            for (int k = 0; k < kN; ++k) {
                lv[k] = Luma(l, x0 + (k % kCols) * step, y0 + (k / kCols) * step);
                mean += lv[k];
            }
            mean /= kN;
            for (int k = 0; k < kN; ++k) var += (lv[k] - mean) * (lv[k] - mean);
            if (var / kN < 0.008 * 0.008) continue; // flat patch: no evidence either way
            float best = -1.0f;
            for (int d = -maxShift; d <= maxShift; d += 2) {
                double rm = 0;
                for (int k = 0; k < kN; ++k) {
                    rv[k] = Luma(r, x0 + (k % kCols) * step + d, y0 + (k / kCols) * step);
                    rm += rv[k];
                }
                rm /= kN;
                double cov = 0, rvar = 0;
                for (int k = 0; k < kN; ++k) {
                    cov += (lv[k] - mean) * (rv[k] - rm);
                    rvar += (rv[k] - rm) * (rv[k] - rm);
                }
                if (rvar <= 0) continue;
                best = std::max(best, static_cast<float>(cov / std::sqrt(var * rvar)));
            }
            corrSum += std::max(best, 0.0f);
            ++textured;
        }
    }
    a.texture = patches ? static_cast<float>(textured) / patches : 0.0f;
    if (textured < 12) return a; // too little structure (loading/black screen)
    a.correlation = static_cast<float>(corrSum / textured);
    if (a.correlation > 0.75f) a.verdict = SplitVerdict::Stereo;
    else if (a.correlation < 0.55f) a.verdict = SplitVerdict::Mono;
    return a;
}
const char* SplitVerdictName(SplitVerdict v) noexcept {
    switch (v) {
    case SplitVerdict::Stereo: return "split-stereo";
    case SplitVerdict::Mono: return "split-mono";
    case SplitVerdict::Undetermined: return "split-undetermined";
    }
    return "unknown";
}

} // namespace fvr::bf4::stereo
