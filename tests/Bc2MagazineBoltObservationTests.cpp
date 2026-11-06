#include "Bc2MagazineBoltObservationJson.h"
#include "Bc2LiveMechanismSnapshotBudget.h"
#include "Test.h"
#include <sstream>
#include <utility>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
using namespace fvr::bc2::live_magazine_bolt;
namespace {
constexpr std::int64_t At=1000000000,Now=At+1000,End=At+100000000;
MagazineLiveObservation Magazine(){
    MagazineLiveObservation o;o.owner={0x10000,0x20000,0x30000,0x40000,5,3,7};
    o.input={{(std::uint64_t(0x30000)<<32)|0x20000,5,17,7},9,At,End,Now,true,{true,true},{false,false}};
    o.weapon={0xb0000,17};o.profile=&Xm8MagazineEquipment();o.capturedNs=Now;o.resourceBackend=true;
    o.family={{o.owner,o.weapon,0xd0000,0xc0000,2,o.profile},At,End,true};
    AmmoResourceView v;v.binding={o.owner,{{0x20000,5,0x40000,45},3,7},0xd0000,0x130000,0x140000,0x150000,At,End};
    v.identity.owner=o.owner;v.snapshot={v.binding.context,20,At+100,End,{0,83,30},true};
    v.phase=AmmunitionLedgerPhase::Ready;v.originalState=MagazineResourceState::Held;v.wellEmpty=true;
    AmmunitionCommand c{v.binding.context,12,18,At-500,At+500,At+2000000000,AmmunitionOperation::RemoveMagazine,{27,83,30},{0,83,30},{},true};
    v.receipt=AmmunitionReceipt{c,79,At-400,At,{27,83,30},{0,83,30},true,true};
    v.original=RemovedMagazineResource{v.binding.context.resource,12,27,30};v.request=57;v.requestState=AmmoResourceRequestState::Completed;
    AmmoResourceRequest request{v.binding,{v.binding.context,57,At-500,At+500,AmmunitionOperation::RemoveMagazine,{}}};
    v.terminal=AmmoResourceOutcome{request,AmmoResourceRequestState::Completed,v.receipt,At+50,true};
    o.state.resource=v;
    o.raw.valid=true;o.raw.owner=o.owner;o.raw.rigFingerprint=44;o.raw.inputEvidence=o.input;
    o.raw.rawLeftWristWorldMeters=o.raw.weaponWorldMeters=reload_insertion_detail::Identity();o.originalHandEvidence=o.input;
    return o;
}
BoltLiveObservation Bolt(){
    const auto m=Magazine();BoltLiveObservation o;o.owner=m.owner;o.input=m.input;o.weapon=m.weapon;o.capturedNs=Now;
    o.result.tracking={true,o.owner,o.input,std::make_shared<const Bc2BoltCalibration>(M95ObservedBoltCalibration235())};
    auto& t=o.result.tracking;t.custody=BoltCustodyPhase::Manipulating;
    HandClaim gun{{101,o.input.owner,InteractionHand::Left,HandClaimKind::GunHold,o.weapon,{11,1},0},End,o.input.sequence};
    o.left=gun;t.gun=gun;t.weapon=BoltCustodyWeaponTarget{gun.token,o.input.sequence,At,End,reload_insertion_detail::Identity()};
    t.currentContactGun=gun.token;t.currentContactInput=o.input.sequence;t.currentContactObserved=At;t.currentContactDeadline=End;
    o.contact.raw={o.owner,{},44,o.input,reload_insertion_detail::Identity(),true};return o;
}
int AbsenceAndClocks(){
    auto o=Magazine();const auto before=o.input;
    CHECK(!Joined<MagazineLiveObservation>(nullptr,o.owner,o.input,Now));CHECK(Joined(&o,o.owner,o.input,Now));
    CHECK(!Joined(&o,o.owner,o.input,End));CHECK(!Joined(&o,o.owner,o.input,At-1));
    auto changed=o.input;++changed.sequence;CHECK(!Joined(&o,o.owner,changed,Now));
    changed=o.input;changed.deadlineNs++;CHECK(!Joined(&o,o.owner,changed,Now));
    auto owner=o.owner;++owner.weak;CHECK(!Joined(&o,owner,o.input,Now));
    CHECK(MagazineRawCurrent(o,{},44,Now));o.raw.valid=false;CHECK(!MagazineRawCurrent(o,{},44,Now));
    o.raw.valid=true;o.originalHandEvidence.reset();CHECK(!MagazineRawCurrent(o,{},44,Now));
    o.originalHandEvidence=before;CHECK(!MagazineRawCurrent(o,{},45,Now));CHECK(!MagazineRawCurrent(o,{},44,End));
    CHECK(o.input.observedNs==before.observedNs&&o.input.deadlineNs==before.deadlineNs&&o.input.nowNs==before.nowNs);
    return 0;
}
int Custody(){
    auto o=Bolt();CHECK(BoltCustodyCurrent(o,Now));CHECK(BoltCurrentContactJoined(o,Now));CHECK(BoltRawCurrent(o,{},44,Now));
    o.contact.raw.valid=false;CHECK(!BoltRawCurrent(o,{},44,Now));CHECK(BoltCustodyCurrent(o,Now));
    // Current controller custody and missing renderer raw contact are distinct.
    CHECK(BoltCurrentContactJoined(o,Now));o.result.tracking.currentContactGun->id++;
    CHECK(!BoltCurrentContactJoined(o,Now));o=Bolt();o.left->token.id++;
    CHECK(!BoltCustodyCurrent(o,Now));CHECK(!BoltCurrentContactJoined(o,Now));
    o=Bolt();o.left.reset();CHECK(!BoltCustodyCurrent(o,Now));
    o=Bolt();CHECK(!BoltCustodyCurrent(o,End));CHECK(!BoltRawCurrent(o,{},44,End));
    o=Bolt();o.input.released[0]=true;CHECK(!BoltCustodyCurrent(o,Now));
    return 0;
}
int Replacement(){
    auto o=Magazine();CHECK(!MagazineReplacementCurrent(o,Now));
    AmmoSupplyObject replacement;replacement.identity={o.input.owner,o.weapon,{21,1},{22,1},7};replacement.item={23,2};replacement.units=30;
    replacement.family=ReloadInsertionFamily::Magazine;
    replacement.claim={{24,o.input.owner,InteractionHand::Left,HandClaimKind::AmmoObject,replacement.item,{25,1},26},End,o.input.sequence};
    o.right=HandClaim{{26,o.input.owner,InteractionHand::Right,HandClaimKind::GunHold,o.weapon,{27,1},0},End,o.input.sequence};
    o.state.carriedReplacement=replacement;o.left=replacement.claim;CHECK(MagazineReplacementCurrent(o,Now));
    o.right->token.id++;CHECK(!MagazineReplacementCurrent(o,Now));o.right->token.id--;
    o.left->token.id++;CHECK(!MagazineReplacementCurrent(o,Now));o.left=replacement.claim;
    ++o.state.carriedReplacement->identity.owner.actorGeneration;CHECK(!MagazineReplacementCurrent(o,Now));
    o.state.carriedReplacement=replacement;CHECK(!MagazineReplacementCurrent(o,End));
    o.state.resource.reset();CHECK(MagazineReplacementCurrent(o,Now));CHECK(!MagazineResourceCurrent(o,Now));
    CHECK(o.state.carriedReplacement->units==30);return 0;
}
int ReceiptOwnership(){
    auto o=Magazine();CHECK(MagazineResourceCurrent(o,Now));CHECK(MagazineReceiptJoined(o,Now));
    const auto receipt=*o.state.resource->receipt;
    for(unsigned fault=0;fault<11;++fault){auto bad=o;auto& v=*bad.state.resource;
        switch(fault){case 0:v.receipt->authorityVerified=false;break;case 1:v.receipt->copiesVerified=false;break;
        case 2:++v.receipt->command.context.resource.weaponGeneration;break;case 3:++v.terminal->request.intent.id;break;
        case 4:++v.terminal->request.binding.owner.weak;break;case 5:v.terminal->request.discard=true;break;
        case 6:v.terminal->nativeDispatched=false;break;case 7:v.receipt->completedNs=Now+1;break;
        case 8:++v.snapshot.counts.loaded;break;case 9:v.terminal.reset();break;case 10:v.snapshot.deadlineNs=Now;break;}
        CHECK(!MagazineReceiptJoined(bad,Now));
    }
    CHECK(!MagazineReceiptJoined(o,End));CHECK(*o.state.resource->receipt==receipt);
    o.state.resource.reset();CHECK(!MagazineReceiptJoined(o,Now));return 0;
}
int SerializationRefusesAbsent(){
    const auto o=Magazine();std::ostringstream stream;stream<<std::boolalpha;Json json(stream);
    json.Magazine(nullptr,o.owner,o.input,{},44,"","",Now);
    CHECK(stream.str().find("\"available\":false")!=std::string::npos);
    CHECK(stream.str().find("capture_pending_or_not_requested")!=std::string::npos);
    std::ostringstream expired;expired<<std::boolalpha;Json stale(expired);
    stale.Magazine(&o,o.owner,o.input,{},44,"","",End);
    CHECK(expired.str().find("source_mismatched_or_expired")!=std::string::npos);
    CHECK(expired.str().find("authorityVerified")==std::string::npos);
    std::ostringstream native;native<<std::boolalpha;Json writer(native);writer.Receipt(*o.state.resource->receipt);
    CHECK(native.str().find("\"authorityInvocation\":79")!=std::string::npos);
    CHECK(native.str().find("\"copiesVerified\":true")!=std::string::npos);
    CHECK(native.str().find("\"completedNs\":1000000000")!=std::string::npos);return 0;
}
int SnapshotBudget(){
    using live_snapshot::Compose;using live_snapshot::OmittedSightResolution;
    constexpr std::size_t Limit=live_snapshot::MaximumBytes;
    static_assert(Limit==65536);
    const std::string before=R"({"valid":true,"owner":{"weapon":9},"input_evidence":{"sequence":7,"deadlineNs":122280080904400},"sight":{"status":"available","family":{"selectedSecondary":false},"resolutionObservation":)";
    // Match the saved actual513 optional resolver value's 5770-byte budget.
    // Delimiter-looking escaped text must have no effect on captured offsets.
    const std::string diagnosticHead=R"({"resolution":{"candidates":["\"resolutionObservation\":{)",diagnosticTail=R"("]}})";
    const auto resolution=diagnosticHead+std::string(5770-diagnosticHead.size()-diagnosticTail.size(),'x')+diagnosticTail;
    const std::string after=R"(,"adapter":{"root":"jntWpn_1"},"contact":{"valid":true,"deadlineNs":122280080904400},"native":{"valid":true,"physicalItem":9}})";
    const std::string feed=R"(,"feedAdmission":{"firstFailedRetention":{"sequence":7,"deadlineNs":122280080904400},"latestChange":{"detail":"optional diagnostic detail"}})";
    const std::string compactFeed=R"(,"feedAdmission":{"firstFailedRetention":{"sequence":7,"deadlineNs":122280080904400},"latestChange":{"omitted":"payload_limit"}})";
    const std::string suffix=R"(,"magazine":{"available":true,"input":{"sequence":7,"deadlineNs":122280080904400},"receipt":{"authorityInvocation":79,"copiesVerified":true}},"bolt":{"available":true,"custody":{"claim":42,"deadlineNs":122280080904400}},"chestKnife":{"available":false,"reason":"unsupported_configuration"}})" "\n";
    std::ostringstream stream;stream<<before;const auto begin=static_cast<std::size_t>(stream.tellp());
    stream<<resolution;const auto end=static_cast<std::size_t>(stream.tellp());stream<<after;
    const auto prefix=stream.str();const auto full=prefix+feed+suffix;
    CHECK(resolution.size()==5770);
    CHECK(Compose(prefix,feed,compactFeed,suffix,begin,end,Limit)==full);
    const auto exactPrefix=prefix+std::string(Limit-full.size(),' ');
    CHECK(Compose(exactPrefix,feed,compactFeed,suffix,begin,end,Limit)==exactPrefix+feed+suffix);
    CHECK((exactPrefix+feed+suffix).size()==Limit);
    // One-byte overflow must try Feed first and leave sight diagnostics intact.
    const auto feedPrefix=exactPrefix+' ';
    const auto feedOnly=Compose(feedPrefix,feed,compactFeed,suffix,begin,end,Limit);
    CHECK(feedOnly==feedPrefix+compactFeed+suffix);CHECK(feedOnly.size()<=Limit);
    CHECK(feedOnly.find(resolution)!=std::string::npos);
    // Even compact Feed overflows by one: only the captured sight value changes.
    const auto sightPrefix=prefix+std::string(Limit+1-(prefix.size()+compactFeed.size()+suffix.size()),' ');
    const auto expected=[&](const std::string& p){auto value=p;value.replace(begin,end-begin,OmittedSightResolution);return value+compactFeed+suffix;};
    const auto sightOnly=Compose(sightPrefix,feed,compactFeed,suffix,begin,end,Limit);
    CHECK(sightOnly==expected(sightPrefix));CHECK(sightOnly.size()<=Limit);
    CHECK(sightOnly.find(before)==0);CHECK(sightOnly.find(after)!=std::string::npos);
    CHECK(sightOnly.ends_with(suffix));CHECK(sightOnly.find("\"status\":\"omitted\"")!=std::string::npos);
    // Exact final boundary succeeds; one byte beyond remains oversized so the
    // production caller explicitly refuses rather than losing action evidence.
    const auto finalPrefix=sightPrefix+std::string(resolution.size()-OmittedSightResolution.size()-1,' ');
    CHECK(Compose(finalPrefix,feed,compactFeed,suffix,begin,end,Limit).size()==Limit);
    const auto oversized=Compose(finalPrefix+' ',feed,compactFeed,suffix,begin,end,Limit);
    CHECK(oversized.size()==Limit+1);CHECK(oversized==expected(finalPrefix+' '));
    CHECK(oversized.ends_with(suffix));
    // Failed positions or a short unavailable value cannot remove other fields.
    for(const auto range:{std::pair{end,begin},std::pair{begin,sightPrefix.size()+1},
                          std::pair{std::size_t(-1),std::size_t(-1)}})
        CHECK(Compose(sightPrefix,feed,compactFeed,suffix,range.first,range.second,Limit)==sightPrefix+compactFeed+suffix);
    const std::string absent=R"({"status":"capture_pending_or_not_requested"})";
    const auto absentPrefix=before+absent+after+std::string(Limit,' ');
    CHECK(Compose(absentPrefix,feed,compactFeed,suffix,before.size(),before.size()+absent.size(),Limit)==absentPrefix+compactFeed+suffix);
    return 0;
}
}
int main(){if(AbsenceAndClocks()||Custody()||Replacement()||ReceiptOwnership()||SerializationRefusesAbsent()||SnapshotBudget())return 1;
    std::puts("Passive magazine/bolt observation contract regressions passed");}
