#pragma once
#include "fvr/runtime/PresentationPolicy.h"
#include <optional>

namespace fvr::runtime {
// Metadata for the last complete pair in presenter-owned images. The source
// ticket is acknowledged exactly once; repeated presentation never reconsumes it.
// Keep the original render poses/FOV: the compositor reprojects these images.
class RetainedPresentation {
public:
    static constexpr std::int64_t MaxAgeNs=250000000;
    void Reset() noexcept { saved_.reset(); }
    bool Commit(const PresentationRequirements& requirements,const TrackingFrame& rendered,
                const graphics::TextureDescriptor& descriptor,const graphics::PairTicket& ticket) noexcept {
        if(!PairMatchesFrame(requirements,rendered,descriptor,ticket)||!ValidViews(rendered))return false;
        requirements_=requirements;saved_=rendered;return true;
    }
    const TrackingFrame* Select(const PresentationRequirements& requirements,
                                const TrackingFrame& current,bool shouldRender=true) noexcept {
        if(!saved_)return nullptr;
        const auto& old=*saved_;
        // A discontinuity permanently invalidates the retained pair. Regaining
        // focus/tracking must not resurrect an image from before that transition.
        if(!shouldRender||!current.focused||!current.headValid||!ValidViews(current)||
           current.spaceGeneration!=old.spaceGeneration||current.predictedNs<old.predictedNs||
           current.predictedNs-old.predictedNs>MaxAgeNs||
           requirements.width!=requirements_.width||requirements.height!=requirements_.height||
           requirements.format!=requirements_.format||requirements.adapterLow!=requirements_.adapterLow||
           requirements.adapterHigh!=requirements_.adapterHigh){Reset();return nullptr;}
        return &*saved_;
    }
private:
    static bool ValidViews(const TrackingFrame& tracking) noexcept {
        if(!tracking.generation||!tracking.spaceGeneration||tracking.predictedNs<=0)return false;
        for(unsigned eye=0;eye<2;++eye)
            if(!math::MakeLhViewFromOpenXRPose(tracking.eyes[eye])||
               !math::MakeLhProjectionFromFovTangents(tracking.fov[eye],.05f,100.f))return false;
        return true;
    }
    PresentationRequirements requirements_{};
    std::optional<TrackingFrame> saved_;
};
}
