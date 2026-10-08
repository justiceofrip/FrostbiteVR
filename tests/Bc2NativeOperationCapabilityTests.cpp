#include "Bc2NativeOperationCapability.h"
#include "Test.h"
using namespace fvr::bc2;
int main(){
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
 std::puts("Shared native operation receipt validation passed; production admission remains off.");return 0;
}
