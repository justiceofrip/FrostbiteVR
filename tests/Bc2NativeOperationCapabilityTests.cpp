#include "Bc2NativeOperationCapability.h"
#include "Test.h"
using namespace fvr::bc2;
namespace {
constexpr std::string_view Digest="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr std::string_view OtherDigest="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
constexpr std::string_view InvalidDigest="AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
constexpr NativeOperationBinding Key{1,2,0x400000,0x10000,{1,2,3,4},{4,4,4,4},{11,12,13,14}};
constexpr NativeOperationCapability Receipt{Key,1,NativeOperationAlgorithm,Digest,Digest,Digest,Digest,true,true,true};
consteval auto ReceiptCase(unsigned fault){
    std::array<NativeOperationCapability,2> rows{Receipt,{}};
    if(fault==1)rows[0].reviewed=false;
    if(fault==2)rows[0].hideShowVerified=false;
    if(fault==3)rows[0].inputSuppressionVerified=false;
    if(fault==4)rows[0].executableSha256=InvalidDigest;
    if(fault==5)rows[0].hideShowReceiptSha256=Digest.substr(1);
    if(fault==6)rows[0].inputSuppressionReceiptSha256={};
    if(fault==7)rows[0].sourceSha256=OtherDigest;
    if(fault==8)rows[0].algorithm="other";
    if(fault>=9){
        rows[1]=Receipt;
        if(fault>=10)rows[1].reviewed=false;
        if(fault>=11&&fault<=14){rows[1].executableSha256="bad";
            rows[1].hideShowReceiptSha256={};rows[1].inputSuppressionReceiptSha256=InvalidDigest;}
        if(fault==11)rows[1].binding.codeFingerprint[0]++;
        if(fault==12)rows[1].rigFingerprint++;
        if(fault==13)rows[1].sourceSha256=OtherDigest;
        if(fault==14)rows[1].algorithm="other";
        if(fault==15){rows[0]=rows[1];rows[1]=Receipt;}
    }
    return rows;
}
template<unsigned Fault,bool Expected> bool EquivalentReceiptCase(){
    constexpr auto rows=ReceiptCase(Fault);
    constexpr NativeOperationAdmissionTable table{rows,Digest};
    static_assert(table.Admitted(Key,1)==Expected);
    return table.Admitted(Key,1)==NativeOperationAdmitted(Key,1,rows,Digest)&&
        table.Admitted(Key,1)==Expected;
}
int CompiledAdmission(){
    CHECK((EquivalentReceiptCase<0,true>()));
    CHECK((EquivalentReceiptCase<1,false>()));
    CHECK((EquivalentReceiptCase<2,false>()));
    CHECK((EquivalentReceiptCase<3,false>()));
    CHECK((EquivalentReceiptCase<4,false>()));
    CHECK((EquivalentReceiptCase<5,false>()));
    CHECK((EquivalentReceiptCase<6,false>()));
    CHECK((EquivalentReceiptCase<7,false>()));
    CHECK((EquivalentReceiptCase<8,false>()));
    CHECK((EquivalentReceiptCase<9,false>()));
    CHECK((EquivalentReceiptCase<10,false>()));
    CHECK((EquivalentReceiptCase<11,true>())); // Invalid credentials for another binding are irrelevant.
    CHECK((EquivalentReceiptCase<12,true>())); // Another rig is also a separate admission key.
    CHECK((EquivalentReceiptCase<13,true>()));
    CHECK((EquivalentReceiptCase<14,true>()));
    CHECK((EquivalentReceiptCase<15,false>())); // Contradictory first receipt also rejects.

    constexpr auto rows=ReceiptCase(0);
    constexpr NativeOperationAdmissionTable table{rows,Digest};
    for(unsigned field=0;field<16;++field){
        auto live=Key;
        if(field==0)++live.executableFingerprint;
        else if(field==1)++live.executableBytes;
        else if(field==2)++live.imageBase;
        else if(field==3)++live.imageBytes;
        else if(field<8)++live.codeRva[field-4];
        else if(field<12)++live.codeBytes[field-8];
        else ++live.codeFingerprint[field-12];
        CHECK(!table.Admitted(live,1)&&!NativeOperationAdmitted(live,1,rows,Digest));
        CHECK(table.Admitted(Key,1)); // A prior caller cannot alter the immutable table.
    }
    CHECK(!table.Admitted(Key,0)&&!table.Admitted(Key,2));
    auto incomplete=Key;incomplete.codeBytes[0]=0;
    CHECK(!table.Admitted(incomplete,1)&&!NativeOperationAdmitted(incomplete,1,rows,Digest));
    incomplete=Key;incomplete.codeRva[3]=UINT32_MAX;
    CHECK(!table.Admitted(incomplete,1)&&!NativeOperationAdmitted(incomplete,1,rows,Digest));

    constexpr auto badBuildRows=[] {auto r=ReceiptCase(0);r[0].sourceSha256=InvalidDigest;return r;}();
    constexpr NativeOperationAdmissionTable badBuild{badBuildRows,InvalidDigest};
    CHECK(!badBuild.Admitted(Key,1)&&!NativeOperationAdmitted(Key,1,badBuildRows,InvalidDigest));
    constexpr auto emptyBuildRows=[] {auto r=ReceiptCase(0);r[0].sourceSha256={};return r;}();
    constexpr NativeOperationAdmissionTable emptyBuild{emptyBuildRows,std::string_view{}};
    CHECK(!emptyBuild.Admitted(Key,1)&&!NativeOperationAdmitted(Key,1,emptyBuildRows,{}));
    constexpr std::array<NativeOperationCapability,0> none{};
    constexpr NativeOperationAdmissionTable empty{none,Digest};
    CHECK(!empty.Admitted(Key,1)&&!NativeOperationAdmitted(Key,1,none,Digest));

    constexpr auto distinct=[] {auto r=ReceiptCase(0);r[1]=Receipt;r[1].rigFingerprint=2;
        r[1].binding.codeFingerprint[3]++;return r;}();
    constexpr NativeOperationAdmissionTable multiple{distinct,Digest};
    CHECK(multiple.Admitted(Key,1)&&multiple.Admitted(distinct[1].binding,2));
    CHECK(!multiple.Admitted(Key,2)&&!multiple.Admitted(distinct[1].binding,1));
    CHECK(NativeOperationAdmitted(distinct[1].binding,2,distinct,Digest));
    CHECK(!CompiledNativeOperationAdmitted(Key,1)); // This target has no compiled production receipt.
    return 0;
}
}
int main(){
 if(CompiledAdmission())return 1;
 constexpr std::string_view digest="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
 NativeOperationBinding key{1,2,0x400000,0x10000,{1,2,3,4},{4,4,4,4},{11,12,13,14}};
 NativeOperationCapability receipt{key,1,NativeOperationAlgorithm,digest,digest,digest,digest,true,true,true};
 auto records=std::array{receipt};
 CHECK(NativeOperationAdmitted(key,1,records,digest));
 CHECK(!NativeOperationAdmitted(key,1,records)); // production build defaults OFF
 auto wrong=key;wrong.executableFingerprint++;CHECK(!NativeOperationAdmitted(wrong,1,records,digest));
 wrong=key;wrong.codeFingerprint[2]++;CHECK(!NativeOperationAdmitted(wrong,1,records,digest));
 CHECK(!NativeOperationAdmitted(key,2,records,digest));
 CHECK(!NativeOperationAdmitted(key,1,records,"bad"));
 records[0].reviewed=false;CHECK(!NativeOperationAdmitted(key,1,records,digest));records[0]=receipt;
 records[0].hideShowVerified=false;CHECK(!NativeOperationAdmitted(key,1,records,digest));records[0]=receipt;
 records[0].inputSuppressionVerified=false;CHECK(!NativeOperationAdmitted(key,1,records,digest));records[0]=receipt;
 records[0].algorithm="other";CHECK(!NativeOperationAdmitted(key,1,records,digest));records[0]=receipt;
 records[0].executableSha256="";CHECK(!NativeOperationAdmitted(key,1,records,digest));records[0]=receipt;
 records[0].hideShowReceiptSha256="";CHECK(!NativeOperationAdmitted(key,1,records,digest));records[0]=receipt;
 records[0].inputSuppressionReceiptSha256="";CHECK(!NativeOperationAdmitted(key,1,records,digest));records[0]=receipt;
 auto duplicates=std::array{receipt,receipt};CHECK(!NativeOperationAdmitted(key,1,duplicates,digest));
 std::puts("Dynamic/compiled receipt admission agree for exact live bindings, malformed receipts and duplicates; production admission remains off.");return 0;
}
