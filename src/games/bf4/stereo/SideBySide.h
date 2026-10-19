#pragma once
// Writes the left and right eye images as one side-by-side BMP (24-bit, downscaled) so a
// probe run produces a picture you can look at without a headset. Stdlib only.
#include <cstdint>
#include <string>

namespace fvr::bf4::stereo {

struct ImageView {
    const std::uint8_t* pixels = nullptr; // 4 bytes per pixel
    int width = 0, height = 0;
    int rowPitch = 0;                      // bytes
    bool rgba = true;                      // false: BGRA
};

struct MeanColor { float r = 0, g = 0, b = 0; };
MeanColor Mean(const ImageView& image);

// downscale >= 1 (box filter). Returns false on bad input or I/O error.
bool WriteSideBySideBmp(const std::string& path, const ImageView& left, const ImageView& right, int downscale);

// Halves of one side-by-side frame (BF4's RenderDevice.StereoSplitScreenEnable mode).
ImageView LeftHalf(const ImageView& full);
ImageView RightHalf(const ImageView& full);

// Is this backbuffer an engine side-by-side stereo frame? Engine SBS halves show the same
// view from two eyes (they correlate after a small horizontal shift); a mono frame's
// halves show different parts of the screen. Sparse luma patches, best shift +-maxShift px.
enum class SplitVerdict : std::uint8_t { Undetermined, Stereo, Mono };
struct SplitAnalysis {
    float correlation = 0; // mean best normalised cross-correlation of textured patches, 0..1
    float texture = 0;     // fraction of patches with enough structure to judge, 0..1
    SplitVerdict verdict = SplitVerdict::Undetermined;
};
SplitAnalysis AnalyzeSplitFrame(const ImageView& full, int step = 8, int maxShift = 48);
const char* SplitVerdictName(SplitVerdict v) noexcept;

} // namespace fvr::bf4::stereo
