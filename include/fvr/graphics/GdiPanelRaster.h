#pragma once
#include "fvr/ui/VrPanel.h"
#include <cstdint>
#include <vector>
namespace fvr::graphics {
// CPU raster of a VrPanel through GDI (no font assets). Output rows are
// top-down, 4 bytes per pixel, opaque. bgra=false swizzles to RGBA.
class GdiPanelRaster {
public:
    GdiPanelRaster()=default;~GdiPanelRaster();
    GdiPanelRaster(const GdiPanelRaster&)=delete;GdiPanelRaster& operator=(const GdiPanelRaster&)=delete;
    bool Render(const ui::VrPanel&,bool bgra)noexcept;
    const std::uint8_t* Pixels()const noexcept{return pixels_.data();}
    std::uint32_t Width()const noexcept{return width_;}
    std::uint32_t Height()const noexcept{return height_;}
    std::uint32_t Pitch()const noexcept{return width_*4;}
private:
    bool Ensure(std::uint32_t w,std::uint32_t h)noexcept;
    void* dc_=nullptr;void* bitmap_=nullptr;void* old_=nullptr;void* bits_=nullptr;
    void* font_=nullptr;void* titleFont_=nullptr;void* smallFont_=nullptr;
    std::uint32_t width_=0,height_=0;std::vector<std::uint8_t> pixels_;
};
}
