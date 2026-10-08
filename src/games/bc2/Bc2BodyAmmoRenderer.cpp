#include "Bc2BodyAmmoRenderer.h"
#include "Bc2BodyAmmo.h"
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <mutex>
namespace fvr::bc2 {
namespace {
using Microsoft::WRL::ComPtr;
constexpr std::uint64_t FnvOffset=14695981039346656037ull,FnvPrime=1099511628211ull;
void Number(std::uint64_t& h,std::uint64_t n)noexcept {for(unsigned i=0;i<8;++i){h=(h^(n&255))*FnvPrime;n>>=8;}}
void Text(std::uint64_t& h,std::string_view s)noexcept {Number(h,s.size());for(unsigned char c:s)h=(h^c)*FnvPrime;}
bool Name(std::string_view s)noexcept {return !s.empty()&&s.size()<=512&&std::all_of(s.begin(),s.end(),[](unsigned char c){return c>=32&&c<=126;});}
bool Fresh(std::int64_t a,std::int64_t b,std::int64_t now)noexcept{return a>0&&a<=now&&b>now&&b-a<=100000000;}
bool Eye(BodyAmmoEyeKey e)noexcept{return e.world&&e.request&&e.view&&e.frame&&e.eye<2;}
bool Texture(ID3D11View* view,ID3D11Device* expected,unsigned& w,unsigned& h,unsigned& samples,unsigned& quality,
    std::uint64_t& resource,bool& single)noexcept {
    if(!view)return false;ComPtr<ID3D11Device> device;view->GetDevice(&device);
    ComPtr<ID3D11Resource> r;view->GetResource(&r);resource=reinterpret_cast<std::uintptr_t>(r.Get());
    ComPtr<ID3D11Texture2D> t;if(FAILED(r.As(&t)))return false;D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);
    w=d.Width;h=d.Height;samples=d.SampleDesc.Count;quality=d.SampleDesc.Quality;single=d.ArraySize==1&&d.MipLevels==1;
    return device.Get()==expected;
}
BodyAmmoTargetIdentity Target(ID3D11DeviceContext* c,ComPtr<ID3D11RenderTargetView>& color,ComPtr<ID3D11DepthStencilView>& depth)noexcept {
    BodyAmmoTargetIdentity t;if(!c)return t;c->OMGetRenderTargets(1,&color,&depth);
    t.colorView=reinterpret_cast<std::uintptr_t>(color.Get());t.depthView=reinterpret_cast<std::uintptr_t>(depth.Get());
    ComPtr<ID3D11Device> device;c->GetDevice(&device);bool colorSingle=false,depthSingle=false;
    const bool colorDevice=Texture(color.Get(),device.Get(),t.colorWidth,t.colorHeight,t.colorSamples,t.colorQuality,t.colorResource,colorSingle);
    const bool depthDevice=Texture(depth.Get(),device.Get(),t.depthWidth,t.depthHeight,t.depthSamples,t.depthQuality,t.depthResource,depthSingle);
    t.sameDevice=colorDevice&&depthDevice;t.singleSliceMip=colorSingle&&depthSingle;
    D3D11_RENDER_TARGET_VIEW_DESC rd{};D3D11_DEPTH_STENCIL_VIEW_DESC dd{};
    if(color)color->GetDesc(&rd);if(depth)depth->GetDesc(&dd);t.colorFormat=rd.Format;t.depthFormat=dd.Format;
    t.supportedViews=(rd.ViewDimension==D3D11_RTV_DIMENSION_TEXTURE2D||rd.ViewDimension==D3D11_RTV_DIMENSION_TEXTURE2DMS)&&
        (dd.ViewDimension==D3D11_DSV_DIMENSION_TEXTURE2D||dd.ViewDimension==D3D11_DSV_DIMENSION_TEXTURE2DMS);return t;
}
bool GeometryValid(const BodyAmmoGeometry& g)noexcept {
    if(!g.rigFingerprint||!Name(g.asset)||!Name(g.mesh)||!Name(g.part)||g.key!=MakeBodyAmmoGeometryKey(g.asset,g.mesh,g.part,g.rigFingerprint)||
        g.sections.empty()||g.sections.size()>8)return false;
    std::size_t count=0;
    for(const auto& s:g.sections){count+=s.mesh.vertices.size();if(!s.mesh.sourceVertexSkinHash||!s.mesh.sourcePositionHash||
        s.mesh.vertices.empty()||s.mesh.vertices.size()%3||count>32768*3)return false;
        for(auto color:s.color)if(!std::isfinite(color)||color<0||color>1)return false;
        if(s.color[3]!=1)return false;
    }return true;
}
}
graphics::RigidPropGeometryKey MakeBodyAmmoGeometryKey(std::string_view asset,std::string_view mesh,std::string_view part,std::uint64_t rig)noexcept {
    if(!Name(asset)||!Name(mesh)||!Name(part)||!rig)return {};
    auto a=FnvOffset,p=FnvOffset,r=FnvOffset;Text(a,asset);Text(p,part);Text(r,mesh);Number(r,rig);
    return {a,p,r};
}
std::shared_ptr<const BodyAmmoGeometry> BuildBodyAmmoGeometry(std::string_view asset,std::string_view mesh,std::string_view part,
    std::uint64_t rig,std::span<const BodyAmmoSectionBytes> sections){
    const auto key=MakeBodyAmmoGeometryKey(asset,mesh,part,rig);if(!graphics::ValidRigidPropKey(key)||sections.empty()||sections.size()>8)return {};
    auto out=std::make_shared<BodyAmmoGeometry>();out->key=key;out->asset=asset;out->mesh=mesh;out->part=part;out->rigFingerprint=rig;
    for(std::size_t n=0;n<sections.size();++n){const auto& s=sections[n];const auto* p=s.profile;
        if(!p||!p->asset||!p->mesh||!p->part||!p->section||asset!=p->asset||mesh!=p->mesh||part!=p->part||!Name(p->section))return {};
        for(std::size_t prior=0;prior<n;++prior)if(std::string_view(sections[prior].profile->section)==p->section)return {};
        auto vertices=graphics::ExtractRigidProp(p->geometry,s.bytes,s.canonicalInverseBind,s.basis);if(!vertices)return {};
        out->sections.push_back({std::move(*vertices),s.color});
    }
    if(!GeometryValid(*out))return {};return out;
}
bool BodyAmmoGeometryMatches(const BodyAmmoGeometry& g,const BodyAmmoRenderSource& s)noexcept {
    return g.key==s.geometry&&g.asset==s.asset&&g.mesh==s.mesh&&g.part==s.part&&g.rigFingerprint==s.rigFingerprint&&
        s.geometry==MakeBodyAmmoGeometryKey(s.asset,s.mesh,s.part,s.rigFingerprint);
}
bool BodyAmmoSourceRetained(const BodyAmmoRenderSource& a,const BodyAmmoRenderSource& b,std::int64_t now)noexcept {
    const auto authorityMatches=[&](const BodyAmmoRenderSource& s){
        if(!s.authority||!BodyAmmoFresh(*s.authority,now)||
            !interaction::AmmoSupplyVisualRetained(s.prop.source,s.authority->visual,now)||
            !interaction::AmmoSupplyVisualRetained(s.authority->visual,s.prop.source,now))return false;
        if(s.authority->magazine){const auto& g=*s.authority->magazine->family.binding.profile->geometry;
            return s.asset==g.asset&&s.mesh==g.mesh&&s.part==g.bones.magazine&&s.rigFingerprint==g.rigFingerprint;}
        return s.asset==SpasReloadAsset&&s.mesh==SpasReloadMesh&&s.part=="jntWpn_7"&&s.rigFingerprint==SpasReloadRig;
    };
    return authorityMatches(a)&&authorityMatches(b)&&BodyAmmoRetained(*a.authority,*b.authority,now)&&
        graphics::ValidRigidPropKey(a.geometry)&&a.geometry==b.geometry&&a.asset==b.asset&&a.mesh==b.mesh&&a.part==b.part&&
        a.rigFingerprint&&a.rigFingerprint==b.rigFingerprint&&a.geometry==MakeBodyAmmoGeometryKey(a.asset,a.mesh,a.part,a.rigFingerprint)&&
        graphics::RigidPropWorldValid(a.prop.partWorld)&&graphics::RigidPropWorldValid(b.prop.partWorld)&&
        interaction::AmmoSupplyPropRetained(a.prop,b.prop.source,now);
}
unsigned BodyAmmoBoundaryMissing(const BodyAmmoBoundaryProof& p,BodyAmmoEyeKey key,const BodyAmmoTargetIdentity& t,std::uint64_t context,std::int64_t now)noexcept {
    unsigned missing=0;
    if(!Eye(key)||p.eye!=key||!context||p.immediateContext!=context)missing|=BodyAmmoWrongEye;
    if(!t.colorView||!t.colorResource)missing|=BodyAmmoNoColor;
    if(!t.depthView||!t.depthResource)missing|=BodyAmmoNoDepth;
    if(!t.sameDevice||!t.singleSliceMip||!t.supportedViews||!t.colorWidth||!t.colorHeight||t.colorWidth>16384||t.colorHeight>16384||
        t.colorWidth!=t.depthWidth||t.colorHeight!=t.depthHeight||t.colorSamples!=t.depthSamples||t.colorQuality!=t.depthQuality)missing|=BodyAmmoTargetLayout;
    if(!p.nativeDrawReturned||!p.diagnosticQueryEnded||!p.nativeQueriesObserved||!p.queryObservationGeneration)missing|=BodyAmmoNoQueryProof;
    if(p.nativeQueriesObserved&&p.activeScopedQueries)missing|=BodyAmmoQueryActive;
    if(!p.depthProjectionAssociated||p.target!=t||!interaction::InverseRigid(p.eyeView)||
        !graphics::RigidPropClipTransform(interaction::reload_insertion_detail::Identity(),p.eyeView,p.projection))missing|=BodyAmmoNoDepthProjectionProof;
    if(!Fresh(p.observedNs,p.deadlineNs,now))missing|=BodyAmmoStaleProof;
    return missing;
}
struct Bc2BodyAmmoRenderer::Impl {
    struct Record {BodyAmmoEyeKey eye{};BodyAmmoTargetIdentity target{};unsigned thread=0,missing=0,serial=0;std::int64_t now=0;
        std::uint64_t input=0,actor=0,equipment=0,space=0;bool before=false,current=false,geometry=false,drawn=false;std::atomic<bool> complete=false;};
    static constexpr unsigned MaxRows=256;
    std::atomic<bool> enabled=false,stopping=false,active=false;
    std::atomic<unsigned> ownerThread=0,allocated=0,attempts=0,draws=0,dropped=0,inFlight=0;
    std::array<std::atomic<std::uint64_t>,15> reasons{};std::array<Record,MaxRows> rows{};
    std::atomic<std::shared_ptr<const BodyAmmoGeometry>> pending;
    std::atomic<std::shared_ptr<const BodyAmmoGeometryCatalog>> catalog;
    std::shared_ptr<const BodyAmmoGeometry> uploaded;
    std::unique_ptr<graphics::D3D11RigidPropRenderer> renderer;
    ID3D11DeviceContext* context=nullptr; // Identity only; renderer owns the actual COM ref.
    std::array<std::int64_t,2> nextRecord{};std::array<unsigned,2> previousMissing{~0u,~0u};
    mutable std::mutex recordMutex;
};
Bc2BodyAmmoRenderer::Bc2BodyAmmoRenderer():impl_(std::make_unique<Impl>()){}
Bc2BodyAmmoRenderer::~Bc2BodyAmmoRenderer(){
    // A failed detach keeps the module retained; never call COM release from an
    // arbitrary unload thread. Successful detach has drained PumpStop already.
    if(impl_->renderer&&impl_->ownerThread.load()!=GetCurrentThreadId())impl_->renderer.release();
}
bool Bc2BodyAmmoRenderer::QueueGeometry(std::shared_ptr<const BodyAmmoGeometry> geometry)noexcept {
    if(impl_->stopping.load()||!geometry||!GeometryValid(*geometry))return false;impl_->pending.store(std::move(geometry),std::memory_order_release);return true;
}
bool Bc2BodyAmmoRenderer::QueueGeometryCatalog(std::shared_ptr<const BodyAmmoGeometryCatalog> catalog)noexcept {
    if(impl_->stopping.load()||!catalog||catalog->empty()||catalog->size()>16)return false;
    for(std::size_t n=0;n<catalog->size();++n){const auto& g=(*catalog)[n];if(!g||!GeometryValid(*g))return false;
        for(std::size_t previous=0;previous<n;++previous)if((*catalog)[previous]->key==g->key)return false;}
    impl_->catalog.store(std::move(catalog),std::memory_order_release);return true;
}
void Bc2BodyAmmoRenderer::EnableObservation(bool value)noexcept {if(!impl_->stopping.load())impl_->enabled.store(value,std::memory_order_release);}
void Bc2BodyAmmoRenderer::RequestStop()noexcept {impl_->enabled.store(false,std::memory_order_release);impl_->stopping.store(true,std::memory_order_release);}
bool Bc2BodyAmmoRenderer::Quiescent()const noexcept{return !impl_->active.load(std::memory_order_acquire)&&!impl_->inFlight.load(std::memory_order_acquire);}
bool Bc2BodyAmmoRenderer::PumpStop()noexcept {
    auto& p=*impl_;if(!p.stopping.load(std::memory_order_acquire))return Quiescent();
    if(!p.active.load(std::memory_order_acquire))return true;
    if(p.ownerThread.load(std::memory_order_acquire)!=GetCurrentThreadId())return false;
    p.renderer.reset();p.uploaded.reset();p.pending.store({},std::memory_order_release);p.context=nullptr;p.active.store(false,std::memory_order_release);return true;
}
void Bc2BodyAmmoRenderer::EndEye(ID3D11DeviceContext* context,BodyAmmoEyeKey key,const BodyAmmoRenderSource* before,
    const BodyAmmoRenderSource* current,std::int64_t now,const BodyAmmoBoundaryProof& proof)noexcept {
    auto& p=*impl_;struct Callback {std::atomic<unsigned>& count;Callback(std::atomic<unsigned>& c):count(c){count.fetch_add(1,std::memory_order_acq_rel);}~Callback(){count.fetch_sub(1,std::memory_order_release);}} callback(p.inFlight);
    if(p.stopping.load(std::memory_order_acquire)){PumpStop();return;}if(!p.enabled.load(std::memory_order_acquire))return;
    const auto thread=GetCurrentThreadId();unsigned absent=0;p.ownerThread.compare_exchange_strong(absent,thread);
    // Never inspect or modify an immediate context from a migrating callback.
    if(p.ownerThread.load(std::memory_order_acquire)!=thread){++p.reasons[11];return;}
    ++p.attempts;ComPtr<ID3D11RenderTargetView> color;ComPtr<ID3D11DepthStencilView> depth;
    const auto target=Target(context,color,depth);unsigned missing=BodyAmmoBoundaryMissing(proof,key,target,reinterpret_cast<std::uintptr_t>(context),now);
    auto geometry=p.pending.load(std::memory_order_acquire);
    if(before)if(const auto catalog=p.catalog.load(std::memory_order_acquire))
        for(const auto& part:*catalog)if(BodyAmmoGeometryMatches(*part,*before)){geometry=part;break;}
    if(!before||!current)missing|=BodyAmmoNoSource;
    else if(!BodyAmmoSourceRetained(*before,*current,now))missing|=BodyAmmoChangedSource;
    const bool hasGeometry=before&&geometry&&BodyAmmoGeometryMatches(*geometry,*before);
    if(!hasGeometry)missing|=BodyAmmoNoGeometry;
    bool drawn=false;
    try {
        if(!missing){
            if(p.context&&p.context!=context)missing|=BodyAmmoBackendFailure;
            if(!missing&&!p.renderer){auto renderer=std::make_unique<graphics::D3D11RigidPropRenderer>(true);
                if(renderer->Initialize(context)){p.renderer=std::move(renderer);p.context=context;p.active.store(true,std::memory_order_release);}else missing|=BodyAmmoBackendFailure;}
            if(!missing&&p.uploaded!=geometry){if(p.renderer->Upload(geometry->key,geometry->sections))p.uploaded=geometry;else missing|=BodyAmmoBackendFailure;}
            if(!missing){const auto& s=before->prop.source;const auto& i=s.input;
                const auto observed=(std::max)({i.observedNs,s.source.observedNs,proof.observedNs});
                const auto deadline=(std::min)({i.deadlineNs,s.source.deadlineNs,s.gun.deadlineNs,proof.deadlineNs});
                const graphics::RigidPropEye eye{key.world,key.request,key.view,key.frame,i.owner.actor,i.owner.actorGeneration,
                    s.source.identity.weapon.id,i.owner.equipGeneration,i.owner.space,i.sequence,key.eye,observed,deadline};
                drawn=p.renderer->Draw(geometry->key,eye,eye,now,before->prop.partWorld,proof.eyeView,proof.projection,
                    {color.Get(),depth.Get(),target.colorWidth,target.colorHeight,proof.reversedDepth});
                if(!drawn)missing|=BodyAmmoBackendFailure;else ++p.draws;
            }
        }
    }catch(...){missing|=BodyAmmoBackendFailure;}
    for(unsigned n=0;n<p.reasons.size();++n)if(missing&(1u<<n))++p.reasons[n];
    // State changes are recorded immediately; unchanged state is sampled at10Hz.
    if(key.eye<2&&(missing!=p.previousMissing[key.eye]||now>=p.nextRecord[key.eye])){
        std::unique_lock lock(p.recordMutex,std::try_to_lock);if(!lock.owns_lock()){++p.dropped;return;}
        p.previousMissing[key.eye]=missing;p.nextRecord[key.eye]=now+100000000;
        const auto serial=p.allocated.fetch_add(1);auto& row=p.rows[serial%Impl::MaxRows];row.complete.store(false,std::memory_order_release);
        row.eye=key;row.target=target;row.thread=thread;row.missing=missing;row.serial=serial;row.now=now;row.before=bool(before);row.current=bool(current);row.geometry=hasGeometry;row.drawn=drawn;
        row.input=row.actor=row.equipment=row.space=0;
        if(before){const auto& i=before->prop.source.input;row.input=i.sequence;row.actor=i.owner.actor;row.equipment=i.owner.equipGeneration;row.space=i.owner.space;}
        row.complete.store(true,std::memory_order_release);
    }
}
void Bc2BodyAmmoRenderer::Report(std::ostream& out)const {
    const auto& p=*impl_;out<<"{\"attempts\":"<<p.attempts.load()<<",\"draws\":"<<p.draws.load()<<",\"gpu_active\":"<<p.active.load()
        <<",\"dropped_rows\":"<<p.dropped.load()<<",\"in_flight\":"<<p.inFlight.load()<<",\"reason_names\":[\"no_source\",\"source_changed\",\"no_geometry\",\"wrong_eye\",\"no_color\",\"no_depth\",\"target_layout\",\"no_query_proof\",\"query_active\",\"no_depth_projection_proof\",\"stale_proof\",\"wrong_thread\",\"backend_failure\",\"stopped\",\"not_native_admitted\"],\"reasons\":[";
    for(unsigned n=0;n<p.reasons.size();++n){if(n)out<<',';out<<p.reasons[n].load();}out<<"],\"records\":[";bool first=true;
    const auto catalog=p.catalog.load(std::memory_order_acquire);
    std::unique_lock lock(p.recordMutex,std::try_to_lock);
    const unsigned end=p.allocated.load(),begin=end>Impl::MaxRows?end-Impl::MaxRows:0;
    for(unsigned serial=begin;lock.owns_lock()&&serial<end;++serial){const auto& r=p.rows[serial%Impl::MaxRows];if(!r.complete.load(std::memory_order_acquire)||r.serial!=serial)continue;if(!first)out<<',';first=false;
        out<<"{\"world\":"<<r.eye.world<<",\"request\":"<<r.eye.request<<",\"view\":"<<r.eye.view<<",\"frame\":"<<r.eye.frame<<",\"eye\":"<<r.eye.eye
            <<",\"thread\":"<<r.thread<<",\"now_ns\":"<<r.now<<",\"missing\":"<<r.missing<<",\"source_before\":"<<r.before<<",\"source_after\":"<<r.current
            <<",\"geometry\":"<<r.geometry<<",\"drawn\":"<<r.drawn<<",\"input\":"<<r.input<<",\"actor\":"<<r.actor<<",\"equipment\":"<<r.equipment<<",\"space\":"<<r.space
            <<",\"color_view\":"<<r.target.colorView<<",\"depth_view\":"<<r.target.depthView<<",\"color_resource\":"<<r.target.colorResource<<",\"depth_resource\":"<<r.target.depthResource
            <<",\"color_size\":["<<r.target.colorWidth<<','<<r.target.colorHeight<<"],\"depth_size\":["<<r.target.depthWidth<<','<<r.target.depthHeight<<"]"
            <<",\"color_samples\":"<<r.target.colorSamples<<",\"depth_samples\":"<<r.target.depthSamples<<",\"color_format\":"<<r.target.colorFormat<<",\"depth_format\":"<<r.target.depthFormat<<'}';}
    out<<"],\"records_complete\":"<<lock.owns_lock()<<",\"retained_serial_begin\":"<<begin<<",\"record_total\":"<<end
        <<",\"catalog_parts\":"<<(catalog?catalog->size():0)<<'}';
}
} // namespace fvr::bc2
