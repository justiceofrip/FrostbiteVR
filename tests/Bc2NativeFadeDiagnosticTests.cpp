#include "Bc2NativeFadeDiagnostic.h"
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>
using namespace fvr::bc2::fadeDiagnostic;
static std::array<unsigned char,0x10000> memory{};
static unsigned invalidReads=0;
static bool ReadMemory(std::uintptr_t p,void* target,std::size_t size) noexcept {
    if(p<0x10000||p>0x20000||size>0x20000-p){++invalidReads;return false;}
    std::memcpy(target,memory.data()+p-0x10000,size);return true;
}
template<class T> void Put(unsigned p,const T& value){std::memcpy(memory.data()+p-0x10000,&value,sizeof(value));}
void Check(bool value,const char* label){if(!value)throw std::runtime_error(label);}
unsigned Count(const std::string& s,const std::string& part){
    unsigned n=0;for(std::size_t p=0;(p=s.find(part,p))!=std::string::npos;p+=part.size())++n;return n;
}
struct WindowFile {
    std::filesystem::path directory,file;
    WindowFile(){
        directory=std::filesystem::temp_directory_path()/
            ("fvr-native-fade-file-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
        Check(std::filesystem::create_directory(directory),"unique window test directory");file=directory/"native-fade-window.txt";
    }
    ~WindowFile(){std::error_code ignored;std::filesystem::remove(file,ignored);std::filesystem::remove(directory,ignored);}
    void Write(const std::string& text){std::ofstream output(file,std::ios::trunc);output<<text;output.close();Check(bool(output),"window file write");}
};
void CheckFileConfiguration(unsigned vt){
    WindowFile file;
    Observer absent;Check(absent.ConfigureFile(file.file,vt,ReadMemory)&&!absent.Enabled(),"absent window file stays disabled");
    const auto start=GetTickCount64()+5000;
    const auto text=[&](const std::string& duration,const std::string& limit="1"){
        return std::to_string(start)+" "+duration+" "+limit+"\n";
    };
    // Exercise the exact public file path used before NativeProbe hook setup.
    // Direct Configure already accepted 12000 in399; only this path reproduced
    // the actual failure. Literal boundary values keep the test independent.
    for(unsigned duration:{12000u,10001u,10000u}){
        file.Write(text(std::to_string(duration)));Observer accepted;
        Check(accepted.ConfigureFile(file.file,vt,ReadMemory)&&accepted.Enabled(),"file duration at/below12s admitted");
        Check(!accepted.Due(start-1)&&accepted.Due(start)&&accepted.Due(start+duration-1)&&!accepted.Due(start+duration),"file window exact half-open endpoints");
        Check(!accepted.ConfigureFile(file.file,vt,ReadMemory),"file cannot reconfigure active observer");
    }
    for(const auto& invalid:std::array<std::string,9>{text("12001"),text("0"),text("-1"),text("4294979296"),
            text("12000","0"),text("12000","32769"),text("12000")+"extra",text("12000x"),std::string(129,'1')}){
        file.Write(invalid);Observer rejected;Check(!rejected.ConfigureFile(file.file,vt,ReadMemory)&&!rejected.Enabled(),"invalid file rejected without observer allocation");
    }
    file.Write(std::to_string(GetTickCount64()+120000)+" 12000 1\n");Observer future;
    Check(!future.ConfigureFile(file.file,vt,ReadMemory),"excessive future lead rejected");
    file.Write("1 12000 1\n");Observer past;Check(!past.ConfigureFile(file.file,vt,ReadMemory),"past start rejected");
    file.Write(text("12000"));Observer badGetter;Check(!badGetter.ConfigureFile(file.file,vt+4,ReadMemory),"file still requires verified getter bytes");
    std::cout<<"PASS actual ConfigureFile 12s boundary/parser/getter validation\n";
}
int main(int argc,char** argv){try{
    const bool fileOnly=argc==2&&std::strcmp(argv[1],"--file-window")==0;
    Check(argc==1||fileOnly,"unknown test option");
    constexpr unsigned vt=0x10000,a=0x11000,b=0x11200,v0=0x12000,v1=0x14000,req=0x18000,world=0x19000;
    Put(vt+0x58,a);Put(vt+0xd8,b);
    constexpr unsigned char scale[]={0x8d,0x81,0x30,0x15,0,0,0xc3};
    constexpr unsigned char effect[]={0x8b,0x44,0x24,4,0xd9,0x81,0x50,0x16,0,0,0xd9,0x18,
        0xd9,0x81,0x54,0x16,0,0,0xd9,0x58,4,0xd9,0x81,0x58,0x16,0,0,0xd9,0x58,8,
        0xd9,0x81,0x5c,0x16,0,0,0xd9,0x58,0x0c,0xc2,4,0};
    Put(a,scale);Put(b,effect);
    if(fileOnly){CheckFileConfiguration(vt);return 0;}
    for(auto v:{v0,v1}){Put(v,vt);Put(v+0x70,req);}
    Put(world+0x88,17u);Put(req+0xc4,1u);
    Put(v0+0x1530,std::array<unsigned,3>{0x3f800000,0x3f000000,0});
    Put(v1+0x1530,std::array<unsigned,3>{0,0x3f800000,0x3f800000});
    Put(v0+0x1650,std::array<unsigned,4>{0,0,0,0x3f800000});
    Put(v1+0x1650,std::array<unsigned,4>{0,0,0,0x7fc00001}); // Preserve raw NaN bits as valid JSON integers.
    Key k;k.world=world;k.request=req;k.view=v0;k.candidates={v0,v1};k.candidateCount=2;
    k.candidatesAreStereoPair=true;k.pairRequest=77;k.tracking=80;k.space=3;k.leaseFrame=17;
    Observer disabled;Check(!disabled.Enabled()&&!disabled.Due(1000),"disabled by default");
    Check(Observer::VerifyGetters(vt,ReadMemory),"verified getter bodies");
    memory[a-0x10000+2]^=1;Check(!Observer::VerifyGetters(vt,ReadMemory),"changed field offset rejected");
    memory[a-0x10000+2]^=1;
    CheckFileConfiguration(vt);
    Observer bad;Check(!bad.Configure(100,12001,2,vt,ReadMemory),"duration bound");
    Observer extended;Check(extended.Configure(1000,12000,1,vt,ReadMemory)&&extended.Due(12999)&&!extended.Due(13000),"exact12s window remains half-open");
    Check(!bad.Configure(100,100,Observer::Capacity+1,vt,ReadMemory),"record bound");
    Check(!bad.Configure(UINT64_MAX-5,6,1,vt,ReadMemory),"time overflow rejected");
    Observer o;Check(o.Configure(1000,100,2,vt,ReadMemory),"configure once");
    Check(!o.Configure(1000,100,2,vt,ReadMemory),"second configure rejected");
    o.SampleAt(999,Phase::WorldBefore,k);
    o.SampleAt(1000,Phase::DrawBefore,k,reinterpret_cast<ID3D11DeviceContext*>(std::uintptr_t(1))); // Unverified context must not be touched.
    Put(v1+0x70,req+4);o.SampleAt(1050,Phase::DrawAfter,k); // Invalid owner remains explicit, no values admitted.
    o.SampleAt(1099,Phase::CaptureSource,k); // Overflow is bounded and reported.
    o.SampleAt(1100,Phase::WorldAfter,k); // Half-open end.
    std::ostringstream out;o.Write(out);const auto s=out.str();
    Check(Count(s,"\"index\":")==2,"exact record cap");
    Check(s.find("\"attempted_lower_bound\":3")!=std::string::npos&&s.find("\"overflow\":true")!=std::string::npos,"overflow receipt");
    Check(s.find("\"pair_request\":77")!=std::string::npos&&s.find("\"lease_frame\":17")!=std::string::npos,"pair identity");
    Check(s.find("\"effect_bits\":[0,0,0,2143289345]")!=std::string::npos,"raw nonfinite bits preserved");
    Check(s.find("\"identity\":0,\"scale_read\":0,\"effect_read\":0")!=std::string::npos,"owner failure rejected");
    Check(Count(s,"\"phase\":")==2&&Count(s,"\"read\":0,\"context\":0")==2,"no graphics calls requested");
    const auto beforeInvalid=invalidReads;unsigned dummy=0;
    Check(!ReadField(ReadMemory,UINT32_MAX-1,4,&dummy,4),"offset wrap rejected");
    Check(!ReadField(ReadMemory,UINT32_MAX-1,0,&dummy,4),"read range wrap rejected");
    Check(!ReadField(ReadMemory,0,0,&dummy,4),"null object rejected");
    Check(invalidReads==beforeInvalid,"invalid ranges never reach memory reader");
    Check(CheckContext(true,42,42,10,10)==ContextStatus::Ready,"verified graphics context");
    Check(CheckContext(false,42,42,10,10)==ContextStatus::BootstrapUnavailable,"unpublished bootstrap denied");
    Check(CheckContext(true,42,43,10,10)==ContextStatus::DifferentThread,"foreign thread denied");
    Check(CheckContext(true,42,42,10,11)==ContextStatus::DifferentContext,"foreign context denied");
    // Overflow remains sticky through repeated sampling; no old rows are overwritten.
    for(unsigned i=0;i<10000;++i)o.SampleAt(1050,Phase::WorldBefore,k);
    std::ostringstream saturated;o.Write(saturated);Check(saturated.str()==s,"overflow cannot wrap or overwrite");
    // 4 concurrent hook callers publish immutable rows through the release/acquire handoff.
    Put(v1+0x70,req);Observer concurrent;Check(concurrent.Configure(2000,8000,Observer::Capacity,vt,ReadMemory),"full window allocation");
    std::vector<std::thread> workers;
    for(unsigned t=0;t<4;++t)workers.emplace_back([&]{for(unsigned i=0;i<4096;++i)concurrent.SampleAt(2000+i,Phase::DrawAfter,k);});
    for(auto& thread:workers)thread.join();
    std::ostringstream all;concurrent.Write(all);
    Check(Count(all.str(),"\"index\":")==16384&&all.str().find("\"overflow\":false")!=std::string::npos,"concurrent bounded records retained");
    Check(Count(all.str(),"\"effect_read\":1")==32768,"both states published before row admission");
    std::cout<<"row_bytes="<<sizeof(Row)<<" capacity_bytes="<<sizeof(Row)*Observer::Capacity<<'\n';
    std::cout<<"PASS native fade observer bounds/getter identity/two-view state/owner loss/raw bits\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
