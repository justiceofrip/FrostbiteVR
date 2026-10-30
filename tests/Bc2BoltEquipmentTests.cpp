#include "Bc2BoltEquipment.h"
#include "Bc2BoltActionNativeProfile.h"
#include "Test.h"
#include <set>
using namespace fvr::bc2;
int main(){
    const auto rows=EnrolledBoltEquipment();CHECK(rows.size()==17);
    std::set<std::uint64_t> ids;std::set<std::string> configurations;
    for(const auto& row:rows){
        CHECK(BoltCalibrationValid(row)&&row.authoredReviewed&&row.handShape);
        CHECK(ids.insert(row.profile.id).second&&configurations.insert(row.configurationPath).second);
        CHECK(FindBoltEquipment(row.configurationPath,row.asset,row.mesh,row.rigFingerprint)==&row);
        CHECK(!FindBoltEquipment({},row.asset,row.mesh,row.rigFingerprint));
        CHECK(!FindBoltEquipment(row.configurationPath,row.asset,row.mesh,row.rigFingerprint+1));
        const auto native=std::find_if(Bc2CycleNativeProfiles.begin(),Bc2CycleNativeProfiles.end(),[&](const auto& p){
            return p.family==Bc2NativeCycleFamily::Bolt&&p.config->assetPath==row.configurationPath&&p.config->assetName==row.asset;
        });
        CHECK(native!=Bc2CycleNativeProfiles.end());
        SelectedMeshesSnapshot selected;selected.owner={0x10000,0x20000,0x30000,0x40000,1,2,3};
        selected.sequence=5;selected.observedNs=1000000000;selected.deadlineNs=1100000000;
        selected.stateCount=1;selected.soleConfiguredArray=0x50000;selected.states[0].count=1;selected.configurationPathVerified=true;
        const auto text=[](auto& dest,const auto& source){std::copy(source.begin(),source.end(),dest.begin());};
        text(selected.configurationPath,row.configurationPath);text(selected.weaponName,row.asset);text(selected.states[0].meshes[0].assetPath,row.mesh);
        CHECK(BoltEquipmentSelected(selected,selected.owner,row,1000000001));
        selected.states[0].count=2;selected.states[0].meshes[1]=selected.states[0].meshes[0];
        CHECK(!BoltEquipmentSelected(selected,selected.owner,row,1000000001));
        selected.states[0].count=1;selected.configurationPath[0]='?';
        CHECK(!BoltEquipmentSelected(selected,selected.owner,row,1000000001));
    }
    return 0;
}
