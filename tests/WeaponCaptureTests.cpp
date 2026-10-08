#include "Test.h"
#include "Bc2WeaponCapture.h"
#include <sstream>
#include <limits>
using namespace fvr;
math::Matrix4 Identity(){math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;return m;}
int NativeHandCapture(){
 bc2::WeaponCaptureSample s;s.assetName="reload";s.skeleton="rig:fingers";s.actor=11;s.weapon=22;
 s.ownerGeneration=3;s.space=4;s.generation=5;s.capturedMs=600;s.predictedNs=700;s.unitsPerMeter=2;
 s.nativeWeapon=s.nativeLeft=s.nativeRight=Identity();s.nativeWeapon.values[3][0]=10;s.nativeLeft.values[3][0]=11;
 s.rootName="weapon";s.leftName="LeftHand";s.rightName="RightHand";
 s.nativeWeaponBones={{"weapon","outside",s.nativeWeapon,false}};s.weaponBonesComplete=true;
 auto finger=s.nativeLeft;finger.values[3][1]=.2f;
 s.nativeLeftHandBones={{"LeftHand","LeftForeArmRoll",s.nativeLeft,false},{"LeftIndex1","LeftHand",finger,false}};
 s.leftHandBonesCaptured=s.leftHandBonesComplete=true;
 const auto legacyMatrices=s;
 for(auto& bone:s.nativeWeaponBones)bone.inverseBind=Identity();
 for(auto& bone:s.nativeLeftHandBones)bone.inverseBind=Identity();
 const auto good=s;
 bc2::WeaponCapture capture;CHECK(capture.Observe(s));
 // Stored fingers and weapon retain one shared sample identity and raw matrices;
 // later caller edits must not mutate the retained observation.
 s.nativeLeftHandBones[1].native.values[3][1]=.4f;++s.generation;s.capturedMs+=100;s.predictedNs+=100;
 CHECK(capture.Observe(s));auto rows=capture.Samples();CHECK(rows.size()==2);
 CHECK(rows[0].nativeLeftHandBones[1].native.values==finger.values&&rows[0].nativeLeft.values==good.nativeLeft.values);
 CHECK(rows[0].nativeWeaponBones[0].native.values==good.nativeWeapon.values&&rows[0].generation==5&&rows[0].predictedNs==700);
 CHECK(rows[0].nativeWeaponBones[0].inverseBind&&rows[0].nativeLeftHandBones[1].inverseBind&&rows[0].nativeLeftHandBones[1].inverseBind->values==Identity().values);
 CHECK(rows[0].actor==11&&rows[0].weapon==22&&rows[0].space==4&&rows[0].ownerGeneration==3&&rows[0].capturedMs==600&&rows[0].unitsPerMeter==2);
 CHECK(rows[1].nativeLeftHandBones[1].native.values[3][1]==.4f&&rows[1].generation==6&&rows[1].leftHandBonesCaptured&&rows[1].leftHandBonesComplete);
 std::ostringstream report;capture.Report(report);const auto text=report.str();
 CHECK(text.find("\"left_hand_bones_captured\":true")!=std::string::npos&&text.find("\"left_hand_bones_complete\":true")!=std::string::npos);
 CHECK(text.find("\"native_left_hand_bones\":[")!=std::string::npos&&text.find("\"left_hand_pose_source\":\"native_evaluated_before_vr\"")!=std::string::npos);
 // Optional legacy samples remain valid and explicitly lack finger evidence.
 auto absent=good;absent.nativeLeftHandBones.clear();absent.leftHandBonesCaptured=absent.leftHandBonesComplete=false;
 bc2::WeaponCapture legacy;CHECK(legacy.Observe(absent));CHECK(!legacy.Samples()[0].leftHandBonesCaptured);
 bc2::WeaponCapture withoutBind;CHECK(withoutBind.Observe(legacyMatrices));std::ostringstream oldReport;withoutBind.Report(oldReport);
 CHECK(oldReport.str().find("\"inverse_bind\"")==std::string::npos&&text.find("\"inverse_bind\":[")!=std::string::npos);
 const auto rejects=[&](bc2::WeaponCaptureSample invalid){bc2::WeaponCapture candidate;return !candidate.Observe(std::move(invalid))&&candidate.Samples().empty();};
 auto bad=good;bad.leftHandBonesCaptured=false;CHECK(rejects(bad));
 bad=absent;bad.leftHandBonesComplete=true;CHECK(rejects(bad));
 bad=good;bad.nativeLeftHandBones.clear();CHECK(rejects(bad));
 bad=good;bad.nativeLeftHandBones[0].native.values[3][0]+=.01f;CHECK(rejects(bad));
 bad=good;bad.nativeLeftHandBones.push_back(bad.nativeLeftHandBones.back());CHECK(rejects(bad));
 bad=good;bad.nativeLeftHandBones[1].native.values[0][0]=0;CHECK(rejects(bad));
 bad=good;bad.nativeLeftHandBones[1].native.values[0][0]=std::numeric_limits<float>::quiet_NaN();CHECK(rejects(bad));
 bad=good;bad.nativeLeftHandBones[0].hidden=true;CHECK(rejects(bad));
 bad=good;bad.nativeLeftHandBones[1].inverseBind->values[0][0]=0;CHECK(rejects(bad));
 bad=good;bad.nativeWeaponBones[0].inverseBind->values[0][0]=std::numeric_limits<float>::infinity();CHECK(rejects(bad));
 auto hidden=good;hidden.nativeLeftHandBones[1].hidden=true;
 for(unsigned n=0;n<3;++n)hidden.nativeLeftHandBones[1].native.values[n][n]=.0001f;
 bc2::WeaponCapture hiddenLeaf;CHECK(hiddenLeaf.Observe(hidden));
 CHECK(hiddenLeaf.Samples()[0].nativeLeftHandBones[1].hidden&&hiddenLeaf.Samples()[0].nativeLeftHandBones[1].native.values[0][0]==.0001f);
 bad=good;bad.nativeLeftHandBones[1].parentName="LeftIndex1";CHECK(rejects(bad));
 bad=good;bad.nativeLeftHandBones[1].parentName="missing";CHECK(rejects(bad));
 bad=good;bad.nativeLeftHandBones[0].parentName="LeftIndex1";CHECK(rejects(bad));
 bad=good;bad.nativeLeftHandBones[1].name=std::string(64,'x');CHECK(rejects(bad));
 // Overflow stays explicit/incomplete and cannot wrap a dropped-count counter.
 auto many=good;many.nativeLeftHandBones.resize(1);
 for(unsigned n=0;n<bc2::WeaponCapture::MaxLeftHandBones;++n)many.nativeLeftHandBones.push_back({"finger"+std::to_string(n),"LeftHand",finger,false});
 bc2::WeaponCapture bounded;CHECK(bounded.Observe(many));rows=bounded.Samples();
 CHECK(rows[0].nativeLeftHandBones.size()==bc2::WeaponCapture::MaxLeftHandBones&&rows[0].leftHandBonesDropped==1&&!rows[0].leftHandBonesComplete);
 many.leftHandBonesDropped=std::numeric_limits<unsigned>::max();CHECK(rejects(many));
 // Already incomplete data can name a missing ancestor, but never becomes a
 // complete skeleton merely because its retained count fits under the cap.
 auto partial=good;partial.nativeLeftHandBones[1].parentName="omitted";partial.leftHandBonesDropped=1;
 bc2::WeaponCapture incomplete;CHECK(incomplete.Observe(partial));CHECK(!incomplete.Samples()[0].leftHandBonesComplete);
 partial=good;partial.leftHandBonesComplete=false;partial.nativeLeftHandBones[1].native.values[0][0]=0;
 bc2::WeaponCapture rawSingular;CHECK(rawSingular.Observe(partial));CHECK(!rawSingular.Samples()[0].leftHandBonesComplete&&rawSingular.Samples()[0].nativeLeftHandBones[1].native.values[0][0]==0);
 return 0;
}
int SelectedLabelDoesNotBindSubmittedMesh(){
 bc2::WeaponCaptureSample s;s.assetName="shared_pointer";s.skeleton="shared_rig";s.actor=11;s.weapon=22;
 s.ownerGeneration=s.space=1;s.nativeWeapon=s.nativeLeft=s.nativeRight=Identity();
 s.provenance=bc2::WeaponCaptureProvenance{1,33,44,{11,12,13,14,15,16,17,18,19,20,false}};
 bc2::WeaponCapture c;const auto observe=[&](){++s.generation;s.capturedMs+=100;return c.Observe(s);};
 CHECK(observe());for(unsigned n=0;n<7;++n)CHECK(observe());CHECK(!c.Samples().back().attachmentPending);
 // Same address and asset name cannot conceal a new selected lifetime/data.
 ++s.provenance->equipmentGeneration;CHECK(observe());CHECK(c.Samples().back().captureEpisode==2&&c.Samples().back().attachmentPending);
 ++s.provenance->weaponData;CHECK(observe());CHECK(c.Samples().back().captureEpisode==3);
 ++s.provenance->persistence;CHECK(observe());CHECK(c.Samples().back().captureEpisode==4);
 ++s.provenance->rig.animation;CHECK(observe());CHECK(c.Samples().back().captureEpisode==5);
 ++s.provenance->rig.evaluatedMatrices;CHECK(observe());CHECK(c.Samples().back().captureEpisode==6);
 const auto old=s;s.provenance.reset();CHECK(observe());CHECK(c.Samples().back().captureEpisode==7&&!c.Samples().back().provenance);
 s.provenance=old.provenance;++s.provenance->rig.soldier;CHECK(!observe());
 s.provenance=old.provenance;s.provenance->equipmentGeneration=0;CHECK(!observe());
 std::ostringstream report;c.Report(report);const auto text=report.str();
 CHECK(text.find("\"schema_version\":2")!=std::string::npos);
 CHECK(text.find("\"capture_provenance\":null")!=std::string::npos);
 CHECK(text.find("\"equipment_generation\":2,\"weapon_data\":34,\"persistence\":45")!=std::string::npos);
 CHECK(text.find("\"pose_asset_binding_verified\":false")!=std::string::npos);
 CHECK(text.find("\"submitted_mesh_binding_verified\":true")==std::string::npos);
 return 0;
}
int main(){
 CHECK(SelectedLabelDoesNotBindSubmittedMesh()==0);
 CHECK(NativeHandCapture()==0);
 bc2::WeaponCapture capture;bc2::WeaponCaptureSample s;s.assetName="one";s.skeleton="rig";s.actor=s.weapon=1;s.ownerGeneration=s.space=1;s.nativeWeapon=s.nativeLeft=s.nativeRight=Identity();
 for(unsigned n=1;n<=118;++n){s.generation=n;s.capturedMs=n*100;CHECK(capture.Observe(s));}
 auto rows=capture.Samples();CHECK(rows.size()==96&&rows.front().generation==23&&rows.back().equipAgeMs==11700);
 s.assetName="two";s.weapon=2;s.generation=119;s.capturedMs=11900;CHECK(capture.Observe(s));
 // Going back to a previously observed weapon retains both item groups and
 // restarts transition age; reference-space and actor identities never merge.
 s.assetName="one";s.weapon=1;s.generation=120;s.capturedMs=12000;CHECK(capture.Observe(s));
 rows=capture.Samples();CHECK(rows.size()==97&&rows.back().equipAgeMs==0&&rows[rows.size()-2].assetName=="two");
 ++s.space;++s.generation;s.capturedMs+=100;CHECK(capture.Observe(s));
 ++s.ownerGeneration;++s.generation;s.capturedMs+=100;CHECK(capture.Observe(s));CHECK(capture.Samples().size()==99);
 const auto before=capture.Samples().size();CHECK(!capture.Observe(s));CHECK(capture.Samples().size()==before);
 s.nativeLeft.values[0][0]=std::numeric_limits<float>::quiet_NaN();++s.generation;s.capturedMs+=100;CHECK(!capture.Observe(s));
 std::ostringstream out;capture.Report(out);CHECK(out.str().find("\"asset_name\":\"two\"")!=std::string::npos&&out.str().find("\"invalid_samples\":1")!=std::string::npos);
 // A bounded collector preserves earlier item data rather than evicting it.
 bc2::WeaponCapture bounded;s.nativeLeft=Identity();s.space=1;s.ownerGeneration=1;
 for(unsigned n=1;n<=bc2::WeaponCapture::MaxGroups;++n){s.assetName="item"+std::to_string(n);s.weapon=n;s.generation=n;s.capturedMs=n*100;CHECK(bounded.Observe(s));}
 s.assetName="overflow";++s.weapon;++s.generation;s.capturedMs+=100;CHECK(!bounded.Observe(s));CHECK(bounded.Samples().size()==bc2::WeaponCapture::MaxGroups);
 // Late authored attachment changes cannot be called settled just because
 // equipment identity has existed for600ms. Generation rollback keeps capture
 // chronology, resets age, and cannot merge a new equipment episode.
 bc2::WeaponCapture delayed;s.assetName="late";s.nativeLeft=s.nativeRight=s.nativeWeapon=Identity();s.unitsPerMeter=1;
 s.generation=10;s.capturedMs=1000;CHECK(delayed.Observe(s));
 s.generation=11;s.capturedMs=1700;s.nativeWeapon.values[3][1]=.1f;CHECK(delayed.Observe(s));CHECK(delayed.Samples().back().attachmentPending);
 s.generation=12;s.capturedMs=1900;CHECK(delayed.Observe(s));CHECK(!delayed.Samples().back().attachmentPending);
 s.generation=1;s.capturedMs=2000;CHECK(delayed.Observe(s));rows=delayed.Samples();CHECK(rows.back().generation==1&&rows.back().captureSequence==4&&rows.back().attachmentPending&&rows.back().captureEpisode==2);
 s.unitsPerMeter=2;s.generation=2;s.capturedMs=2100;CHECK(delayed.Observe(s));CHECK(delayed.Samples().back().captureEpisode==3);

 // Native mechanism observations retain hidden geometry verbatim, bounded and
 // explicitly incomplete rather than silently dropping bones or emitting NaN.
 bc2::WeaponCapture bones;s.nativeWeaponBones.clear();s.weaponBonesComplete=true;s.weaponBonesDropped=0;
 for(unsigned n=0;n<bc2::WeaponCapture::MaxWeaponBones+1;++n)
     s.nativeWeaponBones.push_back({"part"+std::to_string(n),"root",Identity(),false});
 ++s.generation;s.capturedMs+=100;CHECK(bones.Observe(s));rows=bones.Samples();
 CHECK(rows.back().nativeWeaponBones.size()==bc2::WeaponCapture::MaxWeaponBones);
 CHECK(!rows.back().weaponBonesComplete&&rows.back().weaponBonesDropped==1);
 s.nativeWeaponBones={{"hidden","root",Identity(),true}};
 for(unsigned n=0;n<3;++n)s.nativeWeaponBones[0].native.values[n][n]=.0001f;
 ++s.generation;s.capturedMs+=100;CHECK(bones.Observe(s));
 CHECK(bones.Samples().back().nativeWeaponBones[0].native.values[0][0]==.0001f);
 std::ostringstream mechanisms;bones.Report(mechanisms);CHECK(mechanisms.str().find("\"hidden\":true")!=std::string::npos);
 s.nativeWeaponBones.push_back(s.nativeWeaponBones.front());++s.generation;s.capturedMs+=100;CHECK(!bones.Observe(s));
 s.nativeWeaponBones.resize(1);s.nativeWeaponBones[0].native.values[3][0]=std::numeric_limits<float>::quiet_NaN();
 CHECK(!bones.Observe(s));
 return 0;
}
