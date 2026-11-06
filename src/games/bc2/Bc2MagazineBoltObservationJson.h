#pragma once
#include "Bc2MagazineBoltObservation.h"
#include <cmath>
#include <ostream>

namespace fvr::bc2::live_magazine_bolt {
// Serializes only the immutable post-Tick copies. No native/consumer getters.
class Json {
    std::ostream& out;
public:
    explicit Json(std::ostream& stream):out(stream){}
    void Text(std::string_view value){out<<'"';for(unsigned char c:value){
        if(c=='"'||c=='\\')out<<'\\'<<char(c);
        else if(c<32){constexpr char hex[]="0123456789abcdef";out<<"\\u00"<<hex[c>>4]<<hex[c&15];}
        else out<<char(c);}out<<'"';}
    void Number(float value){if(std::isfinite(value))out<<value;else out<<"null";}
    void Matrix(const math::Matrix4& value){if(!interaction::reload_insertion_detail::Rigid(value)){out<<"null";return;}
        out<<'[';for(unsigned r=0;r<4;++r){if(r)out<<',';out<<'[';for(unsigned c=0;c<4;++c){if(c)out<<',';Number(value.values[r][c]);}out<<']';}out<<']';}
    template<class T> void Clock(const T& v){out<<"\"sequence\":"<<v.sequence<<",\"observedNs\":"<<v.observedNs<<",\"deadlineNs\":"<<v.deadlineNs;}
    template<class T> void Counts(const T& v){out<<"{\"loaded\":"<<v.loaded<<",\"reserve\":"<<v.reserve<<",\"capacity\":"<<v.capacity<<'}';}
    void Key(interaction::HandInteractionKey v){out<<"{\"id\":"<<v.id<<",\"generation\":"<<v.generation<<'}';}
    void Owner(const ReloadStateOwner& v){out<<"{\"player\":"<<v.player<<",\"soldier\":"<<v.soldier<<",\"weak\":"<<v.weak
        <<",\"weapon\":"<<v.weapon<<",\"actorGeneration\":"<<v.actorGeneration<<",\"equipGeneration\":"<<v.equipGeneration<<",\"space\":"<<v.space<<'}';}
    void HandOwner(const interaction::HandInteractionOwner& v){out<<"{\"actor\":"<<v.actor<<",\"actorGeneration\":"<<v.actorGeneration
        <<",\"equipGeneration\":"<<v.equipGeneration<<",\"space\":"<<v.space<<'}';}
    void Input(const interaction::HandInteractionSample& v){out<<"{\"owner\":";HandOwner(v.owner);out<<',';Clock(v);
        out<<",\"nowNs\":"<<v.nowNs<<",\"focused\":"<<v.focused<<",\"tracked\":["<<v.tracked[0]<<','<<v.tracked[1]
            <<"],\"released\":["<<v.released[0]<<','<<v.released[1]<<"]}";}
    void Token(const interaction::HandClaimToken& v){out<<"{\"id\":"<<v.id<<",\"owner\":";HandOwner(v.owner);
        out<<",\"hand\":"<<unsigned(v.hand)<<",\"kind\":"<<unsigned(v.kind)<<",\"item\":";Key(v.item);
        out<<",\"contact\":";Key(v.contact);out<<",\"prerequisiteClaim\":"<<v.prerequisiteClaim<<'}';}
    void Claim(const interaction::HandClaim& v){out<<"{\"token\":";Token(v.token);out<<",\"inputSequence\":"<<v.inputSequence<<",\"deadlineNs\":"<<v.deadlineNs<<'}';}
    void Claim(const std::optional<interaction::HandClaim>& v){if(v)Claim(*v);else out<<"null";}
    void ResourceIdentity(const interaction::AmmoResourceIdentity& v){out<<"{\"actor\":"<<v.actor<<",\"actorGeneration\":"<<v.actorGeneration
        <<",\"weapon\":"<<v.weapon<<",\"weaponGeneration\":"<<v.weaponGeneration<<'}';}
    void Context(const interaction::AmmoResourceContext& v){out<<"{\"resource\":";ResourceIdentity(v.resource);out<<",\"equipGeneration\":"<<v.equipGeneration<<",\"space\":"<<v.space<<'}';}
    void Original(const std::optional<interaction::RemovedMagazineResource>& v){if(!v){out<<"null";return;}
        out<<"{\"owner\":";ResourceIdentity(v->owner);out<<",\"id\":"<<v->id<<",\"rounds\":"<<v->rounds<<",\"capacity\":"<<v->capacity<<'}';}
    void Command(const interaction::AmmunitionCommand& c){out<<"{\"context\":";Context(c.context);out<<",\"id\":"<<c.id<<",\"sourceSequence\":"<<c.sourceSequence
        <<",\"requestedNs\":"<<c.requestedNs<<",\"admissionDeadlineNs\":"<<c.admissionDeadlineNs<<",\"deadlineNs\":"<<c.deadlineNs
        <<",\"operation\":"<<unsigned(c.operation)<<",\"mutationRequired\":"<<c.mutationRequired<<",\"before\":";Counts(c.before);
        out<<",\"after\":";Counts(c.after);out<<",\"original\":";Original(c.original);out<<'}';}
    void Receipt(const interaction::AmmunitionReceipt& r){out<<"{\"command\":";Command(r.command);out<<",\"authorityInvocation\":"<<r.authorityInvocation
        <<",\"beganNs\":"<<r.beganNs<<",\"completedNs\":"<<r.completedNs<<",\"authorityVerified\":"<<r.authorityVerified
        <<",\"copiesVerified\":"<<r.copiesVerified<<",\"before\":";Counts(r.before);out<<",\"after\":";Counts(r.after);out<<'}';}
    void Binding(const AmmoResourceBinding& b){out<<"{\"owner\":";Owner(b.owner);out<<",\"context\":";Context(b.context);
        out<<",\"inventory\":"<<b.inventory<<",\"switching\":"<<b.switching<<",\"data\":"<<b.data<<",\"persistence\":"<<b.persistence
            <<",\"observedNs\":"<<b.observedNs<<",\"deadlineNs\":"<<b.deadlineNs<<'}';}
    void Identity(const ReloadHoldIdentity& v){out<<"{\"owner\":";Owner(v.owner);out<<",\"firing\":["<<v.firing[0]<<','<<v.firing[1]<<','<<v.firing[2]
        <<"],\"serverPlayer\":"<<v.serverPlayer<<",\"serverSoldier\":"<<v.serverSoldier<<",\"serverItem\":"<<v.serverItem<<'}';}
    void Request(const std::optional<AmmoResourceRequest>& v){if(!v){out<<"null";return;}const auto& r=*v;const auto& i=r.intent;
        out<<"{\"binding\":";Binding(r.binding);out<<",\"discard\":"<<r.discard<<",\"intent\":{\"context\":";Context(i.context);
        out<<",\"id\":"<<i.id<<",\"observedNs\":"<<i.observedNs<<",\"deadlineNs\":"<<i.deadlineNs<<",\"operation\":"<<unsigned(i.operation)
            <<",\"original\":";Original(i.original);out<<"},\"sourceProof\":";
        if(!r.sourceProof)out<<"null";else{const auto& p=*r.sourceProof;out<<"{\"constructedNs\":"<<p.constructedNs
            <<",\"inputSequence\":"<<p.inputSequence<<",\"snapshotSequence\":"<<p.snapshotSequence
            <<",\"snapshotObservedNs\":"<<p.snapshotObservedNs<<",\"snapshotDeadlineNs\":"<<p.snapshotDeadlineNs<<'}';}out<<'}';}
    void SupplyIdentity(const interaction::AmmoSupplyIdentity& v){out<<"{\"owner\":";HandOwner(v.owner);out<<",\"weapon\":";Key(v.weapon);
        out<<",\"profile\":";Key(v.profile);out<<",\"pool\":";Key(v.pool);out<<",\"trackingEpoch\":"<<v.trackingEpoch<<'}';}
    void PhysicalOriginal(const std::optional<interaction::OriginalMagazine>& v){if(!v){out<<"null";return;}
        out<<"{\"owner\":";HandOwner(v->owner);out<<",\"weapon\":";Key(v->weapon);out<<",\"item\":";Key(v->item);
        out<<",\"profile\":";Key(v->profile);out<<",\"pool\":";Key(v->pool);out<<",\"trackingEpoch\":"<<v->trackingEpoch
            <<",\"sourceSequence\":"<<v->sourceSequence<<",\"observedNs\":"<<v->observedNs<<",\"deadlineNs\":"<<v->deadlineNs
            <<",\"rounds\":"<<v->rounds<<",\"capacity\":"<<v->capacity<<'}';}
    void Insertion(const interaction::ReloadInsertionProfile& p){out<<"{\"id\":"<<p.id<<",\"revision\":"<<p.revision<<",\"orientation\":"<<unsigned(p.orientation)
        <<",\"approach\":"<<unsigned(p.approach)<<",\"itemFromHand\":";Matrix(p.itemFromHand);out<<",\"itemFromInsertion\":";Matrix(p.itemFromInsertion);
        out<<",\"weaponFromEntry\":";Matrix(p.weaponFromEntry);out<<",\"travelDirection\":[";for(unsigned i=0;i<3;++i){if(i)out<<',';Number(p.travelDirection[i]);}
        out<<"],\"travelMeters\":";Number(p.travelMeters);out<<",\"captureDistanceMeters\":";Number(p.captureDistanceMeters);
        out<<",\"releaseDistanceMeters\":";Number(p.releaseDistanceMeters);out<<",\"captureAngleRadians\":";Number(p.captureAngleRadians);
        out<<",\"releaseAngleRadians\":";Number(p.releaseAngleRadians);out<<",\"seatToleranceMeters\":";Number(p.seatToleranceMeters);
        out<<",\"postCaptureTravelMeters\":";Number(p.postCaptureTravelMeters);out<<",\"maxStepMeters\":";Number(p.maxStepMeters);
        out<<",\"maxStepRadians\":";Number(p.maxStepRadians);out<<",\"alignmentNs\":"<<p.alignmentNs<<",\"seatDwellNs\":"<<p.seatDwellNs
            <<",\"maxSampleGapNs\":"<<p.maxSampleGapNs<<",\"maxGuidedNs\":"<<p.maxGuidedNs<<",\"pathCount\":"<<p.pathCount<<",\"path\":[";
        for(unsigned i=0;i<std::min({p.pathCount,unsigned(p.path.size()),interaction::ReloadInsertionPathLimit});++i){if(i)out<<',';
            out<<"{\"weaponFromItem\":";Matrix(p.path[i].weaponFromItem);out<<",\"arcMeters\":";Number(p.path[i].arcMeters);out<<'}';}out<<"]}";}
    template<class Lease> void Cycle(const Lease& v){out<<"{\"owner\":";HandOwner(v.owner);out<<",\"item\":";Key(v.item);out<<",\"mechanism\":";Key(v.mechanism);
        out<<",\"cycle\":"<<v.cycle<<",\"shot\":"<<v.shot<<',';Clock(v);
        if constexpr(requires{v.held;}){out<<",\"held\":"<<v.held<<",\"advancing\":";if(!v.advancing)out<<"null";else{const auto& a=*v.advancing;
            out<<"{\"guardEpoch\":"<<a.guardEpoch<<",\"updates\":["<<a.updates[0]<<','<<a.updates[1]<<','<<a.updates[2]<<"],\"counts\":";Counts(a);out<<'}';}}
        else{const auto& d=v.grant.debt;out<<",\"grant\":{\"nonce\":"<<v.grant.nonce<<",\"neutral\":";Input(v.grant.neutral);
            out<<",\"debt\":{\"owner\":";HandOwner(d.owner);out<<",\"item\":";Key(d.item);out<<",\"mechanism\":";Key(d.mechanism);
            out<<",\"cycle\":"<<d.cycle<<",\"shot\":"<<d.shot<<",\"guardEpoch\":"<<d.guardEpoch<<",\"counts\":";Counts(d);
            out<<",\"ammunitionRebase\":";if(!d.ammunitionRebase)out<<"null";else{const auto& r=*d.ammunitionRebase;
                out<<"{\"owner\":";HandOwner(r.owner);out<<",\"item\":";Key(r.item);out<<",\"mechanism\":";Key(r.mechanism);
                out<<",\"cycle\":"<<r.cycle<<",\"shot\":"<<r.shot<<",\"pause\":"<<r.pause<<",\"previousGuardEpoch\":"<<r.previousGuardEpoch
                    <<",\"guardEpoch\":"<<r.guardEpoch<<",\"drainRevision\":"<<r.drainRevision<<",\"ownUpdates\":["<<r.ownUpdates[0]<<','<<r.ownUpdates[1]<<','<<r.ownUpdates[2]<<"],\"receipt\":";Receipt(r.receipt);out<<'}';}
            out<<"}},\"first\":["<<v.first[0]<<','<<v.first[1]<<','<<v.first[2]<<"],\"second\":["<<v.second[0]<<','<<v.second[1]<<','<<v.second[2]<<']';}out<<'}';}
    template<class Ready> void ReadyEvent(const Ready& v){const auto& r=v.release;out<<"{\"release\":{\"cycle\":";Cycle(r.cycle);
        out<<",\"request\":"<<r.request<<",\"inputSequence\":"<<r.inputSequence<<",\"observedNs\":"<<r.observedNs<<",\"deadlineNs\":"<<r.deadlineNs<<"},";Clock(v);
        out<<",\"nativeReady\":"<<v.nativeReady<<",\"unchangedAmmunition\":"<<v.unchangedAmmunition<<'}';}
    template<class Target> void TargetPose(const Target& v){out<<"{\"lease\":";Cycle(v.lease);out<<",\"mechanism\":";Token(v.mechanism);
        out<<",\"gun\":";Token(v.gun);out<<",\"profile\":"<<v.profile<<",\"revision\":"<<v.revision<<",\"inputSequence\":"<<v.inputSequence
            <<",\"observedNs\":"<<v.observedNs<<",\"deadlineNs\":"<<v.deadlineNs<<",\"contact\":";Matrix(v.contact);
        out<<",\"travel\":";Number(v.travel);out<<",\"rotation\":";Number(v.rotation);out<<'}';}
    void Pause(const interaction::WeaponCycleReloadPause& p){out<<"{\"owner\":";HandOwner(p.owner);out<<",\"item\":";Key(p.item);out<<",\"mechanism\":";Key(p.mechanism);
        out<<",\"cycle\":"<<p.cycle<<",\"shot\":"<<p.shot<<",\"pause\":"<<p.pause<<",\"profile\":"<<p.profile<<",\"revision\":"<<p.revision
            <<",\"inputSequence\":"<<p.inputSequence<<",\"observedNs\":"<<p.observedNs<<",\"deadlineNs\":"<<p.deadlineNs
            <<",\"cycleDeadlineNs\":"<<p.cycleDeadlineNs<<",\"pausedNs\":"<<p.pausedNs<<",\"pauseDeadlineNs\":"<<p.pauseDeadlineNs
            <<",\"travel\":";Number(p.travel);out<<",\"rotation\":";Number(p.rotation);out<<'}';}
    void Unavailable(const char* reason){out<<"{\"observationOnly\":true,\"available\":false,\"reason\":";Text(reason);out<<'}';}
    void Magazine(const MagazineLiveObservation* o,const ReloadStateOwner& owner,const interaction::HandInteractionSample& input,
        const RigIdentity& rig,std::uint64_t fingerprint,std::string_view configuration,std::string_view mesh,std::int64_t now){
        if(!Joined(o,owner,input,now)){Unavailable(o?"source_mismatched_or_expired":"capture_pending_or_not_requested");return;}
        const auto* p=o->profile;const bool profile=p&&p->Ready()&&p->native->configuration.assetPath==configuration&&
            p->geometry->mesh==mesh&&p->geometry->rigFingerprint==fingerprint;
        if(!profile){Unavailable("profile_configuration_or_rig_mismatch");return;}
        const auto& s=o->state;const auto& r=s.result;const auto& t=r.tracking;const auto& raw=o->raw;
        out<<"{\"observationOnly\":true,\"available\":true,\"retainedEventsAreHistorical\":true,\"owner\":";Owner(o->owner);
        out<<",\"input\":";Input(o->input);out<<",\"capturedNs\":"<<o->capturedNs<<",\"weapon\":";Key(o->weapon);
        out<<",\"resourceBackend\":"<<o->resourceBackend<<",\"cancel\":"<<o->cancel<<",\"phase\":"<<unsigned(r.interaction.phase)
            <<",\"reason\":"<<unsigned(r.interaction.reason)<<",\"reloadHeld\":"<<r.reloadHeld<<",\"blocksWeaponActions\":"<<r.blocksWeaponActions
            <<",\"ownsLeftHand\":"<<r.ownsLeftHand<<",\"cycle\":"<<t.cycle<<",\"seatedRequest\":"<<s.seatedRequest
            <<",\"acquired\":"<<r.acquired<<",\"submitted\":"<<r.submitted<<",\"completed\":"<<r.completed;
        out<<",\"leftClaim\":";Claim(o->left);out<<",\"rightClaim\":";Claim(o->right);
        out<<",\"leftClaimCurrent\":"<<ClaimCurrent(o->left,o->input,now)<<",\"rightClaimCurrent\":"<<ClaimCurrent(o->right,o->input,now);
        out<<",\"family\":{\"owner\":";Owner(o->family.binding.owner);out<<",\"weapon\":";Key(o->family.binding.weapon);
        out<<",\"inventory\":"<<o->family.binding.inventory<<",\"observedNs\":"<<o->family.observedNs<<",\"deadlineNs\":"<<o->family.deadlineNs
            <<",\"verified\":"<<o->family.verified<<",\"current\":"<<MagazineFamilyFresh(o->family,o->owner,o->input.owner,o->weapon,now)<<'}';
        out<<",\"profile\":{\"nativeId\":"<<std::uint64_t(p->nativeId)<<",\"configurationPath\":";Text(p->native->configuration.assetPath);
        out<<",\"mesh\":";Text(p->geometry->mesh);out<<",\"rigFingerprint\":"<<p->geometry->rigFingerprint<<",\"geometryVerified\":"<<p->geometryVerified
            <<",\"experimentalGeometry\":"<<p->experimentalGeometry<<",\"attachedItem\":";Matrix(p->geometry->attachedItem);
        const auto& g=p->geometry->interaction;out<<",\"grabRadiusMeters\":";Number(g.grabRadiusMeters);out<<",\"pullMeters\":";Number(g.pullMeters);
        out<<",\"pullReleaseMeters\":";Number(g.pullReleaseMeters);out<<",\"maxPullStepMeters\":";Number(g.maxPullStepMeters);
        out<<",\"insertion\":";Insertion(g.insertion);out<<"},\"raw\":{\"valid\":"<<raw.valid<<",\"current\":"<<MagazineRawCurrent(*o,rig,fingerprint,now)
            <<",\"reason\":"<<unsigned(raw.evidence.reason)<<",\"nativeMagazineAttached\":"<<raw.nativeMagazineAttached<<",\"owner\":";Owner(raw.owner);
        out<<",\"rigFingerprint\":"<<raw.rigFingerprint<<",\"rigMatches\":"<<(raw.rig==rig)<<",\"input\":";Input(raw.inputEvidence);
        out<<",\"originalHandEvidence\":";if(o->originalHandEvidence)Input(*o->originalHandEvidence);else out<<"null";
        out<<",\"rawWristWorldMeters\":";Matrix(raw.rawLeftWristWorldMeters);out<<",\"weaponWorldMeters\":";Matrix(raw.weaponWorldMeters);out<<'}';
        out<<",\"pouch\":{\"centerMeters\":[";for(unsigned i=0;i<3;++i){if(i)out<<',';Number(o->pouch.centerMeters[i]);}out<<"],\"radiusMeters\":";Number(o->pouch.radiusMeters);out<<'}';
        out<<",\"originalPhysical\":";PhysicalOriginal(s.original);out<<",\"pendingRequest\":";Request(s.pendingRequest);out<<",\"submittedSeat\":";Request(s.submittedSeat);
        out<<",\"removalRequest\":";if(s.pendingRequest&&!s.pendingRequest->discard&&s.pendingRequest->intent.operation==interaction::AmmunitionOperation::RemoveMagazine)
            out<<s.pendingRequest->intent.id;else out<<"null";
        out<<",\"requestPending\":"<<s.requestPending;
        out<<",\"carriedReplacement\":";if(!s.carriedReplacement)out<<"null";else{const auto& c=*s.carriedReplacement;
            out<<"{\"current\":"<<MagazineReplacementCurrent(*o,now)<<",\"identity\":";SupplyIdentity(c.identity);out<<",\"item\":";Key(c.item);out<<",\"claim\":";Claim(c.claim);
            out<<",\"units\":"<<c.units<<",\"family\":"<<unsigned(c.family)<<'}';}
        out<<",\"reservation\":";if(!s.reservation)out<<"null";else{const auto& v=*s.reservation;out<<"{\"identity\":";SupplyIdentity(v.identity);
            out<<",\"item\":";Key(v.item);out<<",\"claim\":";Token(v.claim);out<<",\"seat\":"<<v.seat<<",\"request\":"<<v.request<<",\"cycle\":"<<v.cycle
                <<",\"sourceSequence\":"<<v.sourceSequence<<",\"startedNs\":"<<v.startedNs<<",\"units\":"<<v.units<<",\"reserveBefore\":"<<v.reserveBefore<<'}';}
        out<<",\"target\":";if(!t.target)out<<"null";else{const auto& v=*t.target;out<<"{\"role\":"<<unsigned(v.role)<<",\"owner\":";HandOwner(v.owner);
            out<<",\"weapon\":";Key(v.weapon);out<<",\"item\":";Key(v.item);out<<",\"handClaim\":";Token(v.handClaim);out<<",\"gunClaim\":";Token(v.gunClaim);
            out<<",\"inputSequence\":"<<v.inputSequence<<",\"observedNs\":"<<v.observedNs<<",\"deadlineNs\":"<<v.deadlineNs<<",\"nativeCycle\":"<<v.nativeCycle
                <<",\"handTarget\":"<<v.handTarget<<",\"weaponFromItemMeters\":";Matrix(v.weaponFromItemMeters);out<<",\"weaponFromHandMeters\":";Matrix(v.weaponFromHandMeters);out<<'}';}
        out<<",\"resource\":";if(!s.resource)out<<"null";else{const auto& v=*s.resource;out<<"{\"current\":"<<MagazineResourceCurrent(*o,now)
            <<",\"binding\":";Binding(v.binding);out<<",\"identity\":";Identity(v.identity);out<<",\"snapshot\":{\"context\":";Context(v.snapshot.context);out<<',';Clock(v.snapshot);
            out<<",\"coherentIdleVerified\":"<<v.snapshot.coherentIdleVerified<<",\"counts\":";Counts(v.snapshot.counts);
            out<<"},\"phase\":"<<unsigned(v.phase)<<",\"wellEmpty\":"<<v.wellEmpty<<",\"originalState\":"<<unsigned(v.originalState)<<",\"original\":";Original(v.original);
            out<<",\"request\":"<<v.request<<",\"requestState\":"<<unsigned(v.requestState)<<",\"command\":";if(v.command)Command(*v.command);else out<<"null";
            out<<",\"receiptJoined\":"<<MagazineReceiptJoined(*o,now)<<",\"receipt\":";if(v.receipt)Receipt(*v.receipt);else out<<"null";
            out<<",\"terminal\":";if(!v.terminal)out<<"null";else{const auto& terminal=*v.terminal;
                out<<"{\"request\":";Request(terminal.request);out<<",\"state\":"<<unsigned(terminal.state)<<",\"resolvedNs\":"<<terminal.resolvedNs
                    <<",\"nativeDispatched\":"<<terminal.nativeDispatched<<",\"receipt\":";if(terminal.receipt)Receipt(*terminal.receipt);else out<<"null";out<<'}';}out<<'}';}out<<'}';
    }
    void Bolt(const BoltLiveObservation* o,const ReloadStateOwner& owner,const interaction::HandInteractionSample& input,
        const RigIdentity& rig,std::uint64_t fingerprint,std::string_view configuration,std::string_view mesh,std::int64_t now){
        if(!Joined(o,owner,input,now)){Unavailable(o?"source_mismatched_or_expired":"capture_pending_or_not_requested");return;}
        const auto& r=o->result;const auto& t=r.tracking;const auto c=t.calibration;
        if(t.nativeOwner!=o->owner||!SameInput(t.input,o->input)){Unavailable("tracking_owner_or_input_mismatch");return;}
        if(!c||!BoltCalibrationValid(*c)||c->configurationPath!=configuration||c->mesh!=mesh||c->rigFingerprint!=fingerprint){Unavailable("calibration_configuration_or_rig_mismatch");return;}
        out<<"{\"observationOnly\":true,\"available\":true,\"retainedEventsAreHistorical\":true,\"owner\":";Owner(o->owner);out<<",\"input\":";Input(o->input);
        out<<",\"capturedNs\":"<<o->capturedNs<<",\"cancel\":"<<o->cancel<<",\"blocksFire\":"<<r.blocksFire<<",\"ownsGunCustody\":"<<r.ownsGunCustody
            <<",\"ownsMechanism\":"<<r.ownsMechanism<<",\"custody\":"<<unsigned(t.custody)<<",\"mechanismPhase\":"<<unsigned(t.mechanismPhase)
            <<",\"mechanismFailure\":"<<unsigned(t.mechanismFailure)<<",\"custodyCurrent\":"<<BoltCustodyCurrent(*o,now);
        out<<",\"trackingRawContactValid\":"<<t.rawContactValid<<",\"trackingRawInputSequence\":"<<t.rawInputSequence
            <<",\"trackingRawContactDistanceMeters\":";Number(t.rawContactDistance);
        out<<",\"leftClaim\":";Claim(o->left);out<<",\"rightClaim\":";Claim(o->right);out<<",\"gunClaim\":";Claim(t.gun);out<<",\"mechanismClaim\":";Claim(t.mechanism);
        out<<",\"controllerCustody\":";if(o->controllerCustody)Token(*o->controllerCustody);else out<<"null";
        out<<",\"originalGripReferences\":";if(!o->originalGripReferences)out<<"null";else{
            out<<"{\"geometryOnly\":true,\"sourceInputClockAvailable\":false,\"wristInWeapon\":[";
            Matrix((*o->originalGripReferences)[0]);out<<',';Matrix((*o->originalGripReferences)[1]);out<<"]}";}
        out<<",\"currentContact\":{\"joined\":"<<BoltCurrentContactJoined(*o,now)<<",\"inputSequence\":"<<t.currentContactInput
            <<",\"observedNs\":"<<t.currentContactObserved<<",\"deadlineNs\":"<<t.currentContactDeadline<<",\"gun\":";if(t.currentContactGun)Token(*t.currentContactGun);else out<<"null";out<<'}';
        const auto& raw=o->contact.raw;out<<",\"raw\":{\"valid\":"<<raw.valid<<",\"current\":"<<BoltRawCurrent(*o,rig,fingerprint,now)<<",\"owner\":";Owner(raw.nativeOwner);
        out<<",\"rigMatches\":"<<(raw.rig==rig)<<",\"rigFingerprint\":"<<raw.rigFingerprint<<",\"input\":";Input(raw.input);
        out<<",\"mechanismWristInWeapon\":";Matrix(raw.mechanismWristInWeapon);out<<",\"mappingValid\":"<<o->contact.mappingValid
            <<",\"bodyWorldMeters\":";Matrix(o->contact.bodyWorldMeters);out<<",\"weaponWorldMeters\":";Matrix(o->contact.weaponWorldMeters);
        out<<",\"rawWristWorldMeters\":[";Matrix(o->contact.rawWristWorldMeters[0]);out<<',';Matrix(o->contact.rawWristWorldMeters[1]);
        out<<"],\"wristToGrip\":[";Matrix(o->contact.wristToGrip[0]);out<<',';Matrix(o->contact.wristToGrip[1]);out<<"]}";
        const auto& p=c->profile;out<<",\"calibration\":{\"asset\":";Text(c->asset);out<<",\"mesh\":";Text(c->mesh);out<<",\"configurationPath\":";Text(c->configurationPath);
        out<<",\"partName\":";Text(c->partName);out<<",\"rigFingerprint\":"<<c->rigFingerprint<<",\"nativeJoined\":"<<c->nativeJoined<<",\"authoredReviewed\":"<<c->authoredReviewed
            <<",\"profile\":"<<p.id<<",\"revision\":"<<p.revision<<",\"mechanismHand\":"<<unsigned(p.hands.mechanism)<<",\"gunHand\":"<<unsigned(p.hands.gun)
            <<",\"closedContact\":";Matrix(p.closedContact);out<<",\"wristFromPart\":";Matrix(c->wristFromPart);out<<",\"axis\":[";for(unsigned i=0;i<3;++i){if(i)out<<',';Number(p.axis[i]);}
        out<<"],\"strokeMeters\":";Number(p.stroke);out<<",\"unlockRadians\":";Number(p.unlockRadians);out<<",\"rotationTolerance\":";Number(p.rotationTolerance);
        out<<",\"contactRadius\":";Number(p.contactRadius);out<<",\"lateralTolerance\":";Number(p.lateralTolerance);out<<",\"rearTolerance\":";Number(p.rearTolerance);
        out<<",\"frontTolerance\":";Number(p.frontTolerance);out<<",\"maxStepMeters\":";Number(p.maxStepMeters);
        out<<",\"endpointDwellNs\":"<<p.endpointDwellNs<<",\"maximumCycleNs\":"<<p.maximumCycleNs<<'}';
        out<<",\"held\":";if(t.held)Cycle(*t.held);else out<<"null";out<<",\"recovery\":";if(t.recovery)Cycle(*t.recovery);else out<<"null";
        out<<",\"target\":";if(t.target)TargetPose(*t.target);else out<<"null";out<<",\"recoveryTarget\":";if(t.recoveryTarget)TargetPose(*t.recoveryTarget);else out<<"null";
        out<<",\"weaponTarget\":";if(!t.weapon)out<<"null";else{const auto& v=*t.weapon;out<<"{\"gun\":";Token(v.gun);
            out<<",\"inputSequence\":"<<v.inputSequence<<",\"observedNs\":"<<v.observedNs<<",\"deadlineNs\":"<<v.deadlineNs<<",\"weaponInWorld\":";Matrix(v.weaponInWorld);out<<'}';}
        out<<",\"settled\":";if(r.settled)ReadyEvent(*r.settled);else out<<"null";out<<",\"recovered\":";if(r.recovered)ReadyEvent(*r.recovered);else out<<"null";
        out<<",\"completion\":";if(!o->completion)out<<"null";else{const auto& event=*o->completion;
            out<<"{\"acknowledged\":true,\"native\":";Identity(event.native);
            out<<",\"settled\":";if(event.settled)ReadyEvent(*event.settled);else out<<"null";
            out<<",\"recovered\":";if(event.recovered)ReadyEvent(*event.recovered);else out<<"null";out<<'}';}
        out<<",\"reloadPause\":";if(t.reloadPause)Pause(*t.reloadPause);else out<<"null";out<<",\"reloadContact\":";if(t.reloadContact)Matrix(*t.reloadContact);else out<<"null";
        out<<",\"reloadMechanicalPose\":";if(!t.reloadMechanicalPose)out<<"null";else{const auto& v=*t.reloadMechanicalPose;
            out<<"{\"pause\":";Pause(v.pause);out<<",\"authority\":";Cycle(v.authority);out<<",\"gun\":";Token(v.gun);
            out<<",\"resumedNs\":"<<v.resumedNs<<",\"cycleDeadlineNs\":"<<v.cycleDeadlineNs<<'}';}
        out<<",\"custodyChange\":";if(!r.custodyChange)out<<"null";else{const auto& v=*r.custodyChange;out<<"{\"accepted\":"<<v.transaction.accepted
            <<",\"reason\":"<<unsigned(v.transaction.reason)<<",\"gun\":";Claim(v.transaction.claim);out<<",\"companion\":";Claim(v.companion);out<<'}';}
        out<<",\"native\":";if(!o->native)out<<"null";else{const auto& n=*o->native;out<<"{\"readNs\":"<<o->nativeReadNs
            <<",\"countsFreshnessAvailable\":false,\"identity\":";Identity(n.native);out<<",\"phase\":"<<unsigned(n.phase)<<",\"failure\":"<<unsigned(n.failure)
            <<",\"cycle\":"<<n.cycle<<",\"shot\":"<<n.shot<<",\"blocksFire\":"<<n.blocksFire<<",\"heldSuspended\":"<<n.heldSuspended<<",\"counts\":";Counts(n);
            out<<",\"held\":";if(n.held)Cycle(*n.held);else out<<"null";out<<",\"ready\":";if(n.ready)ReadyEvent(*n.ready);else out<<"null";out<<'}';}out<<'}';
    }
};
} // namespace fvr::bc2::live_magazine_bolt
