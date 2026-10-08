#pragma once
#include "fvr/interaction/ControllerInput.h"
#include "fvr/ipc/MenuProtocol.h"
#include "fvr/math/UiPointerMath.h"
#include <algorithm>
namespace fvr::interaction {
struct MenuPointerFrame {
    ipc::MenuControl control{};math::Pose panel{},cursor{};
    math::Vec3 rayStart{},rayEnd{};
    float width=2.2f,height=1.2375f;
    bool visible=false,pointing=false,suppressLeftFaceButtons=false;
    void FilterGameplayInput(InputFrame& input)const noexcept {
        // Keep availability/poses/touches intact; only suppress chord button
        // holds until BOTH buttons release, including through menu close.
        if(suppressLeftFaceButtons)input.hands[0].held&=~std::uint32_t(Primary|Secondary);
    }
};
// Independent menu policy: no native input codes, GPU resources or OS cursor.
class MenuPointer {
public:
    MenuPointerFrame Update(const InputFrame& input,const ipc::MenuState* menu,
        std::uint32_t rasterWidth,std::uint32_t rasterHeight)noexcept {
        MenuPointerFrame out;out.suppressLeftFaceButtons=chordSuppress_;auto& c=out.control;c.sequence=input.generation;c.space=input.spaceGeneration;
        c.toggle=toggle_;c.cancel=cancel_;
        const bool safe=ValidInput(input)&&input.focused&&input.headValid;
        if(!safe||space_!=input.spaceGeneration){
            anchor_={};menuArmed_=cancelArmed_=pressArmed_=chordArmed_=false;chordSince_=lastChordNs_=0;
            space_=input.spaceGeneration;epoch_=0;
            if(!safe)return out;
        }
        c.flags=ipc::MenuFocused;
        const auto& l=input.hands[0];const auto& r=input.hands[1];
        const bool menuAvailable=(l.active&MenuClick)!=0;
        const bool menuHeld=menuAvailable&&(l.held&MenuClick);
        bool toggle=UpdateChord(input);
        out.suppressLeftFaceButtons=chordSuppress_;
        if(!menuAvailable)menuArmed_=false;
        else if(!menuHeld)menuArmed_=true;
        else if(menuArmed_){toggle=true;menuArmed_=false;}
        if(toggle)++toggle_;
        c.toggle=toggle_;
        if(!menu||menu->mode!=ipc::MenuMode::Menu||!menu->inputReady||!rasterWidth||!rasterHeight){
            anchor_={};epoch_=0;cancelArmed_=pressArmed_=false;return out;
        }
        if(epoch_!=menu->epoch){anchor_={};epoch_=menu->epoch;cancelArmed_=pressArmed_=false;}
        if(!math::UpdateUiMenuAnchor(anchor_,input.head,input.predictedNs,1.1f,.5f))return out;
        out.visible=true;out.height=out.width*float(rasterHeight)/float(rasterWidth);
        if(!std::isfinite(out.height)||out.height<=0||out.height>8){out.visible=false;return out;}
        out.panel=anchor_.anchor;
        const auto rotate=[](math::Quaternion q,math::Vec3 p){
            math::Vec3 t{2*(q.y*p.z-q.z*p.y),2*(q.z*p.x-q.x*p.z),2*(q.x*p.y-q.y*p.x)};
            return math::Vec3{p.x+q.w*t.x+q.y*t.z-q.z*t.y,p.y+q.w*t.y+q.z*t.x-q.x*t.z,p.z+q.w*t.z+q.x*t.y-q.y*t.x};
        };
        const auto ahead=rotate(out.panel.orientation,{0,0,-1.5f});
        out.panel.position.x+=ahead.x;out.panel.position.y+=ahead.y;out.panel.position.z+=ahead.z;
        c.menuEpoch=menu->epoch;
        const bool cancelAvailable=(r.active&Secondary)!=0;
        const bool cancelHeld=cancelAvailable&&(r.held&Secondary);
        if(!cancelAvailable)cancelArmed_=false;
        else if(!cancelHeld)cancelArmed_=true;
        else if(cancelArmed_){++cancel_;cancelArmed_=false;}
        c.cancel=cancel_;
        const bool triggerActive=(r.active&Trigger)!=0;
        if(!r.aimTracked||!triggerActive){pressArmed_=false;return out;}
        if(r.trigger<=.25f)pressArmed_=true;
        const auto hit=math::MapOpenXRAimPoseToAspectFitUiCanvas(r.aim,out.panel,out.width,out.height,
            rasterWidth,rasterHeight,rasterWidth,rasterHeight,menu->logicalWidth,menu->logicalHeight);
        if(!hit){pressArmed_=false;return out;}
        out.pointing=true;c.flags|=ipc::MenuPoint;c.u=hit->normalizedX;c.v=hit->normalizedY;
        if(pressArmed_&&r.trigger>=.65f)c.flags|=ipc::MenuDown;
        out.cursor=out.panel;
        const auto point=rotate(out.panel.orientation,{(c.u-.5f)*out.width,(.5f-c.v)*out.height,.002f});
        out.cursor.position.x+=point.x;out.cursor.position.y+=point.y;out.cursor.position.z+=point.z;
        out.rayStart=r.aim.position;out.rayEnd=out.cursor.position;return out;
    }
private:
    // Some wireless runtime bindings expose no usable app-menu source. A
    // neutral-armed half-second left Primary+Secondary chord stays portable
    // and leaves both-stick recenter and individual button mappings intact.
    bool UpdateChord(const InputFrame& input)noexcept {
        const auto now=input.predictedNs;
        if(lastChordNs_&&(now<lastChordNs_||now-lastChordNs_>150000000)){
            chordArmed_=false;chordSince_=0;
        }
        lastChordNs_=now;
        const auto& left=input.hands[0];constexpr auto buttons=std::uint32_t(Primary|Secondary);
        if((left.active&buttons)!=buttons){chordArmed_=false;chordSince_=0;return false;}
        const auto held=left.held&buttons;
        if(!held){chordArmed_=true;chordSuppress_=false;chordSince_=0;return false;}
        if(held!=buttons){chordSince_=0;return false;}
        chordSuppress_=true;
        if(!chordArmed_)return false;
        if(!chordSince_)chordSince_=now;
        if(now-chordSince_<500000000)return false;
        chordArmed_=false;chordSince_=0;return true;
    }
    math::UiMenuAnchorTracker anchor_{};
    std::uint64_t space_=0,epoch_=0,toggle_=0,cancel_=0;
    bool menuArmed_=false,cancelArmed_=false,pressArmed_=false,chordArmed_=false,chordSuppress_=false;
    std::int64_t chordSince_=0,lastChordNs_=0;
};
}
