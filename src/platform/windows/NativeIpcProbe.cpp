#include "fvr/ipc/RemoteFrameProvider.h"
#include "fvr/ipc/WaitForConnection.h"
#include "fvr/graphics/D3D11SharedPair.h"
#include "fvr/graphics/D3D11SceneDepthPair.h"
#include "fvr/graphics/D3D11BodyPropCompositor.h"
#include "fvr/graphics/BodyPropMaterials.h"
#include "Bc2BodyAmmoGeometryCache.h"
#include "Bc2ReloadRequestProbe.h"
#include "Bc2PumpDiagnostic.h"
#include "Bc2PumpReceiverGapProbe.h"
#include "Bc2ShellFocusLossProbe.h"
#include "Bc2MagazineReloadProbe.h"
#include "Bc2PhysicalReloadPreparation.h"
#include "BoatFireProbe.h"
#include "SceneRecoveryProbe.h"
#include "TriggerInputSmoke.h"
#include "BodyCrossDrawProbe.h"
#include "OrdinaryGrenadeProbeInput.h"
#include "OrdinaryGrenadeFadeProbe.h"
#include "TwoGrenadeFadeProbe.h"
#include "PlayerGrenadeReceiver.h"
#include "ChestKnifeProbeReceiver.h"
#include "ContextualToolProbeReceiver.h"
#include "NormalKnifeHeldCloseReceiver.h"
#include "fvr/ipc/MenuChannel.h"
#include "BodyInspectionProbe.h"
#include "ResourceMagazineProbeInput.h"
#include "PhysicalPumpProbeInput.h"
#include "SightGestureFixtureGeometry.h"
#include "DiagnosticImageWriter.h"
#include "PumpSideCaptureWindow.h"
#include <Windows.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include <iomanip>
#include <sstream>
namespace fs=std::filesystem;using namespace fvr;using Microsoft::WRL::ComPtr;
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void Hr(HRESULT value,const char* message){Require(SUCCEEDED(value),message);}
std::int64_t PresentationClockNs()noexcept {
    LARGE_INTEGER q{},f{};
    if(!QueryPerformanceCounter(&q)||!QueryPerformanceFrequency(&f)||q.QuadPart<=0||f.QuadPart<=0||f.QuadPart>1000000000)return 0;
    return (q.QuadPart/f.QuadPart)*1000000000+(q.QuadPart%f.QuadPart)*1000000000/f.QuadPart;
}
// Observe the same delivered frame and freshness policy used by the XR host.
// These records neither enter gameplay nor renew any native observation.
void RecordPresentation(std::ostream& out,const graphics::BodyPropFrame& props,bool read,
    graphics::AmmoCounterPresentation& counter,const graphics::PairTicket& ticket,std::uint64_t elapsed) {
    const auto now=PresentationClockNs();
    const auto matrix=[&](const math::Matrix4& m){out<<'['<<std::setprecision(9);
        for(unsigned row=0;row<4;++row){if(row)out<<',';out<<'[';
            for(unsigned col=0;col<4;++col){if(col)out<<',';out<<m.values[row][col];}out<<']';}out<<']';};
    const bool visible=read&&counter.Receive(props.eyes[0].ammo,props.eyes[1].ammo,ticket.spaceGeneration,now);
    if(!read)counter.Reset();
    out<<"{\"elapsed_ms\":"<<elapsed<<",\"native_frame\":"<<ticket.frameId
       <<",\"tracking\":"<<ticket.trackingGeneration<<",\"space\":"<<ticket.spaceGeneration
       <<",\"sample_ns\":"<<now<<",\"props_read\":"<<(read?"true":"false")
       <<",\"ammo_visible\":"<<(visible?"true":"false")<<",\"eyes\":[";
    for(unsigned eye=0;eye<2;++eye){
        if(eye)out<<',';
        const auto& e=props.eyes[eye];const auto& a=e.ammo;
        out<<"{\"view\":";matrix(e.view);out<<",\"projection\":";matrix(e.projection);
        out<<",\"ammo\":{\"sequence\":"<<a.sequence<<",\"observed_ns\":"<<a.observedNs
           <<",\"deadline_ns\":"<<a.deadlineNs<<",\"actor\":"<<a.actorGeneration
           <<",\"equipment\":"<<a.equipmentGeneration<<",\"space\":"<<a.spaceGeneration
           <<",\"loaded\":"<<a.loaded<<",\"reserve\":"<<a.reserve<<",\"capacity\":"<<a.capacity
           <<",\"fresh\":"<<(graphics::AmmoCounterFresh(a,now)?"true":"false")<<"},\"props\":[";
        for(unsigned n=0;n<e.count&&n<graphics::MaxBodyProps;++n){
            if(n)out<<',';const auto& p=e.instances[n];
            out<<"{\"asset\":"<<p.geometry.asset<<",\"part\":"<<p.geometry.part
               <<",\"revision\":"<<p.geometry.revision<<",\"actor\":"<<p.actorGeneration
               <<",\"equipment\":"<<p.equipmentGeneration<<",\"space\":"<<p.spaceGeneration
               <<",\"observed_ns\":"<<p.observedNs<<",\"deadline_ns\":"<<p.deadlineNs
               <<",\"fresh\":"<<(graphics::BodyPropFresh(p,now)?"true":"false")<<",\"world\":";
            matrix(p.world);out<<'}';
        }
        out<<"]}";
    }
    out<<"]}\n";out.flush();Require(bool(out),"Save delivered presentation evidence");
}
std::vector<unsigned char> Pixels(ID3D11DeviceContext* context,ID3D11Texture2D* output,ID3D11Texture2D* staging,unsigned eye){
    context->CopySubresourceRegion(staging,0,0,0,0,output,eye,nullptr);D3D11_MAPPED_SUBRESOURCE map{};Hr(context->Map(staging,0,D3D11_MAP_READ,0,&map),"Read native image");
    std::vector<unsigned char> pixels(1920u*1080u*4u);for(unsigned row=0;row<1080;++row)std::memcpy(pixels.data()+std::size_t(row)*1920*4,static_cast<const char*>(map.pData)+std::size_t(row)*map.RowPitch,1920*4);context->Unmap(staging,0);return pixels;
}
template<class T> bool ReadDeathWitness(const fs::path& path,T& out)noexcept {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER length{};DWORD copied=0;T value{};
    const bool okay=GetFileSizeEx(file,&length)&&length.QuadPart==sizeof(T)&&ReadFile(file,&value,sizeof(T),&copied,nullptr)&&copied==sizeof(T);
    CloseHandle(file);if(okay)out=value;return okay;
}
// Read-only held handle: original process identity survives PID reuse.
struct NormalToolTargetLifetime {
    HANDLE handle=nullptr;
    ~NormalToolTargetLifetime(){if(handle)CloseHandle(handle);}
    bool Open(std::uint64_t pid,std::uint64_t creation) {
        if(handle||!pid||pid>UINT32_MAX||!creation)return false;
        handle=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,DWORD(pid));
        if(!handle)return false;
        FILETIME made{},exited{},kernel{},user{};std::wstring path(32768,L'\0');DWORD count=DWORD(path.size());
        if(!GetProcessTimes(handle,&made,&exited,&kernel,&user)||
           ((std::uint64_t(made.dwHighDateTime)<<32)|made.dwLowDateTime)!=creation||
           !QueryFullProcessImageNameW(handle,0,path.data(),&count))return false;
        path.resize(count);return fs::path(path).filename()==L"BFBC2Game.exe"&&Current();
    }
    bool Current()const noexcept{return handle&&WaitForSingleObject(handle,0)==WAIT_TIMEOUT;}
};
// Only the opt-in trigger smoke opens this bounded journal. Each phase request
// and successful publication receipt is flushed to the OS before continuing;
// hard termination can lose the stop row, never turn missing evidence into pass.
class TriggerSmokeJournal {
public:
    explicit TriggerSmokeJournal(probe::TriggerInputSmoke& state):state_(state){}
    ~TriggerSmokeJournal(){if(file_!=INVALID_HANDLE_VALUE){
        if(!stopped_)try{Stop("exception_or_early_exit",false,0);}catch(...){}
        CloseHandle(file_);}}
    bool Open(const fs::path& folder,std::uint64_t started,unsigned pairs) {
        started_=started;file_=CreateFileW((folder/L"trigger-input.jsonl").c_str(),GENERIC_WRITE,
            FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file_==INVALID_HANDLE_VALUE)return false;
        std::ostringstream out;out<<"{\"event\":\"controls_started\",\"controls_started_ms\":"<<started_
            <<",\"sample_ns\":"<<PresentationClockNs()<<",\"receiver_pid\":"<<GetCurrentProcessId()
            <<",\"requested_pairs\":"<<pairs<<",\"duration_ms\":"<<state_.Duration()
            <<",\"selected_knife_motion\":"<<(state_.IsMotion()?"true":"false")
            <<",\"approach_ms\":"<<state_.ApproachMs()<<",\"approach_left_stick\":["<<state_.Strafe()<<','<<state_.Forward()
            <<"],\"attack_start_ms\":"<<state_.AttackStartMs()<<",\"all_inputs_journaled\":"<<(state_.JournalEveryInput()?"true":"false");
        state_.ReportRotation(out);
        state_.ReportTurn(out);
        state_.ReportReview(out);
        state_.ReportResume(out,0);
        state_.ReportPrepared(out,0);
        if(state_.CrouchDuringApproach())out<<",\"approach_crouch\":true";
        out<<",\"emulated_host\":true,\"native_input_observed\":false,\"headset_tested\":false}\n";
        return Write(out.str());
    }
    bool Requested(const interaction::InputFrame& input,std::uint64_t elapsed) {
        const auto phase=state_.CurrentPhase(elapsed);
        if(!state_.JournalEveryInput()&&phase==lastRequestedPhase_)return true;
        lastRequestedPhase_=phase;
        return InputRecord("input_request",input,elapsed,0,0,0);
    }
    bool Published(probe::TriggerInputSmoke::Receipt receipt,const interaction::InputFrame& input,
        std::uint64_t elapsed,std::int64_t before,std::int64_t after,unsigned code,bool known,bool delivered,bool busy) {
        if(!state_.JournalEveryInput()&&receipt==probe::TriggerInputSmoke::Receipt::None)return true;
        const char* event=state_.JournalEveryInput()?"publication_result":receipt==probe::TriggerInputSmoke::Receipt::Neutral?"neutral_published":
            receipt==probe::TriggerInputSmoke::Receipt::Press?"trigger_press_published":
            receipt==probe::TriggerInputSmoke::Receipt::Release?"trigger_release_published":
            receipt==probe::TriggerInputSmoke::Receipt::Motion?"motion_published":"neutral_tail_published";
        return InputRecord(event,input,elapsed,before,after,code,known,delivered,busy);
    }
    bool Stop(const char* reason,bool durationEnded,unsigned pairs) {
        if(stopped_)return !failed_;
        const auto elapsed=GetTickCount64()-started_;
        std::ostringstream out;out<<"{\"event\":\"stopped\",\"reason\":\""<<reason
            <<"\",\"tick_ms\":"<<GetTickCount64()<<",\"sample_ns\":"<<PresentationClockNs()
            <<",\"consumed_pairs\":"<<(durationEnded?std::to_string(pairs):"null")
            <<",\"journal_error\":"<<(failed_?"true":"false")<<",\"summary\":";
        state_.Report(out,elapsed,durationEnded&&!failed_);out<<"}\n";
        stopped_=Write(out.str());return stopped_;
    }
private:
    bool InputRecord(const char* event,const interaction::InputFrame& input,std::uint64_t elapsed,
        std::int64_t before,std::int64_t after,unsigned code,bool known=false,bool delivered=false,bool busy=false) {
        std::ostringstream payload;if(state_.HasMotionRotation())payload.precision(9);
        probe::WriteNeutralSceneInput(payload,input,started_+elapsed);
        auto frame=payload.str();if(!frame.empty()&&frame.back()=='\n')frame.pop_back();
        std::ostringstream out;out<<"{\"event\":\""<<event<<"\",\"elapsed_ms\":"<<elapsed
            <<",\"publication_before_ns\":"<<before<<",\"publication_after_ns\":"<<after
            <<",\"publication_code\":"<<code<<",\"publication_known\":"<<(known?"true":"false")
            <<",\"publication_delivered\":"<<(delivered?"true":"false")<<",\"publication_busy\":"<<(busy?"true":"false");
        state_.ReportResume(out,elapsed);
        state_.ReportPrepared(out,elapsed);
        out<<",\"input\":"<<frame<<"}\n";
        return Write(out.str());
    }
    bool Write(const std::string& text)noexcept {
        DWORD written=0;
        const bool okay=file_!=INVALID_HANDLE_VALUE&&text.size()<=16384&&
            (!state_.JournalEveryInput()||writtenBytes_+text.size()<=16u*1024u*1024u)&&
            WriteFile(file_,text.data(),DWORD(text.size()),&written,nullptr)&&written==text.size()&&FlushFileBuffers(file_);
        writtenBytes_+=written;
        failed_=failed_||!okay;return okay;
    }
    probe::TriggerInputSmoke& state_;HANDLE file_=INVALID_HANDLE_VALUE;
    std::uint64_t started_=0,writtenBytes_=0;unsigned lastRequestedPhase_=~0u;bool stopped_=false,failed_=false;
};
int wmain(int argc,wchar_t** argv){try{
    Require(argc>=3&&std::wstring(argv[1])==L"--output","Usage: BC2NativeIpcProbe --output new-folder [--pairs N] [--asymmetric] [--static-pose] [--async]");
    unsigned targetPairs=8,requestLifetimeMs=150,shotPrimaryPulses=2;bool asymmetric=false,staticPose=false,async=false,fovSweep=false,controlsObserve=false,controlsPulse=false,controlsTurn=false,controlsFire=false,controlsButtons=false,controlsAim=false,controlsRoomscale=false,capturePoses=false,controlsHands=false,controlsIndependence=false,trackingRecovery=false,controlsEquip=false,controlsWeaponCycle=false,weaponModeCapture=false,sightGesture=false,sightStartSecondary=false,controlsUse=false,controlsVehicle=false,shotProbe=false,shotReload=false,deathProbe=false,supportProbe=false,supportReload=false,supportPrepareShots=false,reloadRequestProbe=false,physicalReloadProbe=false,weaponVisibilityProbe=false,bodyHolsterProbe=false,sceneRecoveryProbe=false,magazineReloadProbe=false,magazinePhysicalProbe=false;
    bool triggerSmoke=false,triggerSmokeOptionsOnly=true,triggerSmokeComplete=false;probe::TriggerInputSmoke triggerSmokeState;
    bool selectedKnifeMenu=false,selectedKnifeNeutral=false,selectedKnifeNeutralOptionsOnly=true;
    probe::PlayerGrenadeReceiver selectedKnifeMenuReceiver;
    TriggerSmokeJournal triggerSmokeJournal(triggerSmokeState);
    std::uint64_t fadeStartTick=0,fadeNextTick=0;unsigned fadeDurationMs=0,fadeIntervalMs=0,fadePixelPairs=0;
    bool ordinaryGrenadeFade=false,ordinaryGrenadeFadeOptionsOnly=true;probe::GrenadeFadeGesture grenadeFadeGesture;bool grenadeFadeHeld=false;
    bool twoGrenadeFade=false,twoGrenadeOptionsOnly=true;std::uint64_t deathPid=0,deathCreation=0;probe::TwoGrenadeFadeGesture twoGrenadeGesture;bool secondWitnessSaved=false;
    bool normalKnifeClose=false,normalKnifeOptionsOnly=true,noImageFiles=false;std::uint64_t normalKnifePid=0,normalKnifeCreation=0;
    probe::NormalKnifeHeldCloseReceiver normalKnifeReceiver;NormalToolTargetLifetime normalKnifeTarget;
    bool contextualToolProbe=false,toolOptionsOnly=true;std::uint64_t toolPid=0,toolCreation=0;probe::ContextualToolProbeReceiver toolReceiver;
    unsigned chestKnifeProbe=0;bool knifeOptionsOnly=true;std::uint64_t knifePid=0,knifeCreation=0;probe::ChestKnifeProbeReceiver knifeReceiver;
    bool playerGrenadeProbe=false,playerGrenadeOptionsOnly=true;probe::PlayerGrenadeReceiver playerGrenadeReceiver;
    bool ordinaryGrenadeProbe=false,ordinaryGrenadeOptionsOnly=true,ordinaryGrenadeHeld=false;
    unsigned ordinaryGrenadeSamples=0,ordinaryGrenadePresses=0,ordinaryGrenadeReleases=0;
    bool physicalBoltProbe=false,boltEmptyCycle=false,physicalBoltOptionsOnly=true;
    bool m95StockShotNeutral=false,m95OptionsOnly=true;
    bool pumpEmptyCycle=false,physicalPumpProbe=false,physicalPumpOptionsOnly=true;
    bool resourceUseProbe=false,resourceUseOptionsOnly=true;
    bool resourceMagazineProbe=false,resourceMagazineOptionsOnly=true;
    bool emptyFireNeutral=false,emptyFireOptionsOnly=true,pumpHoldProbe=false,pumpOptionsOnly=true,pumpExitVehicle=false;unsigned pumpFireSamples=0;
    bool boatHeadAimProbe=false,boatFireProbe=false,sceneControlsNeutral=false,neutralSceneOptionsOnly=true,bodyCrossDrawProbe=false,magazineOriginalReturnProbe=false,magazineFullReturnProbe=false,originalReturnOptionsOnly=true;
    bool inventoryReloadProbe=false,inventoryOptionsOnly=true,resourceInventoryProbe=false,resourceInventoryOptionsOnly=true,resourceStartHeld=false;
    bool resourceSceneResume=false;probe::ResourceSceneResume sceneResume;std::uint64_t resumePolled=0;
    bool resourceOriginProbe=false,originChanged=false;std::uint64_t originPolled=0;
    bool resourceReceiverGap=false;bc2::PumpReceiverGapProbe receiverGap;
    bool resourceShellFocus=false;bc2::ShellFocusReceiver shellFocus;
    probe::PumpSideCaptureWindow pumpSideCapture;
    std::uint64_t gapLastInput=0,gapPreInput=0;std::int64_t gapLastPublishNs=0,gapPrePublishNs=0;
    const auto gapNow=[](){LARGE_INTEGER q{},f{};QueryPerformanceCounter(&q);QueryPerformanceFrequency(&f);
        return (q.QuadPart/f.QuadPart)*1000000000ll+(q.QuadPart%f.QuadPart)*1000000000ll/f.QuadPart;};
    float roomscaleStepX=.4f,shotCycleDirection=-1,supportPrimaryDirection=0,weaponCycleDirection=1;
    bool weaponCycleDirectionSet=false,weaponCycleRepeat=false,weaponCycleExitVehicle=false;
    fs::path bodyPropsCache,bodyPropsMaterials;bool bodyLookDown=false,bodyViewOptionsOnly=true;
    bool bodyInspection=false;math::Vec3 bodyInspectionContact{};
    for(int i=3;i<argc;++i){const std::wstring option=argv[i];
        selectedKnifeNeutralOptionsOnly=selectedKnifeNeutralOptionsOnly&&probe::SelectedKnifeNeutralOption(option);
        triggerSmokeOptionsOnly=triggerSmokeOptionsOnly&&probe::TriggerInputSmokeOption(option);
        if(option==L"--trigger-input-smoke"){Require(!triggerSmoke,"Select trigger smoke once");triggerSmoke=controlsObserve=true;continue;}
        if(option==L"--selected-knife-trigger-smoke"){
            Require(!triggerSmoke,"Select trigger smoke once");selectedKnifeMenu=triggerSmoke=controlsObserve=true;continue;}
        if(option==L"--selected-knife-approach"){
            Require(i+3<argc,"Approach requires duration milliseconds, strafe and forward");
            const std::wstring duration=argv[++i];std::size_t used=0;const auto ms=std::stoull(duration,&used);
            Require(used==duration.size(),"Integer approach duration required");
            const auto number=[&](){const std::wstring text=argv[++i];std::size_t count=0;const float n=std::stof(text,&count);
                Require(count==text.size()&&std::isfinite(n),"Finite approach stick required");return n;};
            const float strafe=number(),forward=number();
            Require(triggerSmokeState.Approach(ms,strafe,forward),"One approach of1..10000ms with nonzero stick magnitude<=0.5 and forward>=0 required");continue;
        }
        if(option==L"--selected-knife-approach-crouch"){
            Require(triggerSmokeState.ApproachCrouch(),"Select approach crouch once, after the bounded approach option");continue;
        }
        if(option==L"--selected-knife-approach-resume"){
            Require(triggerSmokeState.ApproachResume(),"Select one bounded approach resume after approach, without snap turns");continue;
        }
        if(option==L"--selected-knife-approach-prepared"){
            Require(triggerSmokeState.ApproachPrepared(),"Prepared approach requires motion and measured rotation first, without turn or resume");continue;
        }
        if(option==L"--selected-knife-approach-turn"){
            Require(i+1<argc,"Approach turn requires signed snap count");
            const std::wstring text=argv[++i];std::size_t used=0;const auto count=std::stoi(text,&used);
            Require(used==text.size()&&triggerSmokeState.ApproachTurn(count),"Select approach turn once after approach, signed count -6..6 except zero");continue;
        }
        if(option==L"--selected-knife-review-images"){
            Require(triggerSmokeState.EnableReviewImages(),"Select knife review images once, after the bounded approach option");continue;
        }
        if(option==L"--selected-knife-neutral"){
            Require(!selectedKnifeNeutral,"Select selected knife neutral once");
            selectedKnifeNeutral=selectedKnifeMenu=sceneControlsNeutral=sceneRecoveryProbe=controlsObserve=capturePoses=true;continue;
        }
        if(option==L"--selected-knife-motion-smoke"){
            Require(!triggerSmoke&&i+3<argc,"Selected knife smoke requires one measured LOCAL blade direction");
            const auto number=[&](){const std::wstring text=argv[++i];std::size_t used=0;const float n=std::stof(text,&used);
                Require(used==text.size()&&std::isfinite(n),"Finite knife motion direction required");return n;};
            const float x=number(),y=number(),z=number();Require(triggerSmokeState.Motion({x,y,z}),"Unit blade direction required");
            selectedKnifeMenu=triggerSmoke=controlsObserve=true;continue;
        }
        if(option==L"--selected-knife-motion-rotation"){
            Require(selectedKnifeMenu&&triggerSmokeState.IsMotion()&&i+4<argc,"Rotation must follow selected knife motion and provide quaternion xyzw");
            const auto number=[&](){const std::wstring text=argv[++i];std::size_t used=0;const float n=std::stof(text,&used);
                Require(used==text.size()&&std::isfinite(n),"Finite knife rotation required");return n;};
            const float x=number(),y=number(),z=number(),w=number();
            Require(triggerSmokeState.MotionRotation({x,y,z,w}),"One unit positive-w knife rotation of at most90degrees required");continue;
        }
        normalKnifeOptionsOnly=normalKnifeOptionsOnly&&probe::NormalKnifeHeldCloseOption(option);
        if(option==L"--no-image-files"){
            Require(!noImageFiles,"Select image-file opt-out once");noImageFiles=true;continue;}
        if(option==L"--normal-knife-held-close"){
            Require(!normalKnifeClose&&i+2<argc,"One normal held-close recipe requires exact PID and creation");
            const auto number=[&](){const std::wstring text=argv[++i];std::size_t used=0;const auto n=std::stoull(text,&used);
                Require(!text.empty()&&text.front()!=L'-'&&used==text.size()&&n,"Invalid normal tool process identity");return n;};
            normalKnifePid=number();normalKnifeCreation=number();Require(normalKnifePid<=UINT32_MAX,"Normal tool PID outside DWORD");
            normalKnifeClose=controlsObserve=true;continue;}
        toolOptionsOnly=toolOptionsOnly&&probe::ContextualToolProbeOption(option);
        if(option==L"--contextual-tool-draw-probe"){
            Require(!contextualToolProbe&&i+2<argc,"One contextual fixture requires exact PID and creation time");
            const auto number=[&](){const std::wstring value=argv[++i];std::size_t used=0;const auto n=std::stoull(value,&used);
                Require(!value.empty()&&value.front()!=L'-'&&used==value.size()&&n,"Invalid tool process identity");return n;};
            toolPid=number();toolCreation=number();Require(toolPid<=UINT32_MAX,"Tool PID outside DWORD");
            contextualToolProbe=controlsObserve=true;continue;}
        twoGrenadeOptionsOnly=twoGrenadeOptionsOnly&&probe::TwoGrenadeFadeOption(option);
        if(option==L"--two-grenade-fade-probe"){
            Require(!twoGrenadeFade&&!ordinaryGrenadeProbe&&!playerGrenadeProbe&&i+2<argc,"One two-grenade fixture requires exact PID and creation time");
            const auto number=[&](){const std::wstring value=argv[++i];std::size_t used=0;const auto n=std::stoull(value,&used);Require(!value.empty()&&value.front()!=L'-'&&used==value.size()&&n,"Invalid death process identity");return n;};
            deathPid=number();deathCreation=number();Require(deathPid<=MAXDWORD,"PID outside DWORD");twoGrenadeFade=ordinaryGrenadeFade=controlsObserve=true;continue;
        }
        knifeOptionsOnly=knifeOptionsOnly&&probe::ChestKnifeProbeOption(option);
        if(option==L"--chest-knife-draw-probe"||option==L"--chest-knife-motion-probe"){
            Require(!chestKnifeProbe&&i+2<argc,"Knife receiver requires exact PID and creation time once");
            chestKnifeProbe=option==L"--chest-knife-motion-probe"?2:1;
            const auto number=[&](){const std::wstring value=argv[++i];std::size_t used=0;const auto n=std::stoull(value,&used);
                Require(!value.empty()&&value.front()!=L'-'&&used==value.size()&&n,"Invalid knife process identity");return n;};
            knifePid=number();knifeCreation=number();
            Require(knifePid&&knifePid<=UINT32_MAX&&knifeCreation,"Invalid knife process identity");controlsObserve=true;continue;}
        playerGrenadeOptionsOnly=playerGrenadeOptionsOnly&&probe::PlayerGrenadeOption(option);
        if(option==L"--player-grenade-probe"){Require(!playerGrenadeProbe,"Specify player grenade receiver once");playerGrenadeProbe=controlsObserve=true;continue;}
        ordinaryGrenadeOptionsOnly=ordinaryGrenadeOptionsOnly&&probe::OrdinaryGrenadeOption(option);
        ordinaryGrenadeFadeOptionsOnly=ordinaryGrenadeFadeOptionsOnly&&probe::OrdinaryGrenadeFadeOption(option);
        if(option==L"--ordinary-grenade-probe"||option==L"--ordinary-grenade-fade-probe"){
            Require(!ordinaryGrenadeProbe,"Specify grenade receiver once");ordinaryGrenadeFade=option==L"--ordinary-grenade-fade-probe";
            ordinaryGrenadeProbe=controlsObserve=true;continue;}
        if(option==L"--fade-window"){
            Require(!fadeStartTick&&i+3<argc,"One fade window requires absolute start tick, duration and interval");
            const auto number=[&](){const std::wstring value=argv[++i];std::size_t used=0;
                const auto n=std::stoull(value,&used);Require(used==value.size()&&value.front()!=L'-',"Invalid fade timing");return n;};
            fadeStartTick=number();const auto duration=number(),interval=number();
            Require(fadeStartTick&&duration>=1&&duration<=12000&&interval>=100&&interval<=1000&&
                fadeStartTick<=UINT64_MAX-duration&&(duration+interval-1)/interval<=48,"Fade window outside diagnostic bounds or pixel budget");
            fadeDurationMs=unsigned(duration);fadeIntervalMs=unsigned(interval);fadeNextTick=fadeStartTick;continue;
        }
        if(option==L"--pump-side-capture"){
            Require(i+3<argc,"Pump capture requires start, duration and interval in milliseconds");
            const auto number=[&](){const std::wstring value=argv[++i];std::size_t used=0;
                const auto n=std::stoull(value,&used);Require(!value.empty()&&value.front()!=L'-'&&used==value.size(),"Invalid pump capture timing");return n;};
            const auto begin=number(),duration=number(),interval=number();
            Require(pumpSideCapture.Configure(begin,duration,interval),"Pump capture exceeds one6s/120-pair window");continue;
        }
        // Render-only option: it adds no controller/native simulation input.
        if(option==L"--body-props-cache"){
            Require(bodyPropsCache.empty()&&i+1<argc,"Specify one body geometry cache");bodyPropsCache=fs::absolute(argv[++i]);continue;}
        if(option==L"--body-props-materials"){
            Require(bodyPropsMaterials.empty()&&i+1<argc,"Specify one body material cache");bodyPropsMaterials=fs::absolute(argv[++i]);continue;}
        if(option==L"--body-look-down"){Require(!bodyLookDown,"Specify look-down once");bodyLookDown=true;continue;}
        if(option==L"--body-inspection-contact"){
            Require(!bodyInspection&&i+3<argc,"Specify one inspection contact x y z");
            const auto scalar=[&](){const std::wstring value=argv[++i];std::size_t used=0;
                const float number=std::stof(value,&used);Require(used==value.size(),"Invalid inspection coordinate");return number;};
            const float x=scalar(),y=scalar(),z=scalar();bodyInspectionContact={x,y,z};
            Require(probe::ValidBodyInspectionContact(bodyInspectionContact),"Inspection contact must be finite and within one metre per axis");
            bodyInspection=controlsObserve=true;continue;
        }
        bodyViewOptionsOnly=bodyViewOptionsOnly&&(option==L"--pairs"||option==L"--static-pose"||option==L"--async"||
            option==L"--capture-poses"||option==L"--request-lifetime"||option==L"--controls-observe");
        resourceInventoryOptionsOnly=resourceInventoryOptionsOnly&&(option==L"--resource-shell-focus-loss"||option==L"--resource-receiver-gap"||option==L"--resource-origin-probe"||option==L"--resource-scene-resume"||option==L"--resource-start-held"||option==L"--resource-inventory-probe"||option==L"--pairs"||option==L"--static-pose"||option==L"--async"||option==L"--capture-poses"||option==L"--request-lifetime"||option==L"--weapon-cycle-direction"||option==L"--weapon-cycle-exit-vehicle");
        resourceMagazineOptionsOnly=resourceMagazineOptionsOnly&&(option==L"--resource-magazine-probe"||option==L"--pairs"||
            option==L"--static-pose"||option==L"--async"||option==L"--capture-poses"||option==L"--request-lifetime"||
            option==L"--weapon-cycle-direction"||option==L"--weapon-cycle-exit-vehicle");
        resourceUseOptionsOnly=resourceUseOptionsOnly&&(option==L"--resource-use-probe"||option==L"--pairs"||option==L"--static-pose"||option==L"--async");
        inventoryOptionsOnly=inventoryOptionsOnly&&(option==L"--inventory-reload-probe"||option==L"--inventory-recovery-probe"||option==L"--pairs"||option==L"--static-pose"||option==L"--async"||option==L"--capture-poses");
        physicalBoltOptionsOnly=physicalBoltOptionsOnly&&(option==L"--physical-bolt-probe"||option==L"--bolt-empty-cycle"||option==L"--pairs"||option==L"--static-pose"||option==L"--async"||option==L"--capture-poses"||option==L"--request-lifetime");
        m95OptionsOnly=m95OptionsOnly&&(option==L"--m95-stock-shot-neutral"||option==L"--pairs"||option==L"--static-pose"||option==L"--async"||option==L"--capture-poses"||option==L"--request-lifetime");
        physicalPumpOptionsOnly=physicalPumpOptionsOnly&&probe::PhysicalPumpReceiverOption(option);
        emptyFireOptionsOnly=emptyFireOptionsOnly&&bc2::EmptyFireReceiverOption(option);
        pumpOptionsOnly=pumpOptionsOnly&&(option==L"--pump-exit-vehicle"||option==L"--pump-hold-probe"||option==L"--pairs"||option==L"--static-pose"||option==L"--async"||option==L"--capture-poses"||option==L"--request-lifetime");
        neutralSceneOptionsOnly=neutralSceneOptionsOnly&&probe::NeutralSceneOption(option);
        originalReturnOptionsOnly=originalReturnOptionsOnly&&(option==L"--magazine-original-return-probe"||option==L"--magazine-full-return-probe"||option==L"--pairs"||option==L"--static-pose"||option==L"--async"||option==L"--capture-poses"||option==L"--request-lifetime");
        if(option==L"--pairs"&&i+1<argc){targetPairs=std::stoul(argv[++i]);Require(targetPairs>=8&&targetPairs<=320,"Pair count must be 8..320");}
        else if(option==L"--request-lifetime"&&i+1<argc){requestLifetimeMs=std::stoul(argv[++i]);Require(requestLifetimeMs>=1&&requestLifetimeMs<=200,"Request lifetime must be 1..200 ms");}
        else if(option==L"--roomscale-step-x"&&i+1<argc){roomscaleStepX=std::stof(argv[++i]);Require(std::isfinite(roomscaleStepX)&&std::abs(roomscaleStepX)<=.5f,"Roomscale step must be at most half a metre");}
        else if(option==L"--shot-primary-pulses"&&i+1<argc){shotPrimaryPulses=std::stoul(argv[++i]);Require(shotPrimaryPulses<=2,"Shot primary pulses must be 0, 1 or 2");}
        else if(option==L"--shot-cycle-direction"&&i+1<argc){shotCycleDirection=std::stof(argv[++i]);Require(shotCycleDirection==1||shotCycleDirection==-1,"Shot cycle direction must be -1 or 1");}
        else if(option==L"--weapon-cycle-direction"&&i+1<argc){weaponCycleDirection=std::stof(argv[++i]);weaponCycleDirectionSet=true;Require(weaponCycleDirection==1||weaponCycleDirection==-1,"Weapon cycle direction must be -1 or 1");}
        else if(option==L"--support-primary-direction"&&i+1<argc){supportPrimaryDirection=std::stof(argv[++i]);Require(supportPrimaryDirection==0||supportPrimaryDirection==1||supportPrimaryDirection==-1,"Invalid support selection");}
        else if(option==L"--xm8-magazine-probe"){magazineReloadProbe=controlsObserve=true;}
        else if(option==L"--inventory-reload-probe"||option==L"--inventory-recovery-probe"){Require(!inventoryReloadProbe,"Select one inventory diagnostic");inventoryReloadProbe=magazinePhysicalProbe=controlsObserve=true;}
        else if(option==L"--resource-inventory-probe"){resourceInventoryProbe=inventoryReloadProbe=magazinePhysicalProbe=controlsObserve=capturePoses=true;}
        else if(option==L"--resource-origin-probe"){resourceOriginProbe=true;}
#ifdef FVR_BC2_SHELL_FOCUS_LOSS_PROBE
        else if(option==L"--resource-shell-focus-loss"){Require(!resourceShellFocus,"One shell focus episode only");resourceShellFocus=true;}
#endif
#ifdef FVR_BC2_PUMP_RECEIVER_GAP_PROBE
        else if(option==L"--resource-receiver-gap"){Require(!resourceReceiverGap,"One receiver gap only");resourceReceiverGap=true;}
#endif
        else if(option==L"--resource-start-held")resourceStartHeld=true;
        else if(option==L"--resource-scene-resume")resourceSceneResume=true;
        else if(option==L"--resource-magazine-probe"){resourceMagazineProbe=magazinePhysicalProbe=controlsObserve=capturePoses=true;}
        else if(option==L"--magazine-physical-probe"){magazinePhysicalProbe=controlsObserve=true;}
        else if(option==L"--magazine-original-return-probe"){magazineOriginalReturnProbe=magazinePhysicalProbe=controlsObserve=capturePoses=true;}
        else if(option==L"--magazine-full-return-probe"){magazineFullReturnProbe=magazinePhysicalProbe=controlsObserve=capturePoses=true;}
        else if(option==L"--scene-recovery-probe"){sceneRecoveryProbe=true;}
        else if(option==L"--resource-use-probe"){resourceUseProbe=controlsUse=controlsObserve=capturePoses=true;}
        else if(option==L"--empty-fire-neutral"){emptyFireNeutral=sceneControlsNeutral=controlsObserve=capturePoses=true;}
        else if(option==L"--scene-controls-neutral"){sceneControlsNeutral=sceneRecoveryProbe=controlsObserve=capturePoses=true;}
        else if(option==L"--body-cross-draw-probe"){bodyCrossDrawProbe=controlsObserve=true;}
        else if(option==L"--body-holster-probe"){bodyHolsterProbe=controlsObserve=true;}
        else if(option==L"--weapon-visibility-probe"){weaponVisibilityProbe=controlsObserve=true;}
        else if(option==L"--physical-reload-probe"){physicalReloadProbe=supportReload=supportProbe=controlsHands=controlsObserve=capturePoses=true;}
        else if(option==L"--reload-request-probe"){reloadRequestProbe=supportReload=supportProbe=controlsHands=controlsObserve=capturePoses=true;}
        else if(option==L"--pump-exit-vehicle"){pumpExitVehicle=true;}
        else if(option==L"--pump-empty-cycle"){pumpEmptyCycle=true;}
        else if(option==L"--bolt-empty-cycle"){Require(!boltEmptyCycle,"Select empty bolt once");boltEmptyCycle=true;}
        else if(option==L"--physical-bolt-probe"){Require(!physicalBoltProbe,"Select physical bolt once");physicalBoltProbe=controlsObserve=capturePoses=true;}
        else if(option==L"--m95-stock-shot-neutral"){m95StockShotNeutral=controlsObserve=capturePoses=true;}
        else if(option==L"--physical-pump-probe"){physicalPumpProbe=controlsObserve=capturePoses=true;}
        else if(option==L"--pump-hold-probe"){pumpHoldProbe=controlsObserve=capturePoses=true;}
        else if(option==L"--support-prepare-shots")supportPrepareShots=true;
        else if(option==L"--controls-support-reload"){supportReload=supportProbe=controlsHands=controlsObserve=capturePoses=true;}
        else if(option==L"--controls-support-grip"){supportProbe=controlsHands=controlsObserve=capturePoses=true;}
        else if(option==L"--shot-reload")shotReload=true;
        else if(option==L"--controls-shot-probe"){shotProbe=controlsHands=controlsObserve=capturePoses=true;}
        else if(option==L"--controls-sight-flip"){sightGesture=weaponModeCapture=controlsHands=controlsObserve=capturePoses=true;}
        else if(option==L"--sight-start-secondary")sightStartSecondary=true;
        else if(option==L"--controls-weapon-mode"){weaponModeCapture=controlsHands=controlsObserve=capturePoses=true;}
        else if(option==L"--controls-weapon-cycle"){controlsWeaponCycle=controlsHands=controlsObserve=capturePoses=true;}
        else if(option==L"--weapon-cycle-repeat")weaponCycleRepeat=true;
        else if(option==L"--weapon-cycle-exit-vehicle")weaponCycleExitVehicle=true;
        else if(option==L"--controls-use"){controlsUse=controlsObserve=true;}
        else if(option==L"--controls-vehicle"){controlsVehicle=controlsObserve=true;}
        else if(option==L"--boat-head-aim-probe"){boatHeadAimProbe=controlsVehicle=controlsObserve=true;}
        else if(option==L"--boat-fire-probe"){boatFireProbe=boatHeadAimProbe=controlsVehicle=controlsObserve=true;}
        else if(option==L"--controls-equip"){controlsEquip=controlsHands=controlsObserve=capturePoses=true;}
        else if(option==L"--controls-tracking-recovery"){trackingRecovery=controlsHands=controlsObserve=capturePoses=true;}
        else if(option==L"--controls-death-probe"){deathProbe=controlsObserve=capturePoses=true;}
        else if(option==L"--controls-hand-independence"){controlsIndependence=controlsHands=controlsObserve=capturePoses=true;}
        else if(option==L"--controls-hands"){controlsHands=controlsObserve=capturePoses=true;}
        else if(option==L"--capture-poses"){capturePoses=true;}
        else if(option==L"--controls-roomscale"){controlsObserve=controlsRoomscale=true;}
        else if(option==L"--controls-aim"){controlsObserve=true;controlsAim=true;}else if(option==L"--controls-buttons"){controlsObserve=true;controlsButtons=true;}else if(option==L"--controls-turn"){controlsObserve=true;controlsTurn=true;}else if(option==L"--controls-fire"){controlsObserve=true;controlsFire=true;}else if(option==L"--controls-pulse"){controlsObserve=true;controlsPulse=true;}else if(option==L"--controls-observe")controlsObserve=true;else if(option==L"--asymmetric")asymmetric=true;else if(option==L"--static-pose")staticPose=true;else if(option==L"--async")async=true;else if(option==L"--fov-sweep")fovSweep=true;
        else throw std::runtime_error("Unknown native probe option");}
    Require(targetPairs<=240||triggerSmokeState.IsApproach(),"More than240 pairs requires the bounded approach recipe");
    Require(!triggerSmokeState.IsApproach()||(triggerSmoke&&selectedKnifeMenu),"Approach requires selected knife trigger or motion smoke");
    Require(!triggerSmoke||probe::TriggerInputSmokeArguments(triggerSmoke,staticPose,async,targetPairs,triggerSmokeOptionsOnly,triggerSmokeState.IsMotion(),noImageFiles,triggerSmokeState.IsApproach(),triggerSmokeState.ReviewImagesRequested()),
        "Trigger smoke requires isolated static async input; no other gesture/capture modes");
    Require(!selectedKnifeNeutral||probe::SelectedKnifeNeutralArguments(selectedKnifeNeutral,staticPose,async,targetPairs,selectedKnifeNeutralOptionsOnly),
        "Selected knife neutral requires isolated static async240; no sidecar or other input modes");
    Require(!noImageFiles||normalKnifeClose||resourceInventoryProbe||triggerSmokeState.IsMotion()||triggerSmokeState.IsApproach(),"Image-file opt-out requires an isolated supported recipe");
    Require(!triggerSmokeState.ReviewImagesRequested()||(triggerSmoke&&selectedKnifeMenu&&triggerSmokeState.IsApproach()&&!noImageFiles&&!capturePoses),"Knife review images require isolated approach without other image options");
    Require(!normalKnifeClose||(normalKnifeOptionsOnly&&!contextualToolProbe&&!chestKnifeProbe&&!playerGrenadeProbe&&!ordinaryGrenadeProbe&&
        !twoGrenadeFade&&staticPose&&async&&targetPairs==240&&requestLifetimeMs==150),"Normal held-close requires isolated static async240");
    Require(!normalKnifeClose||normalKnifeTarget.Open(normalKnifePid,normalKnifeCreation),"Normal held-close original game identity differs");
#ifndef FVR_BC2_CONTEXTUAL_TOOLS
    Require(!contextualToolProbe,"Contextual receiver requires its explicit private build");
#endif
    Require(!contextualToolProbe||(toolOptionsOnly&&!chestKnifeProbe&&!playerGrenadeProbe&&!twoGrenadeFade&&!ordinaryGrenadeProbe&&
        staticPose&&async&&targetPairs==240&&requestLifetimeMs==150),"Contextual receiver requires isolated static async240 and exact identity");
#ifndef FVR_BC2_CHEST_KNIFE_PROBE
    Require(!chestKnifeProbe,"Knife receiver requires its explicit private build");
#endif
    Require(!chestKnifeProbe||(knifeOptionsOnly&&staticPose&&async&&targetPairs==240&&requestLifetimeMs==150),
        "Knife receiver requires isolated static async240 with exact identity");
    Require(!twoGrenadeFade||(twoGrenadeOptionsOnly&&!playerGrenadeProbe&&!ordinaryGrenadeProbe&&staticPose&&async&&targetPairs==240&&requestLifetimeMs==150),"Two-grenade receiver requires isolated static async240 and exact identity");
    Require(!playerGrenadeProbe||(playerGrenadeOptionsOnly&&!ordinaryGrenadeProbe&&staticPose&&async&&targetPairs==240&&requestLifetimeMs==150),
        "Player grenade receiver requires isolated static async240 pairs with actual native menu state");
    Require(!boltEmptyCycle||physicalBoltProbe,"Empty bolt requires physical bolt receiver");
    Require(!physicalBoltProbe||(physicalBoltOptionsOnly&&staticPose&&async&&targetPairs==240&&requestLifetimeMs==150),"Physical bolt receiver requires isolated30s neutral stream");
    Require(!m95StockShotNeutral||(m95OptionsOnly&&staticPose&&async&&targetPairs==240&&requestLifetimeMs==150),"M95 stock-shot receiver requires isolated15s neutral stream");
    Require(!resourceUseProbe||(resourceUseOptionsOnly&&staticPose&&async&&targetPairs==240),"Resource-use receiver requires isolated static async240-pair stream; preflight vehicle before use");
    Require(!resourceMagazineProbe||(resourceMagazineOptionsOnly&&staticPose&&async&&targetPairs==240),"Resource magazine receiver requires isolated static async240-pair stream and verified setup route");
    Require(!resourceInventoryProbe||(resourceInventoryOptionsOnly&&staticPose&&async&&targetPairs==240&&requestLifetimeMs==150),"Resource inventory receiver requires finite60s static async240 pairs and isolated setup input");
    Require(!pumpSideCapture.Enabled()||(resourceInventoryProbe&&resourceStartHeld&&resourceInventoryOptionsOnly&&
        !resourceReceiverGap&&!resourceShellFocus&&!resourceSceneResume&&!resourceOriginProbe&&!fadeStartTick),
        "Pump capture requires isolated ordinary held-start resource input");
    Require(probe::ValidResourceMagazineStartHeld(resourceStartHeld,resourceInventoryProbe,weaponCycleExitVehicle,weaponCycleDirectionSet),"Resource start-held requires resource inventory mode without vehicle-exit or selection options");
    Require(!resourceSceneResume||(resourceInventoryProbe&&resourceStartHeld&&resourceInventoryOptionsOnly),"Scene resume requires isolated resource inventory start-held mode");
#ifndef FVR_BC2_SHELL_FOCUS_LOSS_PROBE
    Require(!resourceShellFocus,"Shell focus episode requires its private build");
#endif
    Require(!resourceShellFocus||(resourceInventoryProbe&&resourceStartHeld&&!resourceReceiverGap&&!resourceSceneResume&&!resourceOriginProbe&&resourceInventoryOptionsOnly),"Shell focus requires isolated held-start resource input");
    Require(!resourceReceiverGap||(resourceInventoryProbe&&resourceStartHeld&&!resourceSceneResume&&!resourceOriginProbe&&resourceInventoryOptionsOnly),"Receiver gap requires isolated held-start resource input");
    Require(!resourceOriginProbe||(resourceInventoryProbe&&resourceStartHeld&&!resourceSceneResume&&resourceInventoryOptionsOnly),"Origin probe requires isolated resource inventory start-held mode");
    Require(!weaponCycleDirectionSet||controlsWeaponCycle||resourceMagazineProbe||resourceInventoryProbe,"Weapon cycle direction requires its controller fixture");
    Require(!weaponCycleRepeat||controlsWeaponCycle,"Repeated weapon cycle requires its controller fixture");
    Require(!weaponCycleExitVehicle||resourceInventoryProbe||resourceMagazineProbe||(controlsWeaponCycle&&weaponCycleRepeat),"Vehicle exit requires the repeated weapon-cycle fixture");
    Require(!emptyFireNeutral||(emptyFireOptionsOnly&&staticPose&&async&&targetPairs==240),"Empty-fire receiver requires isolated neutral static async240-pair stream");
    Require(!inventoryReloadProbe||(resourceInventoryProbe?resourceInventoryOptionsOnly:inventoryOptionsOnly),"Combined inventory/reload receiver permits only neutral controller input");
    Require(!(magazineOriginalReturnProbe&&magazineFullReturnProbe)&&(!(magazineOriginalReturnProbe||magazineFullReturnProbe)||originalReturnOptionsOnly),"Select one magazine return fixture without other scripted actions");
    Require(!pumpExitVehicle||pumpHoldProbe||physicalPumpProbe,"Pump exit requires the isolated pump fixture and verified boat setup");
    Require(!pumpEmptyCycle||physicalPumpProbe,"Empty pump stream requires the isolated physical pump fixture");
    Require(!physicalPumpProbe||(physicalPumpOptionsOnly&&staticPose&&async&&targetPairs==240&&requestLifetimeMs==150),"Physical pump receiver requires isolated30s static async240-pair stream");
    Require(!pumpHoldProbe||(pumpOptionsOnly&&staticPose&&async&&targetPairs==240&&requestLifetimeMs==150),"Pump hold receiver requires isolated15s static async240-pair stream");
    Require(!bodyCrossDrawProbe||(staticPose&&async&&targetPairs==240&&!magazinePhysicalProbe&&!magazineReloadProbe&&!sceneRecoveryProbe&&!weaponVisibilityProbe&&!bodyHolsterProbe&&!reloadRequestProbe&&!physicalReloadProbe&&!supportPrepareShots&&!supportProbe&&!shotProbe&&!shotReload&&!supportReload&&!controlsHands&&!controlsFire&&!deathProbe&&!controlsEquip&&!controlsWeaponCycle&&!controlsPulse&&!controlsUse&&!controlsButtons&&!controlsTurn&&!controlsRoomscale&&!trackingRecovery&&!controlsIndependence&&!controlsAim&&!weaponModeCapture&&!controlsVehicle&&!asymmetric&&!fovSweep&&!sightStartSecondary),"Body cross-draw requires isolated14s static async240-pair receiver gestures");
    Require(!magazinePhysicalProbe||(staticPose&&async&&targetPairs==240&&!magazineReloadProbe&&!sceneRecoveryProbe&&!weaponVisibilityProbe&&!bodyHolsterProbe&&!reloadRequestProbe&&!physicalReloadProbe&&!supportPrepareShots&&!supportProbe&&!shotProbe&&!controlsHands&&!controlsFire&&!deathProbe&&!controlsEquip&&!controlsWeaponCycle&&!controlsPulse&&!controlsUse&&!controlsButtons&&!controlsTurn&&!controlsRoomscale&&!trackingRecovery&&!controlsIndependence&&!controlsAim&&!weaponModeCapture&&!controlsVehicle&&!asymmetric&&!fovSweep),"Physical magazine fixture requires isolated30s neutral static async240-pair stream");
    Require(!magazineReloadProbe||(staticPose&&async&&targetPairs==240&&!sceneRecoveryProbe&&!weaponVisibilityProbe&&!bodyHolsterProbe&&!reloadRequestProbe&&!physicalReloadProbe&&!supportPrepareShots&&!supportProbe&&!shotProbe&&!controlsHands&&!controlsFire&&!deathProbe&&!controlsEquip&&!controlsWeaponCycle&&!controlsPulse&&!controlsUse&&!controlsButtons&&!controlsTurn&&!controlsRoomscale&&!trackingRecovery&&!controlsIndependence&&!controlsAim&&!weaponModeCapture&&!controlsVehicle&&!asymmetric&&!fovSweep),"Magazine fixture requires isolated30s static async240-pair stream");
    Require(!sceneRecoveryProbe||(sceneControlsNeutral?probe::NeutralSceneArguments(sceneRecoveryProbe,staticPose,async,targetPairs,neutralSceneOptionsOnly):(staticPose&&async&&targetPairs==240&&!controlsObserve&&!capturePoses&&!weaponVisibilityProbe&&!asymmetric&&!fovSweep)),"Scene recovery probe requires static async240-pair stream; neutral controllers reject every action script");
    Require(!(weaponVisibilityProbe&&bodyHolsterProbe),"Select one visibility/body fixture");
    Require(!(weaponVisibilityProbe||bodyHolsterProbe)||(staticPose&&async&&targetPairs==240&&!reloadRequestProbe&&!physicalReloadProbe&&!supportPrepareShots&&!supportProbe&&!shotProbe&&!controlsHands&&!controlsFire&&!deathProbe&&!controlsEquip&&!controlsWeaponCycle&&!controlsPulse&&!controlsUse&&!controlsButtons&&!controlsTurn&&!controlsRoomscale&&!trackingRecovery&&!controlsIndependence&&!controlsAim&&!weaponModeCapture&&!controlsVehicle&&!asymmetric&&!fovSweep),"Visibility probe requires neutral static async240-pair stream");
    Require(!supportPrepareShots||(supportReload&&supportPrimaryDirection==0&&!shotProbe&&!controlsFire&&!controlsPulse&&!deathProbe&&!weaponModeCapture),"Support preparation requires an already selected reload fixture without other actions");
    Require(!(reloadRequestProbe&&physicalReloadProbe),"Select one reload fixture");
    Require(!(reloadRequestProbe||physicalReloadProbe)||(staticPose&&async&&targetPairs==240&&supportPrimaryDirection==0&&!controlsFire&&!shotProbe&&!deathProbe&&!controlsEquip&&!controlsWeaponCycle&&!controlsPulse&&!controlsUse&&!controlsButtons&&!controlsTurn&&!controlsRoomscale&&!trackingRecovery&&!controlsIndependence&&!controlsAim&&!weaponModeCapture&&!controlsVehicle&&!asymmetric&&!fovSweep),"Request fixture requires isolated30s static async240-pair observation");
    Require(!sightStartSecondary||sightGesture,"Sight start-secondary requires controls-sight-flip");
    Require(!sightGesture||staticPose,"Measured sight fixture requires static-pose");
    Require(!weaponModeCapture||(staticPose&&async&&targetPairs==240),"Weapon mode capture requires static async 240-pair acquisition");
    Require(!weaponModeCapture||!(controlsFire||shotProbe||deathProbe||controlsEquip||controlsWeaponCycle||controlsPulse||controlsUse||controlsButtons||controlsTurn||controlsRoomscale||supportProbe||trackingRecovery||controlsIndependence||controlsAim),"Weapon mode capture cannot combine with other action scripts");
    Require(!controlsVehicle||(staticPose&&async&&targetPairs==240&&!controlsFire&&!shotProbe&&!deathProbe&&!controlsEquip&&!controlsWeaponCycle&&!controlsPulse&&!controlsUse&&!controlsButtons&&!controlsTurn&&!controlsRoomscale&&!supportProbe&&!trackingRecovery&&!controlsIndependence&&!controlsAim&&!controlsHands&&!weaponModeCapture&&!asymmetric&&!fovSweep),"Vehicle fixture requires isolated static async240-pair observation");
    Require(!bodyLookDown||(!bodyPropsCache.empty()&&staticPose&&async&&controlsObserve&&bodyViewOptionsOnly),
        "Body look-down requires isolated static async body composition; only the explicit inspection contact may add a grab");
    Require(!bodyInspection||(bodyLookDown&&targetPairs==240),"Body inspection requires look-down and240 paced pairs");
    Require(!fadeStartTick||((twoGrenadeFade?twoGrenadeOptionsOnly:ordinaryGrenadeFade?ordinaryGrenadeFadeOptionsOnly:
        (sceneRecoveryProbe&&neutralSceneOptionsOnly&&!controlsObserve))&&staticPose&&async&&targetPairs==240&&
        bodyPropsCache.empty()&&bodyPropsMaterials.empty()&&!bodyLookDown&&!bodyInspection),
        "Fade pixels require an isolated scene or combined ordinary grenade receiver");
    Require(!ordinaryGrenadeFade||(twoGrenadeFade?probe::TwoGrenadeFadeWindow(fadeStartTick,fadeDurationMs,fadeIntervalMs):probe::GrenadeFadeWindow(fadeStartTick,fadeDurationMs,fadeIntervalMs)),
        "Combined grenade observation requires an8s window with250ms paired pixels");
    if(fadeStartTick){const auto now=GetTickCount64();Require(fadeStartTick>=now&&fadeStartTick-now<=60000,"Fade start must be within the next60s");}
    Require(!ordinaryGrenadeProbe||((twoGrenadeFade?twoGrenadeOptionsOnly:ordinaryGrenadeFade?ordinaryGrenadeFadeOptionsOnly:ordinaryGrenadeOptionsOnly)&&staticPose&&async&&targetPairs==240),"Grenade requires exclusive static async240 receiver");
    const auto folder=fs::absolute(argv[2]);Require(!fs::exists(folder),"New output directory required");fs::create_directories(folder);
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0};
    Hr(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,1,D3D11_SDK_VERSION,&device,&level,&context),"Create receiver device");
    ComPtr<IDXGIDevice> dxgi;Hr(device.As(&dxgi),"Receiver DXGI");ComPtr<IDXGIAdapter> adapter;Hr(dxgi->GetAdapter(&adapter),"Receiver adapter");DXGI_ADAPTER_DESC gpu{};Hr(adapter->GetDesc(&gpu),"GPU identity");
    runtime::PresentationRequirements requirements{1920,1080,29,gpu.AdapterLuid.LowPart,gpu.AdapterLuid.HighPart};
    D3D11_TEXTURE2D_DESC description{};description.Width=1920;description.Height=1080;description.MipLevels=1;description.ArraySize=2;description.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;description.SampleDesc.Count=1;description.Usage=D3D11_USAGE_DEFAULT;
    ComPtr<ID3D11Texture2D> output,staging;Hr(device->CreateTexture2D(&description,nullptr,&output),"Receiver eyes");description.ArraySize=1;description.Format=DXGI_FORMAT_R8G8B8A8_UNORM;description.Usage=D3D11_USAGE_STAGING;description.CPUAccessFlags=D3D11_CPU_ACCESS_READ;Hr(device->CreateTexture2D(&description,nullptr,&staging),"Receiver staging");
    graphics::D3D11SceneDepthConsumer sceneDepth;Require(sceneDepth.Initialize(device.Get()),"Initialize depth receiver");
    graphics::D3D11BodyPropCompositor bodyProps;
    std::array<ComPtr<ID3D11Texture2D>,2> composedEyes;ComPtr<ID3D11Texture2D> nativeOutput;
    std::unique_ptr<probe::DiagnosticImageWriter> nativeImageWriter;
    Require(bodyPropsMaterials.empty()||!bodyPropsCache.empty(),"Body materials require body geometry");
    if(!bodyPropsCache.empty()){
        const auto cache=bc2::LoadBodyAmmoGeometryCache(bodyPropsCache);Require(bool(cache.catalog),"Load verified private body geometry");
        auto catalog=std::make_shared<graphics::BodyPropCatalog>();for(const auto& asset:*cache.catalog)catalog->push_back({asset->key,asset->sections});
        std::shared_ptr<const graphics::BodyPropCatalog> renderCatalog=catalog;
        if(!bodyPropsMaterials.empty()){
            const auto material=graphics::LoadBodyPropMaterials(bodyPropsMaterials,*catalog);Require(bool(material.catalog),material.error);
            renderCatalog=material.catalog;
            std::ofstream record(folder/L"materials-result.json");record<<"{\"loaded\":true,\"textures\":"<<material.textures<<",\"sections\":"<<material.sections<<"}\n";
            Require(bool(record),"Save body material result");}
        Require(bodyProps.Initialize(context.Get(),1920,1080,requirements.format,renderCatalog),"Initialize body compositor");
        D3D11_TEXTURE2D_DESC d{};d.Width=1920;d.Height=1080;d.ArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT(requirements.format);
        for(auto& eye:composedEyes)Hr(device->CreateTexture2D(&d,nullptr,&eye),"Receiver prop eyes");
        output->GetDesc(&d);Hr(device->CreateTexture2D(&d,nullptr,&nativeOutput),"Receiver native comparison");
        fs::create_directories(folder/L"native");nativeImageWriter=std::make_unique<probe::DiagnosticImageWriter>(folder/L"native");
    }
    Require(!(noImageFiles||triggerSmokeState.ReviewImagesRequested())||!nativeImageWriter,"Bounded knife images cannot include native/composed image capture");
    ipc::RemoteFrameProvider host;Require(host.Create()&&host.SetBudget(50)&&host.SetRequestLifetime(requestLifetimeMs),"Create native receiver channel");
    ipc::MenuChannel playerMenu;LARGE_INTEGER playerMenuFrequency{};
    if(selectedKnifeMenu||normalKnifeClose||contextualToolProbe||playerGrenadeProbe||twoGrenadeFade||chestKnifeProbe)Require(playerMenu.CreateHost(host.Token())&&QueryPerformanceFrequency(&playerMenuFrequency)&&
        playerMenuFrequency.QuadPart>0&&playerMenuFrequency.QuadPart<=1000000000,"Create actual player menu observation lane");
    {std::wofstream token(folder/L"channel.txt");token<<host.Token();Require(bool(token),"Write channel token");}
    const auto start=GetTickCount64();Require(ipc::WaitForConnection([&]{return host.Connected();},
        []{return GetTickCount64();},[]{Sleep(2);},start,15000),"Native producer connection timed out");
    const auto controlsStarted=GetTickCount64();
    if(triggerSmoke)Require(triggerSmokeJournal.Open(folder,controlsStarted,targetPairs),"Save trigger start before input");
    if(ordinaryGrenadeFade){
        const bool leadOkay=probe::GrenadeFadeLead(fadeStartTick,controlsStarted);
        std::ofstream timing(folder/L"grenade-fade-startup.json");
        timing<<"{\"connection_wait_started_ms\":"<<start<<",\"connected_tick_ms\":"<<controlsStarted
              <<",\"window_start_tick_ms\":"<<fadeStartTick<<",\"window_end_tick_ms\":"<<(fadeStartTick+fadeDurationMs)
              <<",\"lead_ms\":"<<(fadeStartTick>=controlsStarted?fadeStartTick-controlsStarted:0)
              <<",\"late_ms\":"<<(controlsStarted>fadeStartTick?controlsStarted-fadeStartTick:0)
              <<",\"minimum_lead_ms\":4000,\"maximum_lead_ms\":12000,\"input_publications\":0,\"lead_accepted\":"<<(leadOkay?"true":"false")<<"}\n";
        timing.flush();Require(bool(timing),"Save combined startup timing before input");
        Require(leadOkay,"Combined window must start4..12s after connection, after setup and before producer drain");
    }
    probe::BoatFireProbe boatFireSchedule;unsigned boatFireInputSamples=0;std::uint64_t boatFireFirstInputMs=0,boatFireLastInputMs=0;
    unsigned consumed=0,timeouts=0,gpuBusy=0;std::uint64_t lastFrame=0;graphics::D3D11PairConsumer consumer;graphics::TextureDescriptor opened{};
    std::shared_ptr<const probe::DiagnosticImageWriter::Pixels> prior;
    probe::DiagnosticImageWriter imageWriter(folder);
    std::ofstream records(folder/L"pairs.jsonl");
    std::ofstream presentationRecords(folder/L"presentation.jsonl");
    std::ofstream depthRecords(folder/L"scene-depth.jsonl");unsigned depthReadPairs=0,depthCopiedPairs=0;
    Require(bool(presentationRecords),"Open delivered presentation evidence");
    graphics::AmmoCounterPresentation ammoPresentation;
    std::ofstream neutralInputRecords;unsigned neutralInputSamples=0;
    std::ofstream bodyInspectionRecords;unsigned bodyInspectionSamples=0;
    if(bodyInspection){bodyInspectionRecords.open(folder/L"body-inspection-input.jsonl");
        Require(bool(bodyInspectionRecords),"Open body inspection input log");}
    if(ordinaryGrenadeFade||physicalBoltProbe||m95StockShotNeutral||resourceInventoryProbe||physicalPumpProbe||pumpHoldProbe||sceneControlsNeutral||resourceMagazineProbe||resourceUseProbe||magazineOriginalReturnProbe||magazineFullReturnProbe){neutralInputRecords.open(folder/(ordinaryGrenadeFade?L"grenade-fade-input.jsonl":physicalBoltProbe?L"physical-bolt-input.jsonl":m95StockShotNeutral?L"m95-stock-shot-input.jsonl":resourceInventoryProbe?L"resource-inventory-input.jsonl":physicalPumpProbe?L"physical-pump-input.jsonl":pumpHoldProbe?L"pump-input.jsonl":resourceMagazineProbe?L"resource-magazine-input.jsonl":resourceUseProbe?L"resource-use-input.jsonl":magazineFullReturnProbe?L"full-return-input.jsonl":magazineOriginalReturnProbe?L"original-return-input.jsonl":L"scene-input.jsonl"));Require(bool(neutralInputRecords),"Open neutral scene input log");}
    for(std::uint64_t attempt=1;(triggerSmoke?triggerSmokeState.ContinueInput(GetTickCount64()-controlsStarted):normalKnifeClose?GetTickCount64()-controlsStarted<probe::NormalKnifeHeldCloseReceiver::DurationMs:(consumed<targetPairs||(contextualToolProbe&&GetTickCount64()-controlsStarted<probe::ContextualToolProbeReceiver::DurationMs)||(chestKnifeProbe&&GetTickCount64()-controlsStarted<probe::ChestKnifeProbeReceiver::DurationMs)||(twoGrenadeFade&&GetTickCount64()<fadeStartTick+fadeDurationMs+500)||(playerGrenadeProbe&&GetTickCount64()-controlsStarted<probe::PlayerGrenadeReceiver::DurationMs)||(ordinaryGrenadeProbe&&GetTickCount64()-controlsStarted<18500ull)||(m95StockShotNeutral&&GetTickCount64()-controlsStarted<15000ull)||(weaponCycleRepeat&&GetTickCount64()-controlsStarted<13500ull)||(inventoryReloadProbe&&GetTickCount64()-controlsStarted<60000ull)||((physicalBoltProbe||physicalPumpProbe||emptyFireNeutral||resourceUseProbe)&&GetTickCount64()-controlsStarted<((pumpEmptyCycle||boltEmptyCycle)?60000ull:30000ull))||((magazineReloadProbe||magazinePhysicalProbe)&&GetTickCount64()-controlsStarted<30000ull)||((shotProbe||supportProbe)&&GetTickCount64()-controlsStarted<((reloadRequestProbe||physicalReloadProbe)?30000ull:11000ull))||(weaponModeCapture&&GetTickCount64()-controlsStarted<13000)||(controlsVehicle&&GetTickCount64()-controlsStarted<6500)))&&GetTickCount64()-start<((inventoryReloadProbe||pumpEmptyCycle||boltEmptyCycle)?75000ull:(physicalBoltProbe||physicalPumpProbe||emptyFireNeutral||resourceUseProbe)?45000ull:twoGrenadeFade?35000ull:sceneRecoveryProbe?70000ull:(reloadRequestProbe||physicalReloadProbe||magazineReloadProbe||magazinePhysicalProbe)?45000ull:triggerSmokeState.IsApproach()?40000ull:25000ull);++attempt){
        if(resourceShellFocus&&!shellFocus.Began()){
            std::ifstream ready(folder.parent_path()/L"shell-focus-ready.txt");
            if(ready){bc2::ShellFocusWitness w;std::string extra;
                Require(bool(ready>>w.space>>w.cycle>>w.input>>w.observedNs>>w.deadlineNs>>w.completedObservedNs
                    >>w.loaded>>w.reserve>>w.capacity)&&!(ready>>extra),"Malformed shell focus witness");
                Require(w.space==0x200000001ULL&&shellFocus.Begin(w,gapNow()),"Stale shell focus witness");
            }
        }
        if(resourceReceiverGap&&!receiverGap.Began()){
            std::ifstream ready(folder.parent_path()/L"pump-receiver-gap-ready.txt");
            if(ready){bc2::PumpReceiverGapWitness w;std::string extra;
                Require(bool(ready>>w.space>>w.cycle>>w.shot>>w.input>>w.observedNs>>w.deadlineNs>>w.loaded>>w.reserve>>w.capacity>>w.travel)&&!(ready>>extra),"Malformed receiver gap witness");
                Require(w.space==0x200000001ULL&&gapLastInput>=w.input&&gapLastPublishNs>0&&receiverGap.Begin(w,gapNow()),"Stale or invalid receiver gap witness");
                gapPreInput=gapLastInput;gapPrePublishNs=gapLastPublishNs;
            }
        }
        if(resourceReceiverGap&&receiverGap.Withhold(gapNow())){
            Require(!receiverGap.Failed(),"Receiver gap clock reversed");Sleep(5);continue;
        }
        if(resourceOriginProbe&&!originChanged&&GetTickCount64()-originPolled>=25){
            originPolled=GetTickCount64();std::ifstream ready(folder.parent_path()/L"pump-origin-ready.txt");
            if(ready){std::uint64_t space=0,cycle=0,shot=0;std::string extra;
                Require(bool(ready>>space>>cycle>>shot)&&!(ready>>extra)&&space==0x200000001ULL&&cycle&&shot,"Invalid pump origin observation");
                originChanged=true;
                std::ofstream receipt(folder/L"origin-change.json");
                receipt<<"{\"old_space\":"<<space<<",\"new_space\":"<<(space+1)<<",\"cycle\":"<<cycle<<",\"shot\":"<<shot
                       <<",\"input_sequence\":"<<attempt<<",\"elapsed_ms\":"<<(GetTickCount64()-controlsStarted)<<",\"native_authority\":false}\n";
                receipt.flush();Require(bool(receipt),"Save origin change receipt");
            }
        }
        runtime::TrackingFrame tracking{};tracking.generation=0x100000000ULL+attempt;tracking.spaceGeneration=receiverGap.Space(0x200000001ULL+(originChanged?1:0));tracking.predictedNs=10000000000LL+std::int64_t(attempt)*11000000;
        tracking.focused=tracking.headValid=true;if(resourceShellFocus)tracking.focused=shellFocus.Focused(gapNow());const float angle=staticPose?0:float(consumed)*.025f;tracking.head.orientation={0,std::sin(angle*.5f),0,std::cos(angle*.5f)};tracking.head.position.x=staticPose?0:float(consumed)*.035f;
        if(controlsRoomscale){const auto ms=GetTickCount64()-controlsStarted;tracking.head={};tracking.head.position.x=(ms>=500&&ms<4000)?roomscaleStepX:0;}
        if(boatHeadAimProbe){const auto ms=GetTickCount64()-controlsStarted;
            // Aim-only sweeps; the separate fire fixture remains completely stationary.
            const float yaw=boatFireProbe?0:(ms>=1000&&ms<2000)?.1745329252f:(ms>=3000&&ms<4000)?-.1745329252f:0;
            const float pitch=boatFireProbe?0:(ms>=4000&&ms<5000)?.0872664626f:0;
            tracking.head={};tracking.head.orientation={std::cos(yaw*.5f)*std::sin(pitch*.5f),
                std::sin(yaw*.5f)*std::cos(pitch*.5f),-std::sin(yaw*.5f)*std::sin(pitch*.5f),std::cos(yaw*.5f)*std::cos(pitch*.5f)};
        }
        else if(controlsVehicle){const auto ms=GetTickCount64()-controlsStarted;
            // Enter with accumulated on-foot displacement/yaw, then lean10cm.
            tracking.head.position.x=(ms>=3500&&ms<5000)?.7f:.6f;
            tracking.head.orientation={0,std::sin(.1745329252f),0,std::cos(.1745329252f)};
        }
        if(bodyLookDown)tracking.head.orientation={-std::sin(.45f),0,0,std::cos(.45f)};
        tracking.eyes={tracking.head,tracking.head};tracking.eyes[0].position.x-=.032f;tracking.eyes[1].position.x+=.032f;tracking.fov={math::FovTangents{-1,1,.8f,-.8f},math::FovTangents{-1,1,.8f,-.8f}};
        if(asymmetric)tracking.fov={math::FovTangents{-1.2f,.8f,.9f,-.9f},math::FovTangents{-.8f,1.2f,.9f,-.9f}};
        if(fovSweep){
            const math::FovTangents symmetric{-1,1,.9f,-.9f},left{-1.2f,.8f,.9f,-.9f},right{-.8f,1.2f,.9f,-.9f};
            switch(consumed%4){case 0:tracking.fov={symmetric,symmetric};break;case 1:tracking.fov={left,right};break;
                case 2:tracking.fov={left,left};break;case 3:tracking.fov={right,right};break;}
        }
        if(controlsObserve){interaction::InputFrame controls{};controls.generation=attempt;controls.spaceGeneration=tracking.spaceGeneration;
            controls.predictedNs=tracking.predictedNs;controls.focused=controls.headValid=true;controls.referenceHead=tracking.referenceHead;controls.head=tracking.head;
            for(auto& hand:controls.hands){hand.gripTracked=hand.aimTracked=true;hand.active=interaction::Components;}
            const auto elapsed=GetTickCount64()-controlsStarted;
            probe::ResourceMagazineSetup resourceSetup{};
            if(physicalBoltProbe||m95StockShotNeutral||sceneControlsNeutral||resourceUseProbe||bodyLookDown)controls=probe::NeutralSceneInput(tracking,attempt);
            if(twoGrenadeFade){
                bc2::TwoGrenadeHealthWitness health{};bc2::TwoGrenadeNativeWitness native{};
                const bool hasHealth=ReadDeathWitness(folder.parent_path()/L"two-grenade-health.bin",health),hasNative=ReadDeathWitness(folder.parent_path()/L"two-grenade-native.bin",native);
                const auto now=GetTickCount64();grenadeFadeHeld=twoGrenadeGesture.Held(now,PresentationClockNs(),fadeStartTick,fadePixelPairs>0,deathPid,deathCreation,hasHealth?&health:nullptr,hasNative?&native:nullptr);
                controls=probe::NeutralSceneInput(tracking,attempt);probe::OrdinaryGrenadeFadeInput(controls,elapsed,now,fadeStartTick,grenadeFadeHeld);
                if(twoGrenadeGesture.SecondAt()&&!secondWitnessSaved){
                    const auto& n=twoGrenadeGesture.SecondNative();const auto& h=twoGrenadeGesture.SecondHealth();
                    std::ofstream a(folder/L"second-native-witness.bin",std::ios::binary),b(folder/L"second-health-witness.bin",std::ios::binary);
                    a.write(reinterpret_cast<const char*>(&n),sizeof(n));b.write(reinterpret_cast<const char*>(&h),sizeof(h));a.close();b.close();
                    Require(bool(a)&&bool(b),"Save exact second-gesture witnesses");secondWitnessSaved=true;
                }
            }
            if(normalKnifeClose&&elapsed>=probe::NormalKnifeHeldCloseReceiver::DurationMs)break;
            if(normalKnifeClose||contextualToolProbe||chestKnifeProbe)controls=probe::NeutralSceneInput(tracking,attempt);
            if(playerGrenadeProbe){controls=probe::NeutralSceneInput(tracking,attempt);probe::OrdinaryGrenadeInput(controls,elapsed);}
            if(ordinaryGrenadeProbe){controls=probe::NeutralSceneInput(tracking,attempt);
                if(ordinaryGrenadeFade){const auto now=GetTickCount64();grenadeFadeHeld=grenadeFadeGesture.Held(now,fadeStartTick,fadePixelPairs>0);
                    probe::OrdinaryGrenadeFadeInput(controls,elapsed,now,fadeStartTick,grenadeFadeHeld);}
                else probe::OrdinaryGrenadeInput(controls,elapsed);}
            if(bodyCrossDrawProbe)probe::BodyCrossDrawInput(controls,elapsed);
            if(bodyInspection)Require(probe::BodyInspectionInput(controls,bodyInspectionContact,elapsed),"Invalid body inspection input");
            if(controlsHands&&!supportProbe&&!shotProbe&&!controlsIndependence&&!trackingRecovery&&!controlsEquip&&!controlsWeaponCycle&&!weaponModeCapture&&!deathProbe){
                controls.hands[0].grip.position={-.2f,-.25f,-.45f};controls.hands[1].grip.position={.2f,-.25f,-.45f};
                if(elapsed>=600&&elapsed<1400){controls.hands[0].grip.position.x-=.08f;controls.hands[1].grip.position.x+=.12f;}
                if(elapsed>=1400&&elapsed<2200){controls.hands[1].grip.orientation={0,0,.258819045f,.965925826f};}
                if(elapsed>=3000&&elapsed<3400)controls.hands[1].gripTracked=false;
                if(controlsRoomscale)for(auto& hand:controls.hands){hand.grip.position.x+=tracking.head.position.x;hand.grip.position.z+=tracking.head.position.z;}
            }

            if(physicalPumpProbe)probe::PhysicalPumpInput(controls,elapsed,pumpExitVehicle);
            if(pumpHoldProbe){
                controls.hands[0].grip.position={-.2f,-.25f,-.45f};controls.hands[1].grip.position={0,-.4f,0};
                for(auto& hand:controls.hands)hand.aim=hand.grip;
                controls.hands[0].held=bc2::PumpDiagnosticUse(elapsed,pumpExitVehicle)?interaction::Primary:0;
                controls.hands[1].trigger=bc2::PumpDiagnosticTrigger(elapsed);
                if(controls.hands[1].trigger>0)++pumpFireSamples;
            }
            if(magazinePhysicalProbe){
                // Actual Gameplay fixture owns every left-hand motion/grab.
                // No fire, selection, native Reload, or private gate markers.
                controls.hands[0].grip.position={-.2f,-.25f,-.45f};controls.hands[1].grip.position={.15f,-.10f,-.20f}; // Diagnostic framing: magazine visible below the receiver.
                for(auto& hand:controls.hands)hand.aim=hand.grip;
            }
            if(resourceMagazineProbe||resourceInventoryProbe){
                auto now=GetTickCount64();
                if(resourceSceneResume&&!sceneResume.Accepted()&&now-resumePolled>=25){
                    resumePolled=now;std::ifstream command(folder/L"scene-resume.txt");
                    if(command){std::uint64_t id=0,issued=0,deadline=0;std::string extra;
                        Require(bool(command>>id>>issued>>deadline)&&!(command>>extra),"Invalid scene resume command");
                        now=GetTickCount64();
                        Require(sceneResume.Accept(id,issued,deadline,now),"Stale or repeated scene resume command");
                        std::ofstream receipt(folder/L"scene-resume.json");
                        receipt<<"{\"id\":"<<id<<",\"issued_ms\":"<<issued<<",\"deadline_ms\":"<<deadline
                               <<",\"accepted_ms\":"<<sceneResume.Began()<<",\"native_authority\":false}\n";
                        receipt.flush();Require(bool(receipt),"Save scene resume receipt");
                    }
                }
                resourceSetup=sceneResume.Accepted()?sceneResume.Step(now):
                    probe::ResourceMagazineSchedule(elapsed,weaponCycleExitVehicle,weaponCycleDirection,resourceStartHeld);
                Require(!sceneResume.Failed(),"Scene resume clock reversed");
                controls.hands[0].held=resourceSetup.use?interaction::Primary:0;controls.hands[1].stickY=resourceSetup.select;
            }
            if(magazineReloadProbe){const auto schedule=bc2::MagazineProbeSchedule(elapsed);
                controls.hands[0].grip.position={-.2f,-.25f,-.45f};controls.hands[1].grip.position={0,-.4f,0};
                for(auto& hand:controls.hands)hand.aim=hand.grip;
                controls.hands[0].trigger=schedule.marker;controls.hands[1].held=schedule.reload?interaction::Secondary:0;
            }
            if(weaponVisibilityProbe||bodyHolsterProbe){
                controls.hands[0].grip.position={-.2f,-.25f,-.45f};controls.hands[1].grip.position={.2f,-.25f,-.45f};
                for(auto& hand:controls.hands)hand.aim=hand.grip;
            }
            if(supportProbe){
                controls.hands[0].grip.position={-.068f,-.381f,-.437f};controls.hands[1].grip.position={0,-.4f,0};
                if(elapsed>=2500&&elapsed<2700)controls.hands[1].stickY=supportPrimaryDirection;
                // Warm hooks/calibration first. Free-hand motion, grab+steer,
                // release, then tracking loss while squeezed and neutral rearm.
                if(elapsed>=2500&&elapsed<3000)controls.hands[0].grip.position.y+=.08f;
                if((elapsed>=3500&&elapsed<5500)||(elapsed>=6500&&elapsed<8000)||(elapsed>=8800&&elapsed<10000))controls.hands[0].squeeze=1;
                if(elapsed>=4000&&elapsed<6000)controls.hands[0].grip.position.y+=.08f;
                if(elapsed>=7000&&elapsed<7500)controls.hands[0].gripTracked=controls.hands[0].aimTracked=false;
                if(elapsed>=9200&&elapsed<9800)controls.hands[0].grip.position.x+=.05f;
            }
            if(supportReload){
                controls.hands[0].grip.position={-.068f,-.381f,-.437f};controls.hands[0].gripTracked=controls.hands[0].aimTracked=true;
                controls.hands[0].squeeze=elapsed>=3500&&elapsed<9500?1.f:0.f;
                controls.hands[1].held=elapsed>=6000&&elapsed<6200?interaction::Secondary:0;
                controls.hands[1].trigger=elapsed>=4200&&elapsed<5500?.65f:0.f; // Visual curl, never Fire.
                // Opt-in diagnostic preparation: two ordinary native shots,
                // then the existing reload pulse. Caller must verify a selected
                // firearm with ammunition before starting; never changes slots.
                if(supportPrepareShots)controls.hands[1].trigger=
                    ((elapsed>=3300&&elapsed<3400)||(elapsed>=4500&&elapsed<4600))?1.f:0.f;
                // Free-hand touch coverage after release. Unsupported input is
                // separately exercised by the ordinary support/sight fixtures.
                controls.hands[0].touchActive=interaction::ThumbTouch|interaction::IndexTouch;
                controls.hands[0].touched=elapsed>=9700&&elapsed<10300?interaction::ThumbTouch|interaction::IndexTouch:0;
            }
            if(reloadRequestProbe){const auto schedule=bc2::RequestProbeSchedule(elapsed,supportPrepareShots);
                controls.hands[0].trigger=schedule.marker;controls.hands[0].squeeze=0;
                controls.hands[1].trigger=schedule.trigger;controls.hands[1].held=schedule.reload?interaction::Secondary:0;
                controls.hands[1].stickY=0;
            }
            if(physicalReloadProbe){
                // Actual Gameplay fixture supplies the left trajectory from
                // genuine renderer contact. Here only static source poses and
                // the existing optional two ordinary preparation shots exist.
                controls.hands[0].grip.position={-.23f,-.55f,-.02f};
                controls.hands[1].grip.position={0,-.4f,0};
                controls.hands[0].squeeze=controls.hands[0].trigger=0;
                controls.hands[1].held=0;controls.hands[1].stickY=0;
                controls.hands[1].trigger=bc2::PhysicalReloadPreparationTrigger(elapsed,supportPrepareShots);
            }
            if(shotProbe){
                if(shotReload&&elapsed>=700&&elapsed<850)controls.hands[1].held=interaction::Secondary;
                controls.hands[0].grip.position={-.2f,-.25f,-.45f};controls.hands[1].grip.position={.2f,-.25f,-.45f};
                // Exercise native selection; its per-slot candidate tables do not
                // guarantee a particular gun. Adapter guards the firearm asset.
                if((shotPrimaryPulses>=1&&elapsed>=2500&&elapsed<2700)||(shotPrimaryPulses==2&&elapsed>=3300&&elapsed<3500))controls.hands[1].stickY=shotCycleDirection;
                if(elapsed>=6000&&elapsed<7600){controls.hands[1].grip.position.x+=.2f;controls.hands[1].grip.position.y-=.1f;}
                if((elapsed>=5000&&elapsed<5080)||(elapsed>=6600&&elapsed<6680)||(elapsed>=9200&&elapsed<9280))controls.hands[1].trigger=1;
                if(elapsed>=9000&&elapsed<9400)controls.hands[1].gripTracked=controls.hands[1].aimTracked=false;
            }
            if(controlsIndependence){
                controls.hands[0].grip.position={-.2f,-.25f,-.45f};controls.hands[1].grip.position={.2f,-.25f,-.45f};
                float yaw=0;
                if(elapsed>=1000&&elapsed<3000)yaw=float(elapsed-1000)/2000.f*2.1f;
                if(elapsed>=3000&&elapsed<5000)yaw=2.1f-float(elapsed-3000)/2000.f*4.2f;
                if(elapsed>=5000&&elapsed<6000)yaw=-2.1f+float(elapsed-5000)/1000.f*2.1f;
                controls.hands[1].aim.orientation=controls.hands[1].grip.orientation={0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)};
            }
            if(weaponModeCapture){
                controls.hands[0].grip.position={-.2f,-.25f,-.45f};controls.hands[1].grip.position={.2f,-.25f,-.45f};
                if(sightGesture)controls.hands[1].grip.position={0,-.4f,0};
                // Test-only request through the verified native switch map.
                // No firearm trigger or global desktop input is generated.
                if(!sightGesture){
                    if((elapsed>=2500&&elapsed<2700)||(elapsed>=8000&&elapsed<8200))controls.hands[0].held=interaction::MenuClick;
                }else{
                    // Negative ADS control on the settled rifle, separate from
                    // sight gestures: full left trigger must only curl its finger.
                    if(elapsed>=5400&&elapsed<6400)controls.hands[0].trigger=1;
                    // Measured fixture geometry only; runtime learns authored
                    // grips and validates its own sight contact independently.
                    const auto atSight=[&](bool launcher,float angle){
                        controls.hands[0].grip.position=bc2::sight_fixture::SightGrip(launcher,angle);
                    };
                    // Reach and squeeze in the same XR packet. The preceding
                    // publication still has the distant hand: no artificial
                    // neutral dwell at the contact may conceal timing defects.
                    // Complete the physical quarter-turn beyond the toggle detent,
                    // exercising direct visual continuation after acknowledgement.
                    if(elapsed>=2300&&elapsed<3800){
                        const float fraction=std::clamp((float(elapsed)-2600.f)/550.f,0.f,1.f);
                        atSight(sightStartSecondary,sightStartSecondary?1.570796327f-fraction*1.570796327f:fraction*1.570796327f);
                        if(elapsed<3700)controls.hands[0].squeeze=1;
                    }
                    // Keep support coverage in launcher mode for either start:
                    // middle dwell for rifle-first, final dwell for launcher-first.
                    const std::uint64_t supportStart=sightStartSecondary?9900:4200;
                    if(elapsed>=supportStart&&elapsed<supportStart+2500){
                        // Start1cm off the authored contact to prove grab seats the hand.
                        controls.hands[0].grip.position=bc2::sight_fixture::LauncherSupportGrip();
                        if(elapsed>=supportStart+400&&elapsed<supportStart+2000)controls.hands[0].squeeze=1;
                    }
                    if(elapsed>=7800&&elapsed<9600){
                        const float fraction=std::clamp((float(elapsed)-8000.f)/600.f,0.f,1.f);
                        atSight(!sightStartSecondary,sightStartSecondary?fraction*1.570796327f:1.570796327f-fraction*1.570796327f);
                        if(elapsed<9400)controls.hands[0].squeeze=1;
                    }
                }
            }
            // Keep the complete synthetic firearm/left-hand trajectory inside
            // the avatar arm reach. Common rigid translation preserves every
            // weapon-local sight and support target; this is diagnostic only.
            if(sightGesture){
                controls.hands[0].grip.position.z+=.10f;
                controls.hands[1].grip.position.z+=.10f;
            }
            if(controlsWeaponCycle){
                controls.hands[0].grip.position={-.2f,-.25f,-.45f};controls.hands[1].grip.position={.2f,-.25f,-.45f};
                // Harness first verifies a vehicle occupant. This is the
                // existing controller Use action, never a native ammo write.
                if(weaponCycleExitVehicle&&elapsed>=400&&elapsed<500)controls.hands[0].held=interaction::Primary;
                if(elapsed>=1000&&elapsed<1800)controls.hands[1].stickY=weaponCycleDirection;
                if(elapsed>=5500&&elapsed<6300)controls.hands[1].stickY=-weaponCycleDirection;
                if(weaponCycleRepeat&&elapsed>=8500&&elapsed<9300)controls.hands[1].stickY=weaponCycleDirection;
                if(weaponCycleRepeat&&elapsed>=11500&&elapsed<12300)controls.hands[1].stickY=-weaponCycleDirection;
                if(controlsFire&&elapsed>=2200&&elapsed<2280)controls.hands[1].trigger=1;
                if(controlsFire&&elapsed>=3200&&elapsed<3320)controls.hands[1].held=interaction::Secondary;
            }
            if(controlsEquip){
                controls.hands[0].grip.position={-.2f,-.25f,-.45f};controls.hands[1].grip.position={.2f,-.25f,-.45f};
                if((elapsed>=1000&&elapsed<1100)||(elapsed>=5500&&elapsed<5600))controls.hands[1].held=interaction::MenuClick;
                if(controlsFire&&elapsed>=2200&&elapsed<2280)controls.hands[1].trigger=1;
                if(controlsFire&&elapsed>=3200&&elapsed<3320)controls.hands[1].held=interaction::Secondary;
            }
            if(trackingRecovery){
                controls.hands[0].grip.position={-.2f+.06f*std::sin(float(elapsed)*.003f),-.25f,-.45f};
                controls.hands[1].grip.position={.2f+.06f*std::cos(float(elapsed)*.003f),-.25f,-.45f};
                if((elapsed>=1000&&elapsed<2000)||(elapsed>=5000&&elapsed<5400))controls.hands[0].gripTracked=controls.hands[0].aimTracked=false;
                if((elapsed>=3000&&elapsed<4000)||(elapsed>=5000&&elapsed<5400))controls.hands[1].gripTracked=controls.hands[1].aimTracked=false;
                if(controlsFire&&((elapsed>=1400&&elapsed<1480)||(elapsed>=3200&&elapsed<4600)))controls.hands[1].trigger=1;
            }
            if(controlsVehicle&&!boatHeadAimProbe){
                if(elapsed>=1000&&elapsed<1300)controls.hands[0].stickY=.25f;
                if(elapsed>=2000&&elapsed<2300)controls.hands[0].stickX=.40f;
            }
            if(controlsUse&&elapsed>=400&&elapsed<500)controls.hands[0].held=interaction::Primary;
            if(controlsPulse&&elapsed>=400&&elapsed<900)controls.hands[0].stickY=.4f;
            if(controlsTurn&&elapsed>=400&&elapsed<900)controls.hands[1].stickX=.9f;
            if(controlsFire&&!trackingRecovery&&!controlsEquip&&!controlsWeaponCycle&&elapsed>=400&&elapsed<480)controls.hands[1].trigger=1;
            if(boatFireProbe&&probe::BoatFireProbe::Trigger(elapsed)){
                controls.hands[1].trigger=1;if(!boatFireInputSamples)boatFireFirstInputMs=elapsed;boatFireLastInputMs=elapsed;++boatFireInputSamples;
            }
            if(controlsAim&&elapsed>=400&&elapsed<1900){
                const float yaw=-.3490658504f,pitch=.1745329252f;
                controls.hands[1].aim.orientation={std::cos(yaw*.5f)*std::sin(pitch*.5f),std::sin(yaw*.5f)*std::cos(pitch*.5f),-std::sin(yaw*.5f)*std::sin(pitch*.5f),std::cos(yaw*.5f)*std::cos(pitch*.5f)};
            }
            if(controlsHands&&controlsAim&&elapsed>=400&&elapsed<1900){
                // Compose the scripted wrist roll with the same yaw/pitch as
                // the aim pose, as a physical controller would provide.
                const auto a=controls.hands[1].aim.orientation,b=controls.hands[1].grip.orientation;
                controls.hands[1].grip.orientation={a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
            }
            if(controlsButtons){
                if(elapsed>=400&&elapsed<1100)controls.hands[0].trigger=1;
                if(elapsed>=1600&&elapsed<1900)controls.hands[1].held=interaction::Secondary;
                if(elapsed>=3200&&elapsed<3500)controls.hands[1].held=interaction::Primary;
                if((elapsed>=4800&&elapsed<5100)||(elapsed>=6400&&elapsed<6700))controls.hands[0].held=interaction::Secondary;
            }
            if(deathProbe){
                // Diagnostic only: native grenade action with downward aim.
                // This never maps a squeeze to a grenade in a regular XR session.
                controls.hands[0].grip.position={-.2f,-.25f,-.45f};controls.hands[1].grip.position={.2f,-.25f,-.45f};
                const float pitch=(elapsed>=400&&elapsed<5000)?-1.4f:0.f;
                controls.hands[1].aim.orientation=controls.hands[1].grip.orientation={std::sin(pitch*.5f),0,0,std::cos(pitch*.5f)};
                controls.hands[1].squeeze=((elapsed>=1000&&elapsed<1250)||(elapsed>=3500&&elapsed<3750))?1.f:0.f;
            }
            // Synthetic poses use one known coordinate basis until an explicit
            // origin/receiver replacement. Never infer continuity from poses.
            controls.trackingBasisGeneration=1u+(originChanged?1u:0u)+(receiverGap.Returned()?1u:0u);
            if(selectedKnifeNeutral){
                ipc::MenuState state{};const auto read=playerMenu.ReadState(state);LARGE_INTEGER q{};QueryPerformanceCounter(&q);
                selectedKnifeMenuReceiver.Decorate(controls,0,read,state,q.QuadPart,playerMenuFrequency.QuadPart);
            }
            if(physicalBoltProbe||m95StockShotNeutral||sceneControlsNeutral||magazineOriginalReturnProbe||magazineFullReturnProbe){
                Require(probe::NeutralSceneActions(controls),"Nonneutral scene packet refused before publication");
                probe::WriteNeutralSceneInput(neutralInputRecords,controls,GetTickCount64());neutralInputRecords.flush();
                Require(bool(neutralInputRecords),"Save neutral scene input evidence");++neutralInputSamples;
            }
            if(resourceUseProbe){
                // Exactly the existing Use pulse, followed by neutral input.
                // The harness first verifies a vehicle occupant; no desktop input.
                auto neutral=controls;neutral.hands[0].held=0;
                Require(probe::NeutralSceneActions(neutral)&&controls.hands[0].held==
                    (elapsed>=400&&elapsed<500?interaction::Primary:0),"Unexpected resource setup action");
                probe::WriteNeutralSceneInput(neutralInputRecords,controls,GetTickCount64());neutralInputRecords.flush();
                Require(bool(neutralInputRecords),"Save resource setup input evidence");
            }
            if(resourceShellFocus){
                bc2::ShellFocusReceiver::ApplyFocus(controls,tracking.focused);
                Require(probe::NeutralSceneActions(controls),"Shell focus receiver must stay neutral");
            }
            if(resourceReceiverGap&&receiverGap.Returned()){
                Require(probe::NeutralSceneActions(controls),"Receiver gap returned nonneutral input");
            }
            if(resourceMagazineProbe||resourceInventoryProbe){
                const auto setup=resourceSetup;
                auto neutral=controls;neutral.hands[0].held=0;neutral.hands[1].stickY=0;
                Require(probe::NeutralSceneActions(neutral)&&controls.hands[0].held==(setup.use?interaction::Primary:0)&&
                    controls.hands[1].stickY==setup.select,"Unexpected resource magazine setup action");
                probe::WriteNeutralSceneInput(neutralInputRecords,controls,GetTickCount64());neutralInputRecords.flush();
                Require(bool(neutralInputRecords),"Save resource magazine setup evidence");++neutralInputSamples;
            }
            if(pumpHoldProbe){
                auto neutral=controls;neutral.hands[0].held=0;neutral.hands[1].trigger=0;
                Require(probe::NeutralSceneActions(neutral)&&controls.hands[0].held==(bc2::PumpDiagnosticUse(elapsed,pumpExitVehicle)?interaction::Primary:0)&&
                    controls.hands[1].trigger==bc2::PumpDiagnosticTrigger(elapsed),"Unexpected pump setup action");
                probe::WriteNeutralSceneInput(neutralInputRecords,controls,GetTickCount64());neutralInputRecords.flush();
                Require(bool(neutralInputRecords),"Save pump input evidence");
            }
            if(physicalPumpProbe){
                Require(probe::PhysicalPumpReceiverActions(controls,elapsed,pumpExitVehicle),"Unexpected physical pump receiver action");
                probe::WriteNeutralSceneInput(neutralInputRecords,controls,GetTickCount64());neutralInputRecords.flush();
                Require(bool(neutralInputRecords),"Save physical pump receiver evidence");
            }
            if(bodyInspection){
                Require(probe::BodyInspectionActions(controls),"Unexpected action in body inspection");
                probe::WriteNeutralSceneInput(bodyInspectionRecords,controls,GetTickCount64());bodyInspectionRecords.flush();
                Require(bool(bodyInspectionRecords),"Save body inspection input evidence");++bodyInspectionSamples;
            }
            if(ordinaryGrenadeProbe){
                Require(ordinaryGrenadeFade?probe::OrdinaryGrenadeFadeControls(controls,elapsed,grenadeFadeHeld):
                    probe::OrdinaryGrenadeControls(controls,elapsed),"Grenade receiver contract violation");
                if(ordinaryGrenadeFade){probe::WriteNeutralSceneInput(neutralInputRecords,controls,GetTickCount64());
                    neutralInputRecords.flush();Require(bool(neutralInputRecords),"Save combined grenade input evidence");}
                const bool held=(controls.hands[1].held&interaction::StickClick)!=0;
                ordinaryGrenadePresses+=held&&!ordinaryGrenadeHeld;ordinaryGrenadeReleases+=!held&&ordinaryGrenadeHeld;
                ordinaryGrenadeHeld=held;++ordinaryGrenadeSamples;
                Require(ordinaryGrenadePresses<=1&&ordinaryGrenadeReleases<=1,"No second grenade gesture");
            }
            if(triggerSmoke){
                const auto requestedMs=GetTickCount64()-controlsStarted;
                if(!triggerSmokeState.ContinueInput(requestedMs))break;
                controls=triggerSmokeState.Input(tracking,attempt,requestedMs);
                controls.trackingBasisGeneration=1u+(originChanged?1u:0u)+(receiverGap.Returned()?1u:0u);
                auto menuRead=ipc::ChannelResult::Closed;
                if(selectedKnifeMenu){
                    ipc::MenuState state{};const auto read=playerMenu.ReadState(state);LARGE_INTEGER q{};QueryPerformanceCounter(&q);
                    selectedKnifeMenuReceiver.Decorate(controls,0,read,state,q.QuadPart,playerMenuFrequency.QuadPart);
                    menuRead=read;
                }
                triggerSmokeState.GateMenu(controls,requestedMs,PresentationClockNs(),menuRead);
                Require(triggerSmokeJournal.Requested(controls,requestedMs),"Save trigger request before publication");
                const auto publishedMs=GetTickCount64()-controlsStarted;
                // A slow journal must not extend/shift the80ms trigger pulse.
                // Retry only the next scheduled frame, never the missed phase.
                if(triggerSmokeState.CurrentPhase(publishedMs)!=triggerSmokeState.CurrentPhase(requestedMs))continue;
                const auto before=PresentationClockNs();const auto publication=host.UpdateInputObserved(controls);
                const auto after=PresentationClockNs();
                const bool delivered=publication.known&&publication.succeeded&&publication.code==unsigned(ipc::ChannelResult::Ok);
                const bool busy=publication.known&&!publication.succeeded&&publication.code==unsigned(ipc::ChannelResult::Busy);
                // Motion coordinates belong to their requested clock sample;
                // actual before/after stamps independently measure delivery.
                const auto receipt=triggerSmokeState.Published(controls,triggerSmokeState.IsMotion()?requestedMs:publishedMs,before,after,delivered,busy);
                Require(triggerSmokeJournal.Published(receipt,controls,publishedMs,before,after,publication.code,publication.known,delivered,busy),"Save actual trigger publication");
            }else if(normalKnifeClose){
                ipc::MenuState state{};const auto read=playerMenu.ReadState(state);LARGE_INTEGER q{};QueryPerformanceCounter(&q);
                normalKnifeReceiver.Update(controls,elapsed,PresentationClockNs(),normalKnifeTarget.Current(),consumed,
                    read,state,q.QuadPart,playerMenuFrequency.QuadPart);
                const auto before=PresentationClockNs();const auto publication=host.UpdateInputObserved(controls);
                normalKnifeReceiver.Published(controls,publication.known,publication.succeeded,publication.code,before,PresentationClockNs());
                if(normalKnifeReceiver.Failed()){
                    std::ofstream record(folder/L"normal-knife-held-close.json");normalKnifeReceiver.Report(record);record<<'\n';record.close();
                    Require(bool(record),"Save failed normal held-close evidence");
                    throw std::runtime_error("Normal held-close source guard failed; receipt retained");
                }
            }else if(contextualToolProbe){
                bc2::ContextualToolProbeWitness witness{};const bool observed=ReadDeathWitness(folder.parent_path()/L"contextual-tool.bin",witness);
                ipc::MenuState menuState{};const auto menuRead=playerMenu.ReadState(menuState);LARGE_INTEGER q{};QueryPerformanceCounter(&q);
                toolReceiver.Update(controls,elapsed,PresentationClockNs(),toolPid,toolCreation,observed?&witness:nullptr,
                    menuRead,menuState,q.QuadPart,playerMenuFrequency.QuadPart);
                const auto publication=host.UpdateInputObserved(controls);
                toolReceiver.Published(controls,publication.known,publication.succeeded,publication.code,PresentationClockNs());
            }else if(chestKnifeProbe){
                bc2::ChestKnifeProbeWitness witness{};const bool observed=ReadDeathWitness(folder.parent_path()/L"chest-knife.bin",witness);
                ipc::MenuState menuState{};const auto menuRead=playerMenu.ReadState(menuState);LARGE_INTEGER q{};QueryPerformanceCounter(&q);
                knifeReceiver.Update(controls,elapsed,PresentationClockNs(),chestKnifeProbe,knifePid,knifeCreation,
                    observed?&witness:nullptr,menuRead,menuState,q.QuadPart,playerMenuFrequency.QuadPart);
                const auto publication=host.UpdateInputObserved(controls);
                knifeReceiver.Published(controls,publication.known,publication.succeeded,publication.code,PresentationClockNs());
            }else if(playerGrenadeProbe||twoGrenadeFade){
                Require(twoGrenadeFade?probe::OrdinaryGrenadeFadeControls(controls,elapsed,grenadeFadeHeld):probe::OrdinaryGrenadeControls(controls,elapsed),"Player grenade recipe permits no extra action");
                ipc::MenuState menuState{};const auto menuRead=playerMenu.ReadState(menuState);
                LARGE_INTEGER sampled{};QueryPerformanceCounter(&sampled);
                playerGrenadeReceiver.Decorate(controls,twoGrenadeFade?(grenadeFadeHeld?5000:0):elapsed,menuRead,menuState,sampled.QuadPart,playerMenuFrequency.QuadPart);
                if(twoGrenadeFade){probe::WriteNeutralSceneInput(neutralInputRecords,controls,GetTickCount64());neutralInputRecords.flush();Require(bool(neutralInputRecords),"Save actual menu-filtered two-grenade input");}
                const auto published=host.UpdateInputObserved(controls);
                playerGrenadeReceiver.Published(controls,published.known,published.succeeded,published.code,PresentationClockNs());
                if(twoGrenadeFade)twoGrenadeGesture.ObservePublished(grenadeFadeHeld,bool(controls.hands[1].held&interaction::StickClick),published.known,published.succeeded,published.code);
            }else if(resourceShellFocus){
                const auto publication=host.UpdateInputObserved(controls);
                if(!shellFocus.Publication(controls,publication.known,publication.succeeded,publication.code,
                    probe::NeutralSceneActions(controls),gapNow())){
                    // Persist the actual rejected outcome before unwinding;
                    // attempted input rows never count as delivered focus loss.
                    std::ofstream receipt(folder/L"receiver-shell-focus.json");shellFocus.Report(receipt);receipt<<'\n';
                    receipt.flush();Require(bool(receipt),"Save rejected shell focus publication");
                    throw std::runtime_error("Shell focus publication rejected; see receiver-shell-focus.json");
                }
            }else if(resourceReceiverGap){
                const auto publication=host.UpdateInputObserved(controls);
                Require(publication.known,"Receiver gap requires observed input publication");
                if(publication.succeeded){gapLastInput=controls.generation;gapLastPublishNs=gapNow();
                    if(receiverGap.Returned())Require(receiverGap.Published(controls.generation,controls.spaceGeneration,gapLastPublishNs,
                        probe::NeutralSceneActions(controls)),"Receiver return publication rejected");}
                else Require(publication.code==unsigned(ipc::ChannelResult::Busy),"Receiver gap input publication failed");
            }else host.UpdateInput(controls); // Each gameplay pulse has its own explicit opt-in diagnostic flag.

        }
        if(triggerSmoke&&consumed>=targetPairs){Sleep(5);continue;}
        if(triggerSmoke&&!triggerSmokeState.PairRequestDue(GetTickCount64()-controlsStarted,consumed,targetPairs)){Sleep(5);continue;}
        if(normalKnifeClose&&!probe::NormalKnifeHeldCloseReceiver::PairRequestDue(GetTickCount64()-controlsStarted,consumed)){Sleep(5);continue;}
        if(contextualToolProbe&&!probe::ContextualToolProbeReceiver::PairRequestDue(GetTickCount64()-controlsStarted,consumed)){Sleep(5);continue;}
        if(chestKnifeProbe&&!probe::ChestKnifeProbeReceiver::PairRequestDue(GetTickCount64()-controlsStarted,consumed)){Sleep(5);continue;}
        if(ordinaryGrenadeFade&&!(twoGrenadeFade?probe::TwoGrenadeFadeRequest(GetTickCount64(),fadeStartTick,consumed):probe::GrenadeFadeRequest(GetTickCount64(),fadeStartTick,consumed))){Sleep(2);continue;}
        if((contextualToolProbe||chestKnifeProbe||twoGrenadeFade||playerGrenadeProbe||ordinaryGrenadeProbe||physicalBoltProbe||m95StockShotNeutral||physicalPumpProbe||weaponCycleRepeat||emptyFireNeutral||resourceUseProbe||shotProbe||supportProbe||weaponModeCapture||controlsVehicle||magazineReloadProbe||magazinePhysicalProbe)&&consumed>=targetPairs){Sleep(5);continue;}
        // The producer's 30-second deadline starts before the receiver connects.
        // Finish the requested pairs with drain margin; keep publishing neutral
        // controls for the unchanged 30-second observation window above.
        // Scheduling pair239 at29.875s lost the last pair at producer shutdown.
        if(inventoryReloadProbe&&(pumpSideCapture.Enabled()?
            !pumpSideCapture.Due(GetTickCount64()-controlsStarted,consumed):
            GetTickCount64()-controlsStarted<std::uint64_t(consumed)*55000/targetPairs)){Sleep(5);continue;}
        if(!inventoryReloadProbe&&(physicalBoltProbe||physicalPumpProbe||reloadRequestProbe||physicalReloadProbe||magazineReloadProbe||magazinePhysicalProbe)&&GetTickCount64()-controlsStarted<std::uint64_t(consumed)*((pumpEmptyCycle||boltEmptyCycle)?55000:28000)/targetPairs){Sleep(5);continue;}
        if((pumpHoldProbe||weaponVisibilityProbe||bodyHolsterProbe||bodyCrossDrawProbe||bodyInspection)&&GetTickCount64()-controlsStarted<std::uint64_t(consumed)*14000/targetPairs){Sleep(5);continue;}
        if(controlsVehicle&&GetTickCount64()-controlsStarted<std::uint64_t(consumed)*6500/targetPairs){Sleep(5);continue;}
        if(weaponModeCapture&&GetTickCount64()-controlsStarted<std::uint64_t(consumed)*13000/targetPairs){Sleep(5);continue;}
        // Deliberately spread neutral requests across checkpoint loads. This
        // original diagnostic publishes no controllers; explicit neutral mode
        // publishes stationary tracked poses with every action axis/button zero.
        const auto fadeNow=GetTickCount64();
        const bool fadeInside=fadeStartTick&&fadeNow>=fadeStartTick&&fadeNow-fadeStartTick<fadeDurationMs;
        if(fadeInside){if(fadeNow<fadeNextTick){Sleep(2);continue;}}
        else if(sceneRecoveryProbe&&fadeNow-controlsStarted<std::uint64_t(consumed)*55000/targetPairs){Sleep(5);continue;}
        graphics::TextureDescriptor descriptor;graphics::PairTicket ticket;runtime::TrackingFrame rendered=tracking;
        const bool ready=async?host.TryGetCompletedPair(requirements,tracking,rendered,descriptor,ticket):host.TryGetPair(requirements,tracking,descriptor,ticket);
        if(!ready){++timeouts;Sleep(async?11:2);continue;}tracking=rendered;
        // A replacement scene may restart its native frame serial. Request/tracking
        // identity is still checked for every pair; owner changes are audited in the native trace.
        Require(runtime::PairMatchesFrame(requirements,tracking,descriptor,ticket)&&(sceneRecoveryProbe||ordinaryGrenadeFade||ticket.frameId>lastFrame),"Native frame identity mismatch");
        if(std::memcmp(&descriptor,&opened,sizeof(descriptor))){Require(consumer.Open(device.Get(),descriptor)==graphics::TransferResult::Ok,"Open native shared eyes");opened=descriptor;sceneDepth.Reset();}
        const std::array<graphics::TextureSlice,2> targets={graphics::TextureSlice{output.Get(),0},graphics::TextureSlice{output.Get(),1}};
        graphics::SceneDepthFrame depthFrame;graphics::SceneDepthViews depthViews;
        const bool depthRead=host.ReadSceneDepth(descriptor,ticket,depthFrame);
        const bool depthCopied=depthRead&&sceneDepth.CopyBeforeColor(depthFrame,descriptor,ticket)&&sceneDepth.Views(ticket,depthViews);
        if(depthRead)++depthReadPairs;if(depthCopied)++depthCopiedPairs;
        // The existing color-copy event query also completes these preceding
        // shared-depth reads before the producer receives its acknowledgement.
        const auto copyStarted=GetTickCount64();const auto copyResult=consumer.Copy(ticket,targets);const auto copyMs=GetTickCount64()-copyStarted;
        const bool copied=copyResult==graphics::TransferResult::Ok;
        graphics::BodyPropFrame props{};const bool propsRead=copied&&host.ReadBodyProps(ticket,props);bool composed=false;
        if(copied&&nativeOutput){
            context->CopyResource(nativeOutput.Get(),output.Get());
            for(unsigned eye=0;eye<2;++eye)context->CopySubresourceRegion(composedEyes[eye].Get(),0,0,0,0,output.Get(),eye,nullptr);
            composed=propsRead&&bodyProps.Compose(props,ticket,{composedEyes[0].Get(),composedEyes[1].Get()},PresentationClockNs,
                depthCopied?&depthViews:nullptr,graphics::BodyPropMissingDepthPolicy::VehicleReticleOnly);
            if(composed)for(unsigned eye=0;eye<2;++eye)context->CopySubresourceRegion(output.Get(),eye,0,0,0,composedEyes[eye].Get(),0,nullptr);
        }
        if(copied)RecordPresentation(presentationRecords,props,propsRead,ammoPresentation,ticket,GetTickCount64()-controlsStarted);
        depthRecords<<"{\"frame\":"<<ticket.frameId<<",\"sequence\":"<<ticket.sequence<<",\"tracking\":"<<ticket.trackingGeneration
            <<",\"read\":"<<(depthRead?"true":"false")<<",\"copied\":"<<(depthCopied?"true":"false")
            <<",\"samples\":"<<(depthRead?depthFrame.samples:0)<<",\"resource_generation\":"<<(depthRead?depthFrame.resourceGeneration:0)
            <<",\"color_copied\":"<<(copied?"true":"false")<<",\"composed\":"<<(composed?"true":"false")<<"}\n";
        host.PairConsumed(ticket,copied);
        if(!copied){records<<"{\"copy_failed\":true,\"native_frame\":"<<ticket.frameId<<",\"result\":"<<unsigned(copyResult)<<",\"hresult\":"<<consumer.LastError()<<",\"copy_ms\":"<<copyMs<<"}\n";records.flush();
            if(copyResult==graphics::TransferResult::Busy){++gpuBusy;continue;}throw std::runtime_error("Copy native GPU pair HRESULT="+std::to_string(consumer.LastError()));}

        // Half-second paired eye evidence throughout the expected physical
        // manipulation window; bounded full-resolution files, no phase guessing
        // used as authority. Correlate elapsed times with fixture/Pack logs.
        const auto fadeCapturedTick=GetTickCount64();
        const bool fadeCapture=fadeStartTick&&fadeCapturedTick>=fadeStartTick&&
            fadeCapturedTick-fadeStartTick<fadeDurationMs&&fadePixelPairs<(twoGrenadeFade?48u:32u);
        if(fadeCapture)fadeNextTick=fadeCapturedTick+fadeIntervalMs;
        const auto pumpCaptureElapsed=GetTickCount64()-controlsStarted;
        const bool pumpPixels=pumpSideCapture.Pixels(pumpCaptureElapsed);
        const unsigned knifeReviewStage=ticket.trackingGeneration>0x100000000ull?
            triggerSmokeState.ReviewPair(ticket.trackingGeneration-0x100000000ull,ticket.spaceGeneration):0;
        const bool inspectPixels=triggerSmokeState.ReviewImagesRequested()?knifeReviewStage!=0:pumpSideCapture.Enabled()?pumpPixels:fadeStartTick?fadeCapture:boatFireProbe?(boatFireSchedule.Capture(GetTickCount64()-controlsStarted)||consumed+1==targetPairs):magazinePhysicalProbe?(consumed<4||consumed+1==targetPairs||(consumed>=40&&consumed<=176&&consumed%4==0)):sceneRecoveryProbe?(consumed%24==0||consumed+1==targetPairs):(weaponVisibilityProbe||bodyHolsterProbe||bodyCrossDrawProbe||bodyInspection)?(consumed%24==0||consumed+1==targetPairs):consumed<8||consumed+1==targetPairs||((controlsAim||controlsRoomscale||capturePoses)&&(consumed==30||consumed==60||(controlsHands&&(consumed==45||consumed==75||consumed==90||consumed==120||consumed==150))));
        if(inspectPixels){
        if(fadeCapture){++fadePixelPairs;
            records<<"{\"fade_pixel_pair\":"<<consumed<<",\"tick_ms\":"<<fadeCapturedTick
                <<",\"sample_ns\":"<<PresentationClockNs()<<",\"native_frame\":"<<ticket.frameId
                <<",\"sequence\":"<<ticket.sequence<<",\"tracking\":"<<ticket.trackingGeneration
                <<",\"space\":"<<ticket.spaceGeneration<<",\"native_color_only\":true}\n";
        }
        bool nativeQueued=true;
        if(nativeImageWriter){
            auto native=std::make_shared<const probe::DiagnosticImageWriter::Pixels>(probe::DiagnosticImageWriter::Pixels{
                Pixels(context.Get(),nativeOutput.Get(),staging.Get(),0),Pixels(context.Get(),nativeOutput.Get(),staging.Get(),1)});
            nativeQueued=nativeImageWriter->Queue(consumed,std::move(native));
        }
        std::array<std::vector<unsigned char>,2> pixels{Pixels(context.Get(),output.Get(),staging.Get(),0),Pixels(context.Get(),output.Get(),staging.Get(),1)};
        const bool distinct=pixels[0]!=pixels[1],changed=!prior||(pixels[0]!=(*prior)[0]&&pixels[1]!=(*prior)[1]);
        records<<"{\"elapsed_ms\":"<<(GetTickCount64()-controlsStarted)<<",\"pair\":"<<consumed<<",\"native_frame\":"<<ticket.frameId<<",\"copy_ms\":"<<copyMs<<",\"sharing_mode\":"<<unsigned(descriptor.sharing)<<",\"tracking\":"<<ticket.trackingGeneration<<",\"distinct\":"<<distinct<<",\"changed\":"<<changed<<"}\n";records.flush();
        if(!sceneRecoveryProbe&&!ordinaryGrenadeFade)Require(distinct&&(staticPose||changed),"Native images did not vary across eyes/tracking");
        prior=std::make_shared<const probe::DiagnosticImageWriter::Pixels>(std::move(pixels));
        // Preserve the actual readback/comparison above; this normal close check
        // needs causal native receipts, not another copy of the same raw images.
        const bool pixelsQueued=!noImageFiles&&imageWriter.Queue(consumed,prior);
        if(knifeReviewStage){records<<"{\"knife_review_stage\":"<<knifeReviewStage<<",\"elapsed_ms\":"<<pumpCaptureElapsed
            <<",\"pair\":"<<consumed<<",\"native_frame\":"<<ticket.frameId<<",\"tracking\":"<<ticket.trackingGeneration
            <<",\"space\":"<<ticket.spaceGeneration<<",\"queue_accepted\":"<<(pixelsQueued?"true":"false")<<"}\n";records.flush();}
        if(pumpPixels){const auto sampled=PresentationClockNs();
            const bool observed=pumpSideCapture.Observe(pumpCaptureElapsed,sampled,pixelsQueued&&nativeQueued);
            records<<"{\"pump_side_pixel_pair\":"<<consumed<<",\"elapsed_ms\":"<<pumpCaptureElapsed
                <<",\"sample_ns\":"<<sampled<<",\"native_frame\":"<<ticket.frameId<<",\"sequence\":"<<ticket.sequence
                <<",\"tracking\":"<<ticket.trackingGeneration<<",\"space\":"<<ticket.spaceGeneration
                <<",\"queue_accepted\":"<<(pixelsQueued&&nativeQueued)<<",\"observation_recorded\":"<<observed<<"}\n";records.flush();
        }
        }else{records<<"{\"elapsed_ms\":"<<(GetTickCount64()-controlsStarted)<<",\"pair\":"<<consumed<<",\"native_frame\":"<<ticket.frameId<<",\"tracking\":"<<ticket.trackingGeneration<<",\"copy_ms\":"<<copyMs<<",\"pixels_checked\":false}\n";records.flush();}
        records<<"{\"tracking_sample\":"<<ticket.trackingGeneration<<",\"asymmetric\":"<<asymmetric<<",\"static_pose\":"<<staticPose<<",\"async\":"<<async<<",\"fov\":[";
        for(unsigned eye=0;eye<2;++eye){if(eye)records<<',';const auto& f=tracking.fov[eye];records<<'['<<f.left<<','<<f.right<<','<<f.up<<','<<f.down<<']';}records<<"]}\n";records.flush();
        lastFrame=ticket.frameId;++consumed;if(async)Sleep(11);
    }
    if(triggerSmoke){const auto stoppedElapsed=GetTickCount64()-controlsStarted;
        const bool durationEnded=triggerSmokeState.DurationEnded(stoppedElapsed);
        const bool journalSaved=triggerSmokeJournal.Stop(durationEnded?"duration_elapsed":"outer_budget_ended",durationEnded,consumed);
        triggerSmokeComplete=journalSaved&&triggerSmokeState.Complete(stoppedElapsed,durationEnded);
        Require(journalSaved,"Save timed trigger stop before final result");}
    if(normalKnifeClose){
        normalKnifeReceiver.Finish(GetTickCount64()-controlsStarted,PresentationClockNs());
        std::ofstream record(folder/L"normal-knife-held-close.json");normalKnifeReceiver.Report(record);record<<'\n';record.close();
        Require(bool(record)&&normalKnifeReceiver.Complete(),"Normal held-close did not finish its original source window");
    }
    const auto feedbackUntil=GetTickCount64()+1000;while(host.FlushFeedback()==ipc::ChannelResult::Busy&&GetTickCount64()<feedbackUntil)Sleep(1);
    if(fadeStartTick)std::ofstream(folder/L"fade-window-result.json")
        <<"{\"start_tick_ms\":"<<fadeStartTick<<",\"duration_ms\":"<<fadeDurationMs
        <<",\"interval_ms\":"<<fadeIntervalMs<<",\"pixel_pair_limit\":"<<(twoGrenadeFade?48:32)<<",\"pixel_pairs\":"<<fadePixelPairs
        <<",\"native_color_only\":true,\"controller_input\":"<<(ordinaryGrenadeFade?"true":"false")
        <<",\"ordinary_downward_gesture\":"<<(ordinaryGrenadeFade?"true":"false")
        <<",\"gesture_admitted\":"<<((twoGrenadeFade?twoGrenadeGesture.FirstAdmitted():grenadeFadeGesture.Admitted())?"true":"false")
        <<",\"transition_induced\":false,\"actual_death\":false,\"headset_tested\":false}\n";
    if(pumpSideCapture.Enabled()){
        std::ofstream receipt(folder/L"pump-side-capture.json");pumpSideCapture.Report(receipt);receipt<<'\n';receipt.close();
        Require(bool(receipt),"Save pump capture schedule and actual gaps");
    }
    const auto imageWrites=imageWriter.Finish();
    if(nativeImageWriter){const auto writes=nativeImageWriter->Finish();Require(writes.queued==writes.written&&!writes.failed&&!writes.rejected,"Native comparison image evidence incomplete");}
    const auto& composeStats=bodyProps.Statistics();
    std::ofstream(folder/L"scene-depth-result.json")<<"{\"read_pairs\":"<<depthReadPairs<<",\"copied_pairs\":"<<depthCopiedPairs
        <<",\"composed_pairs\":"<<composeStats.sceneDepthPairs<<",\"total_composed_pairs\":"<<composeStats.pairs
        <<",\"overlay_only_pairs\":"<<composeStats.overlayOnlyPairs<<",\"depth_omitted_instances\":"<<composeStats.depthOmittedInstances
        <<",\"compose_failures\":"<<composeStats.sceneDepthFailures
        <<",\"missing_geometry\":"<<composeStats.missing<<",\"expired_props\":"<<composeStats.expired
        <<",\"invalid_props\":"<<composeStats.invalid<<",\"render_failures\":"<<composeStats.failures
        <<",\"headset_tested\":false}\n";
    std::ofstream(folder/L"image-writes.json")<<"{\"files_disabled\":"<<(noImageFiles?"true":"false")<<",\"queued\":"<<imageWrites.queued<<",\"written\":"<<imageWrites.written
        <<",\"rejected\":"<<imageWrites.rejected<<",\"failed\":"<<imageWrites.failed<<"}\n";
    Require(imageWrites.queued==imageWrites.written&&!imageWrites.failed&&!imageWrites.rejected,"Diagnostic image evidence incomplete");
    if(triggerSmokeState.ReviewImagesRequested()){
        Require(imageWrites.queued<=2&&imageWrites.queued==triggerSmokeState.ReviewPairs(),"Knife review image cardinality changed");
        std::ofstream(folder/L"knife-review-images.json")<<"{\"maximum_pairs\":2,\"selected_pairs\":"<<triggerSmokeState.ReviewPairs()
            <<",\"written_pairs\":"<<imageWrites.written<<",\"complete\":"<<(imageWrites.written==2?"true":"false")<<"}\n";
    }
    Require(!pumpSideCapture.Enabled()||pumpSideCapture.Complete(),"Pump pixel window incomplete; retain recorded gaps");
    const auto& delivery=host.Statistics();
    if(bodyInspection)std::ofstream(folder/L"body-inspection-result.json")
        <<"{\"contact\":["<<bodyInspectionContact.x<<','<<bodyInspectionContact.y<<','<<bodyInspectionContact.z
        <<"],\"input_samples\":"<<bodyInspectionSamples<<",\"hand\":1,\"single_grip_edge\":true,\"native_authority\":false,\"headset_tested\":false}\n";
    std::ofstream(folder/L"resource-setup.json")<<"{\"enabled\":"<<(resourceUseProbe?"true":"false")<<",\"use_window_ms\":[400,500],\"gameplay_transition_verified\":false,\"os_input\":false}\n";
    if(twoGrenadeFade){
        std::ofstream record(folder/L"two-grenade-receiver.json");
        record<<"{\"contract\":399,\"maximum_gestures\":2,\"first_admitted\":"<<(twoGrenadeGesture.FirstAdmitted()?"true":"false")
            <<",\"second_at_ms\":"<<twoGrenadeGesture.SecondAt()<<",\"second_at_ns\":"<<twoGrenadeGesture.SecondAtNs()<<",\"click_presses\":"<<twoGrenadeGesture.Presses()<<",\"click_releases\":"<<twoGrenadeGesture.Releases()
            <<",\"aborted\":"<<(twoGrenadeGesture.Aborted()?"true":"false")<<",\"health_zero_seen\":"<<(twoGrenadeGesture.ZeroObserved()?"true":"false")
            <<",\"actual_death\":false,\"real_menu\":";playerGrenadeReceiver.Report(record);record<<"}\n";record.close();Require(bool(record),"Save two-grenade receiver result");
        Require(!twoGrenadeGesture.Aborted()&&twoGrenadeGesture.Presses()>=1&&twoGrenadeGesture.Presses()<=2&&twoGrenadeGesture.Presses()==twoGrenadeGesture.Releases(),"Two-grenade recipe did not complete its bounded publications");
    }
    if(contextualToolProbe){std::ofstream report(folder/L"contextual-tool-receiver.json");toolReceiver.Report(report);report<<'\n';
        report.flush();Require(bool(report),"Save contextual receiver recipe report");}
    if(chestKnifeProbe){std::ofstream record(folder/L"chest-knife-receiver.json");knifeReceiver.Report(record);record<<'\n';record.flush();
        Require(bool(record),"Save finite knife receiver evidence");}
    if(playerGrenadeProbe){std::ofstream record(folder/L"player-grenade-receiver.json");playerGrenadeReceiver.Report(record);record<<'\n';record.flush();
        Require(bool(record),"Save bounded player grenade receiver evidence");
        Require(playerGrenadeReceiver.OneGesture(),"Player receiver did not publish exactly one uninterrupted menu-owned gesture");}
    if(ordinaryGrenadeProbe){std::ofstream record(folder/L"ordinary-grenade-receiver.json");
        record<<"{\"contract\":"<<(ordinaryGrenadeFade?395:394)<<",\"combined_fade\":"<<(ordinaryGrenadeFade?"true":"false")<<",\"samples\":"<<ordinaryGrenadeSamples<<",\"click_presses\":"<<ordinaryGrenadePresses
            <<",\"click_releases\":"<<ordinaryGrenadeReleases<<",\"trigger_samples\":0,\"setup\":\"one ordinary left shoulder draw\",\"native_acceptance\":false}\n";
        Require(bool(record),"Save bounded grenade receiver summary");}
    if(resourceShellFocus){std::ofstream receipt(folder/L"receiver-shell-focus.json");shellFocus.Report(receipt);receipt<<'\n';
        receipt.close();Require(bool(receipt)&&shellFocus.Completed(),"Shell focus loss was not exercised");}
    if(resourceReceiverGap){const auto& w=receiverGap.Witness();
        std::ofstream receipt(folder/L"receiver-input-gap.json");
        receipt<<"{\"completed\":"<<(receiverGap.Completed()?"true":"false")<<",\"failed\":"<<(receiverGap.Failed()?"true":"false")
            <<",\"begin_ns\":"<<receiverGap.BeganNs()<<",\"return_ns\":"<<receiverGap.ReturnedNs()<<",\"first_return_ns\":"<<receiverGap.FirstReturnNs()
            <<",\"first_return_input\":"<<receiverGap.FirstReturnInput()<<",\"old_space\":"<<w.space<<",\"new_space\":"<<receiverGap.Space(w.space)
            <<",\"last_pregap_input\":"<<gapPreInput<<",\"last_pregap_publish_after_ns\":"<<gapPrePublishNs
            <<",\"last_pregap_deadline_upper_ns\":"<<gapPrePublishNs+bc2::PumpReceiverGapProbe::OriginalLeaseNs
            <<",\"cycle\":"<<w.cycle<<",\"shot\":"<<w.shot<<",\"input\":"<<w.input<<",\"original_deadline_ns\":"<<w.deadlineNs
            <<",\"loaded\":"<<w.loaded<<",\"reserve\":"<<w.reserve<<",\"capacity\":"<<w.capacity<<",\"travel_m\":"<<w.travel
            <<",\"input_and_tracking_withheld\":true,\"native_authority\":false}\n";
        receipt.close();Require(bool(receipt)&&receiverGap.Completed(),"Full receiver input gap was not completed");
    }
    std::ofstream(folder/L"result.json")<<"{\"physical_bolt_empty_cycle\":"<<(boltEmptyCycle?"true":"false")<<",\"physical_bolt_receiver\":"<<(physicalBoltProbe?"true":"false")<<",\"m95_stock_shot_neutral\":"<<(m95StockShotNeutral?"true":"false")<<",\"physical_pump_receiver\":"<<(physicalPumpProbe?"true":"false")<<",\"physical_pump_empty_cycle\":"<<(pumpEmptyCycle?"true":"false")<<",\"physical_pump_exit_vehicle\":"<<(physicalPumpProbe&&pumpExitVehicle?"true":"false")<<",\"resource_magazine_receiver\":"<<(resourceMagazineProbe?"true":"false")
        <<",\"resource_setup_exit_vehicle\":"<<((resourceMagazineProbe||resourceInventoryProbe)&&weaponCycleExitVehicle?"true":"false")
        <<",\"trigger_input_smoke\":"<<(triggerSmoke?"true":"false")
        <<",\"selected_knife_motion_smoke\":"<<(triggerSmokeState.IsMotion()?"true":"false")
        <<",\"selected_knife_neutral\":"<<(selectedKnifeNeutral?"true":"false")
        <<",\"selected_knife_native_menu\":"<<(selectedKnifeMenu?"true":"false")
        <<",\"trigger_publication_complete\":"<<(triggerSmokeComplete?"true":"false")
        <<",\"resource_setup_start_held\":"<<(resourceStartHeld?"true":"false")
        <<",\"resource_setup_schedule_version\":"<<((resourceMagazineProbe||resourceInventoryProbe)?(resourceStartHeld?probe::ResourceMagazineHeldScheduleVersion:probe::ResourceMagazineScheduleVersion):0)
        <<",\"resource_setup_selection_direction\":"<<((resourceMagazineProbe||resourceInventoryProbe)&&!resourceStartHeld?weaponCycleDirection:0)
        <<",\"resource_inventory_receiver\":"<<(resourceInventoryProbe?"true":"false")<<",\"resource_inventory_input_samples\":"<<(resourceInventoryProbe?neutralInputSamples:0)<<",\"empty_fire_neutral_receiver\":"<<(emptyFireNeutral?"true":"false")<<",\"consumed_pairs\":"<<consumed<<",\"scene_recovery_fixture\":"<<(sceneRecoveryProbe?"true":"false")<<",\"scene_controls_neutral\":"<<(sceneControlsNeutral?"true":"false")<<",\"scene_neutral_input_samples\":"<<(sceneControlsNeutral?neutralInputSamples:0)<<",\"original_return_input_samples\":"<<(magazineOriginalReturnProbe?neutralInputSamples:0)<<",\"full_return_input_samples\":"<<(magazineFullReturnProbe?neutralInputSamples:0)<<",\"magazine_full_return_fixture\":"<<(magazineFullReturnProbe?"true":"false")<<",\"magazine_original_return_fixture\":"<<(magazineOriginalReturnProbe?"true":"false")<<",\"magazine_physical_fixture\":"<<(magazinePhysicalProbe?"true":"false")<<",\"xm8_magazine_fixture\":"<<(magazineReloadProbe?"true":"false")<<",\"reload_request_fixture\":"<<(reloadRequestProbe?"true":"false")<<",\"weapon_visibility_fixture\":"<<(weaponVisibilityProbe?"true":"false")<<",\"body_cross_draw_fixture\":"<<(bodyCrossDrawProbe?"true":"false")<<",\"body_holster_fixture\":"<<(bodyHolsterProbe?"true":"false")<<",\"physical_reload_fixture\":"<<(physicalReloadProbe?"true":"false")<<",\"boat_head_aim_fixture\":"<<(boatHeadAimProbe?"true":"false")<<",\"boat_fire_fixture\":"<<(boatFireProbe?"true":"false")<<",\"boat_fire_input_samples\":"<<boatFireInputSamples<<",\"boat_fire_first_input_ms\":"<<boatFireFirstInputMs<<",\"boat_fire_last_input_ms\":"<<boatFireLastInputMs<<",\"boat_fire_window_ms\":[2000,2080],\"boat_fire_projectile_verified\":false"<<",\"vehicle_fixture\":"<<(controlsVehicle?"true":"false")<<",\"sight_gesture_fixture\":"<<(sightGesture?"true":"false")<<",\"sight_gesture_fixture_version\":"<<(sightStartSecondary?9:8)<<",\"sight_start_secondary\":"<<(sightStartSecondary?"true":"false")<<",\"left_trigger_pose_only_fixture\":"<<(sightGesture?"true":"false")<<",\"weapon_mode_capture\":"<<(weaponModeCapture?"true":"false")<<",\"controls_started_ms\":"<<controlsStarted<<",\"support_primary_direction\":"<<supportPrimaryDirection<<",\"support_grip_requested\":"<<(supportProbe?"true":"false")<<",\"support_reload_fixture\":"<<(supportReload?"true":"false")<<",\"pump_hold_fixture\":"<<(pumpHoldProbe?"true":"false")<<",\"pump_fire_input_samples\":"<<pumpFireSamples<<",\"pump_headset_verified\":false"<<",\"support_prepare_shots\":"<<(supportPrepareShots?"true":"false")<<",\"shot_probe_requested\":"<<(shotProbe?"true":"false")<<",\"shot_primary_pulses\":"<<shotPrimaryPulses<<",\"shot_cycle_direction\":"<<shotCycleDirection<<",\"shot_schedule_version\":2,\"request_lifetime_ms\":"<<requestLifetimeMs<<",\"async_timeouts\":"<<delivery.timeouts<<",\"async_mean_latency_us\":"<<(delivery.completed?delivery.totalLatencyUs/delivery.completed:0)<<",\"async_max_latency_us\":"<<delivery.maxLatencyUs<<",\"async_completed_after_50ms\":"<<delivery.completedAfter50Ms<<",\"gpu_busy\":"<<gpuBusy<<",\"unavailable_attempts\":"<<timeouts<<",\"native_tracking_transport_verified\":"<<(consumed==targetPairs)<<",\"headset_tested\":false}\n";
    if(triggerSmoke){Require(triggerSmokeComplete,"Trigger publication window incomplete; retain durable journal");
        std::cout<<"Trigger publication window completed; "<<consumed<<"/"<<targetPairs<<" native pairs, native attack unverified\n";}
    else{Require(consumed==targetPairs,"Incomplete native stream");std::cout<<targetPairs<<" native BC2 pairs received with matching tracking identities\n";}return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
