#include "fvr/graphics/DiagnosticImageLifetime.h"
#include <array>
#include <atomic>
#include <fstream>
#include <iostream>
#include <latch>
#include <thread>
#include <vector>
#include <cstdio>
using fvr::graphics::DiagnosticImageLifetime;
void Require(bool okay,const char* message){if(!okay)throw message;}
int main(){try {
    {
        DiagnosticImageLifetime life;std::vector<unsigned char> pixels(4096,42);
        Require(!life.TryQuiesce(false),"failed detach must not seal");
        const auto held=life.ReleaseSaved(pixels,true,true,true);
        Require(!held.released&&pixels.size()==4096&&pixels[0]==42,"live image retained");
        DiagnosticImageLifetime::Scope late(life);Require(late.Active(),"failed detach still admits callback");
    }
    {
        DiagnosticImageLifetime life;std::latch entered(1),finish(1);std::atomic<bool> active=false;
        std::thread callback([&]{DiagnosticImageLifetime::Scope scope(life);active=scope.Active();entered.count_down();finish.wait();});
        entered.wait();const bool blocked=!life.TryQuiesce(true);finish.count_down();callback.join();
        Require(active&&blocked,"in-flight callback prevents retirement");
        Require(life.TryQuiesce(true)&&life.Quiescent(),"returned callback permits retirement");
        DiagnosticImageLifetime::Scope delayed(life);Require(!delayed.Active(),"late callback must use native original");
        Require(life.TryQuiesce(true),"retirement idempotent");
    }
    {
        for(unsigned missing=0;missing<3;++missing){DiagnosticImageLifetime life;std::vector<unsigned char> pixels(4096,9);
            Require(life.TryQuiesce(true),"seal guard test");
            const auto r=life.ReleaseSaved(pixels,missing!=0,missing!=1,missing!=2);
            Require(!r.released&&pixels.size()==4096&&pixels[4095]==9,"pending/failed/unsaved evidence retained");
        }
    }
    {
        DiagnosticImageLifetime life;std::vector<unsigned char> pixels(16384);for(std::size_t n=0;n<pixels.size();++n)pixels[n]=static_cast<unsigned char>(n%251);
        const auto expected=pixels;pixels.reserve(32768);const auto capacity=pixels.capacity();
        const char* file="diagnostic-image-lifetime-roundtrip.bin";
        std::ofstream output(file,std::ios::binary|std::ios::trunc);output.write(reinterpret_cast<const char*>(pixels.data()),std::streamsize(pixels.size()));output.close();
        Require(bool(output),"disk serialization succeeds");Require(life.TryQuiesce(true),"saved image seal");
        const auto freed=life.ReleaseSaved(pixels,true,bool(output),true);
        Require(freed.released&&freed.bytes==expected.size()&&freed.capacityBytes==capacity&&pixels.empty()&&pixels.capacity()==0,"capacity actually freed");
        std::ifstream input(file,std::ios::binary);std::vector<unsigned char> restored(expected.size());input.read(reinterpret_cast<char*>(restored.data()),std::streamsize(restored.size()));
        Require(bool(input)&&restored==expected,"saved bytes unchanged after release");input.close();std::remove(file);
        Require(!life.ReleaseSaved(pixels,true,true,true).released,"no double-release accounting");
    }
    {
        DiagnosticImageLifetime life;std::array<std::vector<unsigned char>,3> images;
        for(auto& p:images)p.assign(8192,17);Require(life.TryQuiesce(true),"mixed images seal");
        std::size_t bytes=0;unsigned released=0;
        for(unsigned n=0;n<images.size();++n){const auto r=life.ReleaseSaved(images[n],n!=1,n!=2,true);bytes+=r.bytes;released+=r.released?1u:0u;}
        Require(released==1&&bytes==8192&&images[0].capacity()==0&&images[1].size()==8192&&images[2].size()==8192,"only completed saved image released");
    }
    std::cout<<"DiagnosticImageLifetime: 5 groups passed\n";return 0;
}catch(const char* e){std::cerr<<e<<'\n';return 1;}}
