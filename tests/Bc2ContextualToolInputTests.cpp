#include "Bc2ContextualToolInput.h"
#include <bit>
#include <cstdio>
#include <cstring>
#include <type_traits>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(false)
static_assert(!std::is_base_of_v<HolsterSuppressionReceipt,ToolInputReceipt>);
static_assert(!std::is_convertible_v<ToolInputReceipt,HolsterSuppressionReceipt>);
namespace {
struct Fixture {
 std::array<std::byte,InputBytes> cache{};ToolInputRequest request{};
 std::int64_t now=1000000000;unsigned calls=0,rejectAt=0;bool sameOwner=true;
 Fixture(){
  request.scope={{0x10000,0x11000,0x12000,0x13000,1,2,3},
   {{(std::uint64_t(0x12000)<<32)|0x11000,1,6,3},7,now,now+100000000,now,true,{true,true},{false,false}},11,13,0x17000};
  request.operation=17;request.serial=19;request.kind=ToolInputKind::Equip;
  request.from={0x13000,0x14000,0,0,0};request.target={0x15000,0x16000,0,12,4};
  request.custody={23,request.scope.input.owner,InteractionHand::Right,HandClaimKind::BodyInventory,{0x15000,2},{29,1},0};
  Put(8+4*7,std::bit_cast<unsigned>(.5f));Put(8+4*8,std::bit_cast<unsigned>(.6f));
  Put(0x98,(1u<<12)|(1u<<14)|(1u<<29)|(1u<<18));Put(0x9c,0x7f);
 }
 void Put(std::size_t at,unsigned x){std::memcpy(cache.data()+at,&x,4);}
 unsigned Get(std::size_t at)const{unsigned x;std::memcpy(&x,cache.data()+at,4);return x;}
 ToolInputValidation Validation(){return {this,[](void* c,const ToolInputRequest& r)noexcept{
  auto& f=*static_cast<Fixture*>(c);++f.calls;
  return f.sameOwner&&(!f.rejectAt||f.calls!=f.rejectAt)&&f.now>=r.scope.input.observedNs&&f.now<r.scope.input.deadlineNs;
 }};}
 void Controls(){request.kind=ToolInputKind::Controls;request.scope.owner.weapon=request.target.weapon;
  request.from=request.target;request.custody.kind=HandClaimKind::GunHold;request.zoom=request.fire=true;}
};
int MasksAndSeparateReceipt(){
 Fixture f;const auto before=f.cache;ContextualToolInputOverride w;
 CHECK(w.Apply(f.cache,f.request,f.Validation()));CHECK(f.Get(8+4*7)==0&&f.Get(8+4*8)==0);
 CHECK(f.Get(0x98)==(1u<<18)&&f.Get(0x9c)==((1u<<2)|1u));
 const auto receipt=w.Commit();CHECK(receipt&&receipt->request.operation==17&&receipt->request.serial==19);
 CHECK(w.Restore()&&f.cache!=before);CHECK(f.calls==3);
 Fixture control;control.Controls();ContextualToolInputOverride cw;
 CHECK(cw.Apply(control.cache,control.request,control.Validation()));CHECK(cw.Commit());
 CHECK(control.Get(0x98)==((1u<<18)|(1u<<14))&&control.Get(0x9c)==1u);
 CHECK(std::bit_cast<float>(control.Get(8+4*8))==1.f);
 Fixture ret;ret.request.kind=ToolInputKind::Return;std::swap(ret.request.from,ret.request.target);
 ret.request.scope.owner.weapon=ret.request.from.weapon;ret.request.custody={};ContextualToolInputOverride rw;
 CHECK(rw.Apply(ret.cache,ret.request,ret.Validation()));CHECK(rw.Commit());CHECK(std::bit_cast<float>(ret.Get(8+4*7))==1.f);
 CHECK(ret.Get(8+4*8)==0&&ret.Get(0x98)==(1u<<18)&&ret.Get(0x9c)==1u);return 0;
}
int RecheckAndClockBoundary(){
 for(unsigned rejection=1;rejection<=3;++rejection){Fixture f;const auto before=f.cache;f.rejectAt=rejection;ContextualToolInputOverride w;
  const bool applied=w.Apply(f.cache,f.request,f.Validation());CHECK(applied==(rejection==3));if(applied)CHECK(!w.Commit());CHECK(f.cache==before);}
 Fixture f;const auto before=f.cache;ContextualToolInputOverride w;CHECK(w.Apply(f.cache,f.request,f.Validation()));
 f.now=f.request.scope.input.deadlineNs;CHECK(!w.Commit());CHECK(f.cache==before);CHECK(f.request.scope.input.deadlineNs==1100000000);
 Fixture o;const auto original=o.cache;ContextualToolInputOverride ow;CHECK(ow.Apply(o.cache,o.request,o.Validation()));
 o.sameOwner=false;CHECK(!ow.Commit());CHECK(o.cache==original);return 0;
}
int ExactRestoration(){
 Fixture f;const auto before=f.cache;ContextualToolInputOverride w;CHECK(w.Apply(f.cache,f.request,f.Validation()));
 f.Put(8+4*8,std::bit_cast<unsigned>(.25f));f.Put(0x98,f.Get(0x98)|(1u<<12)|(1u<<5));
 f.Put(0x9c,f.Get(0x9c)|(1u<<7));CHECK(!w.Commit());
 CHECK(std::bit_cast<float>(f.Get(8+4*8))==.25f);CHECK(f.Get(8+4*7)==std::bit_cast<unsigned>(.5f));
 CHECK(f.Get(0x98)==((1u<<12)|(1u<<14)|(1u<<29)|(1u<<18)|(1u<<5)));
 CHECK(f.Get(0x9c)==0xff);CHECK(f.cache!=before);
 Fixture normal;const auto original=normal.cache;{ContextualToolInputOverride nw;CHECK(nw.Apply(normal.cache,normal.request,normal.Validation()));}
 CHECK(normal.cache==original);return 0;
}
int RejectInvalidOwnership(){
 for(unsigned variant=0;variant<13;++variant){Fixture f;const auto before=f.cache;
  if(variant==0)f.request.custody.kind=HandClaimKind::GunHold;
  if(variant==1)f.request.custody.item.id+=4;
  if(variant==2)f.request.custody.owner.space++;
  if(variant==3)f.request.target.category=10;
  if(variant==4)f.request.zoom=true;
  if(variant==5)f.request.scope.input.deadlineNs=f.request.scope.input.nowNs;
  if(variant==6)f.request.scope.input.focused=false;
  if(variant==7)f.request.scope.input.tracked[0]=false;
  if(variant==8)f.request.scope.owner.weapon+=4;
  if(variant==9)f.request.operation=0;
  if(variant==10)f.request.serial=0;
  if(variant==11)f.request.target.slot=5;
  if(variant==12)f.request.scope.input.deadlineNs+=1;
  ContextualToolInputOverride w;CHECK(!w.Apply(f.cache,f.request,f.Validation()));CHECK(f.cache==before&&f.calls==0);
 }return 0;
}
}
int main(){CHECK(MasksAndSeparateReceipt()==0);CHECK(RecheckAndClockBoundary()==0);CHECK(ExactRestoration()==0);CHECK(RejectInvalidOwnership()==0);
 std::puts("Four contextual-tool scoped-writer groups PASS; distinct receipts, route fields, original expiry, ownership and exact rollback.");}
