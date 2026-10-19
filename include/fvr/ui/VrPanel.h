#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
namespace fvr::ui {
// Laser-pointer panel model in logical pixels. No GPU, GDI or native input:
// adapters feed normalized pointer samples (MenuControl u/v + trigger) and
// rasterize the widgets however they like.
struct Rect {float x=0,y=0,w=0,h=0;bool Contains(float px,float py)const noexcept{return px>=x&&py>=y&&px<x+w&&py<y+h;}};
enum class WidgetKind:std::uint32_t {Label,Title,Button,Choice,Toggle};
struct Widget {
    std::uint32_t id=0;WidgetKind kind=WidgetKind::Label;Rect rect{};
    std::wstring text,detail;bool enabled=true,active=false;
};
struct PointerSample {bool pointing=false,down=false;float u=0,v=0;};
struct PanelEvent {std::uint32_t clicked=0;};
class VrPanel {
public:
    VrPanel(std::uint32_t width,std::uint32_t height):width_(width),height_(height){}
    std::uint32_t Width()const noexcept{return width_;}
    std::uint32_t Height()const noexcept{return height_;}
    // Rebuild widgets whenever the model changes; pointer state survives.
    void Begin()noexcept{next_.clear();}
    void Add(Widget w){next_.push_back(std::move(w));}
    void End(){if(!Same(next_,widgets_)){widgets_.swap(next_);++revision_;}}
    const std::vector<Widget>& Widgets()const noexcept{return widgets_;}
    // A click needs a fresh press and a release on the same enabled widget, so a
    // held trigger carried in from gameplay or a drag-off never activates anything.
    PanelEvent Update(const PointerSample& p)noexcept {
        PanelEvent e{};const auto previousHover=hover_;const bool previousPointing=pointing_;
        pointing_=p.pointing&&std::isfinite(p.u)&&std::isfinite(p.v);
        if(pointing_){cursorX_=p.u*float(width_);cursorY_=p.v*float(height_);}
        hover_=pointing_?Hit(cursorX_,cursorY_):0;
        const bool down=pointing_&&p.down;
        if(down&&!down_)pressed_=armed_?hover_:0;
        if(!down&&down_){if(pressed_&&pressed_==hover_){e.clicked=pressed_;++clicks_;}pressed_=0;}
        if(!p.down)armed_=true;
        down_=down;
        if(hover_!=previousHover||pointing_!=previousPointing||pointing_)++pointerRevision_;
        return e;
    }
    void Disarm()noexcept{armed_=false;pressed_=0;down_=false;}
    std::uint32_t Hover()const noexcept{return hover_;}
    std::uint32_t Pressed()const noexcept{return pressed_;}
    bool Pointing()const noexcept{return pointing_;}
    float CursorX()const noexcept{return cursorX_;}
    float CursorY()const noexcept{return cursorY_;}
    std::uint64_t Revision()const noexcept{return revision_;}
    std::uint64_t PointerRevision()const noexcept{return pointerRevision_;}
    std::uint64_t Clicks()const noexcept{return clicks_;}
    const Widget* Find(std::uint32_t id)const noexcept{for(const auto& w:widgets_)if(w.id==id)return &w;return nullptr;}
private:
    std::uint32_t Hit(float x,float y)const noexcept {
        for(auto it=widgets_.rbegin();it!=widgets_.rend();++it)
            if(it->id&&it->enabled&&it->kind!=WidgetKind::Label&&it->kind!=WidgetKind::Title&&it->rect.Contains(x,y))return it->id;
        return 0;
    }
    static bool Same(const std::vector<Widget>& a,const std::vector<Widget>& b){
        if(a.size()!=b.size())return false;
        for(size_t i=0;i<a.size();++i){const auto& x=a[i];const auto& y=b[i];
            if(x.id!=y.id||x.kind!=y.kind||x.text!=y.text||x.detail!=y.detail||x.enabled!=y.enabled||x.active!=y.active||
               x.rect.x!=y.rect.x||x.rect.y!=y.rect.y||x.rect.w!=y.rect.w||x.rect.h!=y.rect.h)return false;}
        return true;
    }
    std::uint32_t width_,height_;std::vector<Widget> widgets_,next_;
    std::uint32_t hover_=0,pressed_=0;bool down_=false,armed_=false,pointing_=false;
    float cursorX_=0,cursorY_=0;std::uint64_t revision_=1,pointerRevision_=0,clicks_=0;
};
}
