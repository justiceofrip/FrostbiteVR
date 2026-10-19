#include "fvr/graphics/GdiPanelRaster.h"
#include <Windows.h>
#include <cstring>
namespace fvr::graphics {
namespace {
COLORREF Rgb(unsigned v){return RGB((v>>16)&255,(v>>8)&255,v&255);}
void Fill(HDC dc,const ui::Rect& r,unsigned color,int radius){
    const auto brush=CreateSolidBrush(Rgb(color));const auto pen=CreatePen(PS_SOLID,1,Rgb(color));
    const auto oldBrush=SelectObject(dc,brush);const auto oldPen=SelectObject(dc,pen);
    RoundRect(dc,int(r.x),int(r.y),int(r.x+r.w),int(r.y+r.h),radius,radius);
    SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(brush);DeleteObject(pen);
}
void Text(HDC dc,HFONT font,const std::wstring& s,RECT r,unsigned color,UINT format){
    if(s.empty())return;SelectObject(dc,font);SetTextColor(dc,Rgb(color));DrawTextW(dc,s.c_str(),int(s.size()),&r,format|DT_NOPREFIX);
}
HFONT Font(int height,int weight){return CreateFontW(-height,0,0,0,weight,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");}
}
GdiPanelRaster::~GdiPanelRaster(){
    if(dc_){SelectObject(HDC(dc_),HGDIOBJ(old_));DeleteDC(HDC(dc_));}
    for(void* o:{bitmap_,font_,titleFont_,smallFont_})if(o)DeleteObject(HGDIOBJ(o));
}
bool GdiPanelRaster::Ensure(std::uint32_t w,std::uint32_t h)noexcept {
    if(dc_&&w==width_&&h==height_)return true;
    if(!w||!h||w>8192||h>8192)return false;
    if(!dc_){dc_=CreateCompatibleDC(nullptr);if(!dc_)return false;font_=Font(30,500);titleFont_=Font(44,700);smallFont_=Font(24,400);}
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(info.bmiHeader);info.bmiHeader.biWidth=LONG(w);info.bmiHeader.biHeight=-LONG(h);
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;const auto bitmap=CreateDIBSection(HDC(dc_),&info,DIB_RGB_COLORS,&bits,nullptr,0);if(!bitmap)return false;
    const auto previous=SelectObject(HDC(dc_),bitmap);if(!old_)old_=previous;else DeleteObject(previous);
    bitmap_=bitmap;bits_=bits;width_=w;height_=h;pixels_.assign(size_t(w)*h*4,0);return true;
}
bool GdiPanelRaster::Render(const ui::VrPanel& panel,bool bgra)noexcept {
    if(!Ensure(panel.Width(),panel.Height()))return false;
    const HDC dc=HDC(dc_);SetBkMode(dc,TRANSPARENT);
    Fill(dc,{0,0,float(width_),float(height_)},0x12151a,0);
    Fill(dc,{0,0,float(width_),6},0x2f6fd6,0);
    for(const auto& w:panel.Widgets()){
        RECT r{LONG(w.rect.x),LONG(w.rect.y),LONG(w.rect.x+w.rect.w),LONG(w.rect.y+w.rect.h)};
        switch(w.kind){
        case ui::WidgetKind::Title:Text(dc,HFONT(titleFont_),w.text,r,0xf2f4f8,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            if(!w.detail.empty()){Text(dc,HFONT(smallFont_),w.detail,r,0x8d97a8,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);}break;
        case ui::WidgetKind::Label:{
            Text(dc,HFONT(font_),w.text,r,w.enabled?0xc9d1dd:0x6b7380,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            if(!w.detail.empty())Text(dc,HFONT(smallFont_),w.detail,r,0x8d97a8,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);break;}
        default:{
            const bool hover=panel.Hover()==w.id,pressed=panel.Pressed()==w.id;
            unsigned fill=!w.enabled?0x1d2127:w.active?0x2f6fd6:0x262c35;
            if(w.enabled&&hover)fill=w.active?0x4a86e8:0x364152;if(w.enabled&&pressed)fill=0x5b95f0;
            Fill(dc,w.rect,fill,18);
            RECT inner=r;inner.left+=16;inner.right-=16;
            const unsigned color=w.enabled?0xf2f4f8:0x5d6573;
            if(w.detail.empty())Text(dc,HFONT(font_),w.text,inner,color,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            else{RECT top=inner;top.bottom=top.top+LONG(w.rect.h*.58f);RECT bottom=inner;bottom.top=top.bottom-4;
                Text(dc,HFONT(font_),w.text,top,color,DT_CENTER|DT_BOTTOM|DT_SINGLELINE);
                Text(dc,HFONT(smallFont_),w.detail,bottom,w.enabled?0xaeb8c8:0x5d6573,DT_CENTER|DT_TOP|DT_SINGLELINE);}
        }}
    }
    GdiFlush();
    const auto* src=static_cast<const std::uint8_t*>(bits_);auto* dst=pixels_.data();const size_t n=size_t(width_)*height_;
    for(size_t i=0;i<n;++i){const auto* s=src+i*4;auto* d=dst+i*4;
        if(bgra){d[0]=s[0];d[1]=s[1];d[2]=s[2];}else{d[0]=s[2];d[1]=s[1];d[2]=s[0];}d[3]=255;}
    return true;
}
}
