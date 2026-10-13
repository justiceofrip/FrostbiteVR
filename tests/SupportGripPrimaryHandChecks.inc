int CloseHandSupportChecks(){
 InputFrame in{};in.generation=in.spaceGeneration=1;in.predictedNs=1000000000;in.focused=in.headValid=true;
 for(auto& h:in.hands){h.active=Components;h.gripTracked=h.aimTracked=true;}
 in.hands[1].grip.position={.1f,-.3f,-.3f};in.hands[0].grip.position={.08f,-.31f,-.32f};
 const SupportGripOwner owner{1,2,3};SupportGripContact contact{true,0,{},SupportAimMode::PrimaryHand};SupportGrip grip;
 const auto tick=[&](float squeeze,bool busy=false){++in.generation;in.predictedNs+=11000000;in.hands[0].squeeze=squeeze;return grip.Update(owner,in,contact,false,busy);};
 CHECK(!tick(1).holding); // Spawn with a held squeeze must not grab.
 CHECK(!tick(0).holding);const auto held=tick(1);CHECK(held.holding&&held.engaged&&held.token);
 CHECK(held.correctionRadians==0&&!std::memcmp(&held.input,&in,sizeof(in)));
 // Nearly coincident hands and arbitrary primary rotation cannot choose an
 // unstable axis, snap the gun, or force a separation-based release.
 in.hands[0].grip.position=in.hands[1].grip.position;
 in.hands[1].grip.orientation=in.hands[1].aim.orientation={0,.6f,0,.8f};
 auto next=tick(1);CHECK(next.holding&&next.token==held.token&&next.correctionRadians==0);
 CHECK(!std::memcmp(&next.input,&in,sizeof(in)));
 next=grip.Update(owner,in,contact);CHECK(next.holding&&!next.engaged&&next.token==held.token);
 CHECK(tick(0).released);CHECK(!tick(1,true).holding);CHECK(!tick(1).holding);
 CHECK(!tick(0,true).holding);CHECK(tick(1).holding);
 contact.distanceMeters=.29f;CHECK(tick(1).released);contact.distanceMeters=0;CHECK(!tick(1).holding);
 CHECK(!tick(0).holding);CHECK(tick(1).holding);
 contact.aimMode=SupportAimMode::TwoPoint;CHECK(tick(1).released);CHECK(!tick(1).holding);
 contact.aimMode=SupportAimMode::PrimaryHand;CHECK(!tick(0).holding);CHECK(tick(1).holding);
 in.hands[1].gripTracked=false;CHECK(tick(1).released);in.hands[1].gripTracked=true;CHECK(!tick(1).holding);
 CHECK(!tick(0).holding);CHECK(tick(1).holding);++in.spaceGeneration;CHECK(!tick(1).holding);
 CHECK(!tick(0).holding);CHECK(tick(1).holding);in.predictedNs+=300000000;CHECK(!tick(1).holding);
 CHECK(!tick(0).holding);CHECK(tick(1).holding);contact.aimMode=static_cast<SupportAimMode>(255);CHECK(tick(1).released);
 return 0;
}
