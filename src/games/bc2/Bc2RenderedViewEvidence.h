#pragma once
#include "fvr/graphics/FrameBridgeDiagnostics.h"
#include "fvr/graphics/RigidPropFrame.h"
#include "fvr/ipc/StagedFrameProducer.h"
#include <limits>
#include <ostream>

namespace fvr::bc2 {
// Owned, bounded diagnostic values only. Never a camera or publication permit.
// The native adapter supplies this exact lease identity at every boundary.
struct RenderedViewKey {
    graphics::FrameBridgeTimingKey frame{};
    std::uint32_t sourceSlot=0,sourceSlotCount=1;
    float worldUnitsPerMeter=0;
    bool operator==(const RenderedViewKey&)const=default;
    bool Valid()const noexcept {
        return frame.channel&&frame.request&&frame.owner&&frame.frame&&frame.device&&frame.tracking&&frame.space&&
            frame.predictedNs>0&&sourceSlotCount&&sourceSlot<sourceSlotCount&&
            std::isfinite(worldUnitsPerMeter)&&worldUnitsPerMeter>0;
    }
};
inline RenderedViewKey RenderedViewLeaseKey(const ipc::FrameLease& lease)noexcept {
    return {{lease.channelGeneration,lease.requestId,lease.native.owner,lease.native.frameId,lease.native.deviceEpoch,
        lease.tracking.generation,lease.tracking.spaceGeneration,lease.tracking.predictedNs},
        lease.sourceSlot,lease.sourceSlotCount,lease.tracking.worldUnitsPerMeter};
}
struct RenderedViewEvidence {
    RenderedViewKey key{};
    std::array<math::Matrix4,2> view{},projection{};
    std::array<std::uint64_t,2> nativeView{};
    graphics::PairTicket ticket{};
    unsigned drawReturnedMask=0,matrixValidMask=0,captureAttemptedMask=0,captureAcceptedMask=0;
    bool finished=false,restored=false,publicationEligible=false,publicationAccepted=false,published=false,invalid=false;

    bool Sampled()const noexcept {return key.frame.request!=0;}
    bool Match(const RenderedViewKey& other)noexcept {
        if(key.Valid()&&key==other)return true;
        invalid=true;return false;
    }
    bool Eye(const RenderedViewKey& other,unsigned eye,std::uint64_t address,
             const math::Matrix4* actualView,const math::Matrix4* actualProjection)noexcept {
        if(!Match(other)||eye>1){invalid=true;return false;}
        const auto bit=1u<<eye;
        if(finished||(drawReturnedMask&bit)||!address){invalid=true;return false;}
        drawReturnedMask|=bit;nativeView[eye]=address;
        math::Matrix4 identity{};for(unsigned n=0;n<4;++n)identity.values[n][n]=1;
        if(!actualView||!actualProjection||!graphics::RigidPropClipTransform(identity,*actualView,*actualProjection))return false;
        view[eye]=*actualView;projection[eye]=*actualProjection;matrixValidMask|=bit;return true;
    }
    bool Capture(const RenderedViewKey& other,unsigned eye,bool accepted)noexcept {
        if(!Match(other)||eye>1){invalid=true;return false;}
        const auto bit=1u<<eye;
        if(finished||(captureAttemptedMask&bit)){invalid=true;return false;}
        captureAttemptedMask|=bit;if(accepted)captureAcceptedMask|=bit;return accepted;
    }
    bool Finish(const RenderedViewKey& other,bool exactRestoration,bool eligible)noexcept {
        if(!Match(other)||finished){invalid=true;return false;}
        finished=true;restored=exactRestoration;publicationEligible=eligible;return true;
    }
    bool Published(const RenderedViewKey& other,const graphics::PairTicket& value)noexcept {
        if(!Match(other))return false;
        bool session=false;for(auto byte:value.session)session|=byte!=0;
        const auto& f=key.frame;
        if(!finished||!restored||!publicationEligible||published||
           value.magic!=graphics::TicketMagic||value.version!=graphics::TextureProtocol||value.bytes!=sizeof(value)||
           value.reserved0||value.reserved[0]||value.reserved[1]||!session||!value.resourceEpoch||
           !value.sequence||value.sequence>0x7fffffffffffffffULL||value.frameId!=f.frame||
           value.trackingGeneration!=f.tracking||value.spaceGeneration!=f.space||value.predictedNs!=f.predictedNs){
            invalid=true;return false;
        }
        ticket=value;published=true;return true;
    }
    bool Usable()const noexcept {
        // This evidence contract covers the one-source comparison only.
        return key.Valid()&&key.sourceSlotCount==1&&!invalid&&finished&&restored&&publicationEligible&&
            publicationAccepted&&published&&drawReturnedMask==3&&matrixValidMask==3&&
            captureAttemptedMask==3&&captureAcceptedMask==3&&nativeView[0]&&nativeView[1]&&nativeView[0]!=nativeView[1];
    }
    void Write(std::ostream& out)const {
        const auto precision=out.precision(std::numeric_limits<float>::max_digits10);
        const auto& f=key.frame;
        out<<"{\"schema\":1,\"sampled\":"<<(Sampled()?"true":"false")<<",\"usable\":"<<(Usable()?"true":"false")
           <<",\"lease\":{\"channel\":"<<f.channel<<",\"request\":"<<f.request<<",\"owner\":"<<f.owner
           <<",\"frame\":"<<f.frame<<",\"device\":"<<f.device<<",\"tracking\":"<<f.tracking<<",\"space\":"<<f.space
           <<",\"predicted_ns\":"<<f.predictedNs<<",\"source_slot\":"<<key.sourceSlot<<",\"source_slot_count\":"<<key.sourceSlotCount
           <<",\"world_units_per_meter\":";
        if(std::isfinite(key.worldUnitsPerMeter))out<<key.worldUnitsPerMeter;else out<<"null";
        out<<"},\"draw_returned_mask\":"<<drawReturnedMask<<",\"matrix_valid_mask\":"<<matrixValidMask
           <<",\"capture_attempted_mask\":"<<captureAttemptedMask<<",\"capture_accepted_mask\":"<<captureAcceptedMask
           <<",\"finished\":"<<(finished?"true":"false")<<",\"restored\":"<<(restored?"true":"false")
           <<",\"publication_eligible\":"<<(publicationEligible?"true":"false")
           <<",\"publication_accepted\":"<<(publicationAccepted?"true":"false")<<",\"published\":"<<(published?"true":"false")
           <<",\"invalid\":"<<(invalid?"true":"false")<<",\"eyes\":[";
        const auto matrix=[&](const math::Matrix4& value){out<<'[';for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){if(r||c)out<<',';out<<value.values[r][c];}out<<']';};
        for(unsigned eye=0;eye<2;++eye){if(eye)out<<',';out<<"{\"eye\":"<<eye<<",\"native_view\":"<<nativeView[eye]<<",\"view\":";
            if(matrixValidMask&(1u<<eye))matrix(view[eye]);else out<<"null";
            out<<",\"projection\":";if(matrixValidMask&(1u<<eye))matrix(projection[eye]);else out<<"null";out<<'}';}
        out<<"],\"ticket\":";
        if(!published)out<<"null";
        else {out<<"{\"magic\":"<<ticket.magic<<",\"version\":"<<ticket.version<<",\"bytes\":"<<ticket.bytes<<",\"reserved0\":"<<ticket.reserved0
            <<",\"resource_epoch\":"<<ticket.resourceEpoch<<",\"sequence\":"<<ticket.sequence<<",\"frame\":"<<ticket.frameId
            <<",\"tracking\":"<<ticket.trackingGeneration<<",\"space\":"<<ticket.spaceGeneration<<",\"predicted_ns\":"<<ticket.predictedNs<<",\"session\":[";
            for(unsigned i=0;i<ticket.session.size();++i){if(i)out<<',';out<<unsigned(ticket.session[i]);}
            out<<"],\"reserved\":["<<ticket.reserved[0]<<','<<ticket.reserved[1]<<"]}";}
        out<<'}';out.precision(precision);
    }
};
static_assert(std::is_trivially_copyable_v<RenderedViewKey>&&std::is_trivially_copyable_v<RenderedViewEvidence>);
}
