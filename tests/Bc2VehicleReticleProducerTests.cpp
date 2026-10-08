#include "Bc2VehicleReticleProducer.h"
#include "Test.h"
#include <unordered_map>
#include <cstring>
#include <iostream>
#include <limits>
#include <fstream>
#include "fvr/engine/PeImage.h"
using namespace fvr;
math::Matrix4 Pose(float x=0,float y=0,float z=0){math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;m.values[3]={x,y,z,1};return m;}
struct Memory {
 std::unordered_map<unsigned,std::byte> bytes;std::unordered_map<unsigned,std::string> types;unsigned mutationAt=0,mutationValue=0,readCalls=0,mutateCall=0;
 void Put(unsigned at,const void* p,std::size_t n){for(std::size_t i=0;i<n;++i)bytes[at+unsigned(i)]=static_cast<const std::byte*>(p)[i];}
 void Word(unsigned at,unsigned v){Put(at,&v,4);}void Float(unsigned at,float v){Put(at,&v,4);}void Byte(unsigned at,unsigned char v){Put(at,&v,1);}
 bc2::VehicleRouteMemory Api(){return {this,[](void* p,unsigned at,void* out,std::size_t n){auto& m=*static_cast<Memory*>(p);++m.readCalls;
   if(m.mutateCall&&m.readCalls==m.mutateCall)m.Word(m.mutationAt,m.mutationValue);
   for(std::size_t i=0;i<n;++i){auto v=m.bytes.find(at+unsigned(i));if(v==m.bytes.end())return false;static_cast<std::byte*>(out)[i]=v->second;}return true;},
   [](void* p,unsigned at,const char* name){auto& m=*static_cast<Memory*>(p);auto i=m.types.find(at);return i!=m.types.end()&&i->second==name;}};}
};
struct Fixture {
 static constexpr unsigned image=0x400000,weapon=0x100000,table=0x200000,component=0x300000,effects=0x500000,firing=0x600000,primary=0x700000,shot=primary+0xa0;
 Memory m;bc2::VehicleReticleSourceBinding binding{image,true};bc2::BoatAimSnapshot aim{};bc2::VehicleRouteSnapshot route{};
 bc2::VehicleReticleProducerContext input{};graphics::PairTicket ticket{};bc2::VehicleReticleNativeProducer producer;
 Fixture(){
   aim.owner={0x11000,2,3,0x12000,0x13000,0x14000,0x15000};aim.player=0x16000;aim.nativeWeapon=weapon;aim.weaponComponent=component;
   route.identity=aim.owner;route.player=aim.player;route.fingerprint=7;route.slot=0;
   input.input={{aim.owner.actor,aim.owner.actorGeneration,aim.owner.controlled,aim.owner.entry},4,true,true,1000000000,1100000000};
   input.inputSequence=8;input.scene=9;input.weaponGeneration=10;input.focused=input.playing=true;
   ticket.resourceEpoch=11;ticket.sequence=12;ticket.frameId=13;ticket.spaceGeneration=4;ticket.trackingGeneration=8;ticket.predictedNs=1000000001;ticket.session[0]=1;
   m.Word(component+0xd0,weapon);m.Word(weapon,table);m.Word(table+8,image+0x3d4c10);m.Word(table+12,image+0x3bf840);m.Word(table+16,image+0x3bf860);
   m.Word(weapon+0xb0,0);m.Word(weapon+0x134,effects);m.Word(weapon+12,firing);m.Word(effects+16,firing);m.Word(firing+64,primary);
   m.Word(effects+0x174,0);m.Byte(firing+0x46,0);
   m.types[firing]="WeaponFiringData";m.types[primary]="FiringFunctionData"; // no speculative effects RTTI
   auto base=Pose(1,2,3),local=Pose(4,5,6);m.Put(effects+0x30,&base,64);m.Put(effects+0x70,&local,64);
   std::array<std::byte,80> s{};m.Put(shot,s.data(),s.size());m.Float(shot+8,600);m.Word(shot+0x34,1);
   m.Float(shot+0x20,.2f);m.Float(shot+0x24,.3f);m.Float(shot+0x28,.4f);
 }
 auto Ray(){return bc2::ReadPblDriverGunRayObservation(m.Api(),binding,aim);}
 auto Observe(std::int64_t now=1000000000){return producer.Observe(m.Api(),binding,aim,route,input,now);}
};
int FullCodeProof(const char* imagePath){
 if(!imagePath){std::cout<<"Disk-only code verification skipped: pass installed-exe path to check it.\n";return 0;}
 struct Code{unsigned rva,size;std::uint64_t fingerprint;const char* name;};const Code proofs[]{
 #include "Bc2VehicleReticleSourceProof.inc"
 };
 std::ifstream file(imagePath,std::ios::binary|std::ios::ate);CHECK(file);const auto size=file.tellg();CHECK(size>0&&size<100000000);
 std::vector<std::byte> data(static_cast<std::size_t>(size));file.seekg(0);file.read(reinterpret_cast<char*>(data.data()),static_cast<std::streamsize>(data.size()));CHECK(file);
 auto pe=engine::InspectPe(data);CHECK(pe.valid);Memory m;
 for(const auto& p:proofs){bool found=false;for(const auto& section:pe.image.sections){
    if(p.rva<section.rva)continue;const auto delta=std::uint64_t(p.rva)-section.rva,offset=std::uint64_t(section.rawOffset)+delta;
    if(delta<=section.rawSize&&p.size<=section.rawSize-delta&&offset<=data.size()&&p.size<=data.size()-offset){m.Put(Fixture::image+p.rva,data.data()+static_cast<std::size_t>(offset),p.size);found=true;break;}}
    CHECK(found);}
 CHECK(bc2::VerifyVehicleReticleSourceCode(m.Api(),Fixture::image).verified);
 // Downstream projectile dispatch and direction constants are covered, not a
 // truncated prologue. Each independently altered span must reject verification.
 for(const auto& p:proofs){const auto at=Fixture::image+p.rva;m.bytes[at]^=std::byte{1};CHECK(!bc2::VerifyVehicleReticleSourceCode(m.Api(),Fixture::image).verified);m.bytes[at]^=std::byte{1};}
 return 0;
}
int NativeVelocityAndCanonicalDirection(){Fixture f;const auto bytes=f.m.bytes;auto r=f.Ray();CHECK(r&&r->effects==Fixture::effects&&r->primaryFire==Fixture::primary);
 CHECK(Near(r->origin.x,5.2f)&&Near(r->origin.y,7.3f)&&Near(r->origin.z,-9.4f)&&Near(r->direction.z,-1));CHECK(bytes==f.m.bytes);
 f.m.Float(Fixture::shot+0x18,1);CHECK(f.Ray());f.m.Float(Fixture::shot+8,-600);r=f.Ray();CHECK(r&&Near(r->direction.z,1));
 // MuzzleExplosion is not custom aim and does not select a different projectile basis.
 f.m.Word(Fixture::shot+0x40,0x81000);CHECK(f.Ray());return 0;
}
int UnsupportedShotBranches(){for(unsigned i=0;i<13;++i){Fixture f;
 if(i<3)f.m.Byte(Fixture::shot+0x48+i,1);if(i==3)f.m.Float(Fixture::shot+0x3c,.5f);
 if(i==4)f.m.Float(Fixture::shot+0x10,.1f);if(i==5)f.m.Float(Fixture::shot+0x18,-1);if(i==6)f.m.Float(Fixture::shot+8,0);
 if(i==7)f.m.Float(Fixture::shot+0x18,std::numeric_limits<float>::quiet_NaN());if(i==8)f.m.Word(Fixture::shot+0x34,0);
 if(i==9)f.m.Word(Fixture::effects+0x174,0x82000);if(i==10)f.m.Byte(Fixture::firing+0x46,1);if(i==11)f.m.bytes.erase(Fixture::effects+0x174);
 if(i==12)f.m.Float(Fixture::shot+0x18,std::numeric_limits<float>::max());
 CHECK(!f.Ray());}return 0;}
int ChangedSourceOrMissingField(){for(unsigned i=0;i<6;++i){Fixture f;
 if(i==0)f.m.bytes.erase(Fixture::weapon+0xb0);if(i==1)f.m.Word(Fixture::weapon+0xb0,Fixture::weapon+0xf4);
 if(i==2)f.m.Word(Fixture::effects+16,Fixture::firing+4);if(i==3)f.m.types[Fixture::firing]="Wrong";
 if(i==4){f.m.mutateCall=25;f.m.mutationAt=Fixture::component+0xd0;f.m.mutationValue=Fixture::weapon+4;}
 if(i==5)f.binding.verified=false;CHECK(!f.Ray());}return 0;}
int ProducerUsesMeasuredRayAndStaysDisabled(){Fixture f;auto s=f.Observe();CHECK(s&&s->visual.proof==graphics::VehicleReticleProof::Unproven);
 CHECK(Near(s->visual.sightWorld.values[2][2],-1)&&graphics::VehicleReticleBasis(s->visual.sightWorld));
 CHECK(f.producer.Begin(f.ticket,1000000001));CHECK(f.producer.Eye(f.m.Api(),f.binding,0,f.aim,f.route,f.input,f.ticket,1000000002));CHECK(f.producer.Eye(f.m.Api(),f.binding,1,f.aim,f.route,f.input,f.ticket,1000000003));
 graphics::BodyPropFrame frame;const auto old=frame;CHECK(!f.producer.Append(frame,f.ticket,1000000004));CHECK(std::memcmp(&frame,&old,sizeof(frame))==0);
 CHECK(f.producer.Complete(f.m.Api(),f.binding,f.aim,f.route,f.input,f.ticket,1000000004));CHECK(!bc2::VehicleReticleNativeShotSourceAdmitted);return 0;}
int PairFrozenAcrossFreshObservation(){Fixture f;auto s=f.Observe();CHECK(s&&f.producer.Begin(f.ticket,1000000001));auto left=f.producer.Eye(f.m.Api(),f.binding,0,f.aim,f.route,f.input,f.ticket,1000000002);CHECK(left);
 auto newBase=Pose(20,2,3);f.m.Put(Fixture::effects+0x30,&newBase,64);++f.input.inputSequence;f.input.input.observedNs+=10000000;f.input.input.deadlineNs+=10000000;
 auto latest=f.Observe(1010000000);CHECK(latest&&!Near(latest->visual.sightWorld.values[3][0],left->visual.sightWorld.values[3][0]));
 auto right=f.producer.Eye(f.m.Api(),f.binding,1,f.aim,f.route,f.input,f.ticket,1010000001);CHECK(right&&std::memcmp(&left->visual.sightWorld,&right->visual.sightWorld,sizeof(math::Matrix4))==0);
 CHECK(f.producer.Complete(f.m.Api(),f.binding,f.aim,f.route,f.input,f.ticket,1010000002));return 0;}
int PairCancelsOnIdentityOrTrackingLoss(){for(unsigned i=0;i<10;++i){Fixture f;CHECK(f.Observe()&&f.producer.Begin(f.ticket,1000000001));CHECK(f.producer.Eye(f.m.Api(),f.binding,0,f.aim,f.route,f.input,f.ticket,1000000002));
 if(i==0)++f.aim.owner.seatGeneration;if(i==1)++f.aim.nativeWeapon;if(i==2)++f.input.scene;if(i==3)++f.input.weaponGeneration;if(i==4)++f.input.input.space;
 if(i==5)f.input.focused=false;if(i==6)f.input.input.live=false;if(i==7)++f.ticket.resourceEpoch;if(i==8)++f.ticket.session[0];if(i==9)++f.route.fingerprint;
 CHECK(!f.producer.Eye(f.m.Api(),f.binding,1,f.aim,f.route,f.input,f.ticket,1000000003));CHECK(!f.producer.Complete(f.m.Api(),f.binding,f.aim,f.route,f.input,f.ticket,1000000004));}return 0;}
int OriginalDeadlineCannotRenew(){Fixture f;CHECK(f.Observe()&&f.producer.Begin(f.ticket,1000000001));CHECK(f.producer.Eye(f.m.Api(),f.binding,0,f.aim,f.route,f.input,f.ticket,1000000002));
 f.input.input.observedNs=1090000000;f.input.input.deadlineNs=1190000000;++f.input.inputSequence;CHECK(f.Observe(1090000000));
 CHECK(!f.producer.Eye(f.m.Api(),f.binding,1,f.aim,f.route,f.input,f.ticket,1100000000));return 0;}
int DuplicatedEyeOrTicketCannotComplete(){Fixture f;CHECK(f.Observe()&&f.producer.Begin(f.ticket,1000000001));CHECK(f.producer.Eye(f.m.Api(),f.binding,0,f.aim,f.route,f.input,f.ticket,1000000002));
 CHECK(!f.producer.Eye(f.m.Api(),f.binding,0,f.aim,f.route,f.input,f.ticket,1000000003));CHECK(!f.producer.Complete(f.m.Api(),f.binding,f.aim,f.route,f.input,f.ticket,1000000004));CHECK(!f.producer.Begin(f.ticket,1000000005));return 0;}
int RegressionAndResetAdvanceEpoch(){Fixture f;auto a=f.Observe();CHECK(a);f.producer.Reset();auto b=f.Observe();CHECK(b&&b->visual.presentationEpoch>a->visual.presentationEpoch);
 --f.input.inputSequence;CHECK(!f.Observe());return 0;}
int WrongSeatOrContextRejectsObservation(){for(unsigned i=0;i<7;++i){Fixture f;
 if(i==0)f.route.slot=1;if(i==1)++f.route.identity.entry;if(i==2)++f.input.input.owner.generation;if(i==3)f.input.playing=false;if(i==4)f.route.fingerprint=0;
 if(i==5)f.input.inputSequence=0;if(i==6)f.input.input.observedNs=1100000000;CHECK(!f.Observe());}return 0;}
int ClockOverflowAndInvalidTicket(){Fixture f;f.input.input.observedNs=INT64_MAX-50000000;f.input.input.deadlineNs=INT64_MAX;CHECK(!f.Observe(INT64_MAX-40000000));
 Fixture g;CHECK(g.Observe());g.ticket.session={};CHECK(!g.producer.Begin(g.ticket,1000000001));return 0;}
int ActualSourceRereadBeforeSecondEyeAndCompletion(){for(unsigned i=0;i<2;++i){Fixture f;CHECK(f.Observe()&&f.producer.Begin(f.ticket,1000000001));CHECK(f.producer.Eye(f.m.Api(),f.binding,0,f.aim,f.route,f.input,f.ticket,1000000002));
 if(i==1)CHECK(f.producer.Eye(f.m.Api(),f.binding,1,f.aim,f.route,f.input,f.ticket,1000000003));
 f.m.Word(Fixture::weapon+0x134,0x900000); // same seat/nativeWeapon, replaced or lost firing source
 if(i==0)CHECK(!f.producer.Eye(f.m.Api(),f.binding,1,f.aim,f.route,f.input,f.ticket,1000000004));
 CHECK(!f.producer.Complete(f.m.Api(),f.binding,f.aim,f.route,f.input,f.ticket,1000000005));}return 0;}
int main(int argc,char** argv){if(FullCodeProof(argc>1?argv[1]:nullptr)||NativeVelocityAndCanonicalDirection()||UnsupportedShotBranches()||ChangedSourceOrMissingField()||ProducerUsesMeasuredRayAndStaysDisabled()||PairFrozenAcrossFreshObservation()||PairCancelsOnIdentityOrTrackingLoss()||OriginalDeadlineCannotRenew()||DuplicatedEyeOrTicketCannotComplete()||RegressionAndResetAdvanceEpoch()||WrongSeatOrContextRejectsObservation()||ClockOverflowAndInvalidTicket()||ActualSourceRereadBeforeSecondEyeAndCompletion())return 1;
 std::cout<<(argc>1?"13":"12")<<" native-source/producer CPU groups passed; synthetic object memory, production disabled.\n";}

