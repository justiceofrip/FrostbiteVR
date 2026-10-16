#include "Bc2PumpVisualObservation.h"
#include "Test.h"
#include <memory>
#include <sstream>
using namespace fvr::bc2;
namespace {
int OriginalWindowAndBounds(){
    auto journal=std::make_unique<PumpVisualJournal<2,2>>();
    PumpVisualSource before{};before.observedNs=100;before.generation=4;
    CHECK(!journal->Begin(before,false));
    PumpVisualCopy early{};early.observedNs=99;early.drawSerial=9;journal->Copy(early);
    PumpVisualSource first{};first.observedNs=1000;first.deadlineNs=1050;first.pumpObservedNs=990;
    first.pumpDeadlineNs=1040;first.pumpInput=7;first.generation=7;first.weapon=0x12345;
    CHECK(journal->Begin(first,true)==1);first.flags=PvFresh|PvAttachmentPending;first.stage=4;journal->Finish(first);
    PumpVisualSource second=first;second.id=0;second.observedNs=1001;second.generation=8;
    CHECK(journal->Begin(second,true)==2);journal->Finish(second);
    PumpVisualSource overflow=second;overflow.id=0;overflow.observedNs=1002;
    CHECK(!journal->Begin(overflow,true));
    PumpVisualSource late=second;late.id=0;late.observedNs=1000+PumpVisualJournal<2,2>::WindowNs;
    CHECK(!journal->Begin(late,true));
    PumpVisualCopy copy{};copy.sourceId=1;copy.observedNs=1002;copy.drawSerial=10;copy.callback=1;
    journal->Copy(copy);copy.callback=2;journal->Copy(copy);copy.drawSerial=11;journal->Copy(copy);
    copy.observedNs=late.observedNs;journal->Copy(copy);
    const auto data=journal->Snapshot();
    CHECK(data->sourceCount==2&&data->copyCount==2&&data->sourceDropped==1&&data->copyDropped==1);
    CHECK(data->beginNs==1000&&data->endNs==late.observedNs&&data->triggerGeneration==7);
    CHECK(data->sources[0].flags==(PvFresh|PvAttachmentPending)&&data->sources[0].complete);
    CHECK(data->sources[0].deadlineNs==1050&&data->sources[0].pumpDeadlineNs==1040);
    CHECK(data->copies[0].sourceId==data->copies[1].sourceId);
    CHECK(data->copies[0].drawSerial==data->copies[1].drawSerial&&data->copies[0].callback==1&&data->copies[1].callback==2);
    // Snapshot owns a heap copy, not references into the journal.
    journal.reset();CHECK(data->sources[1].generation==8);return 0;
}
int PaletteStagesRemainDistinct(){
    std::array<std::array<std::byte,64>,PumpVisualPartCount> palette{};
    for(unsigned n=0;n<PumpVisualPartCount;++n)for(unsigned v=0;v<16;++v){const std::uint32_t word=1000+n*100+v;
        std::memcpy(palette[n].data()+v*4,&word,4);}
    const auto original=palette;PumpVisualCopy copy{};
    for(unsigned n=0;n<PumpVisualPartCount;++n)CHECK(PumpVisualInput(copy,n,n,palette));
    CHECK(copy.inputMask==63);CHECK(copy.input[2][7]==1213); // col1,row3
    CHECK(!PumpVisualInput(copy,6,0,palette)&&!PumpVisualInput(copy,0,6,palette));
    std::array<std::byte,PumpVisualPartCount*48> packed{};
    for(unsigned n=0;n<PumpVisualPartCount;++n)std::memcpy(packed.data()+n*48,copy.input[n].data(),48);
    for(unsigned n=0;n<PumpVisualPartCount;++n)CHECK(PumpVisualOutput(copy,n,n,packed));
    CHECK(copy.input==copy.packed&&copy.packedMask==63&&palette==original);
    // A single detached-ring output cell remains evidence, never normalized.
    const std::uint32_t wrong=0x7fc01234u;std::memcpy(packed.data()+5*48+7*4,&wrong,4);
    CHECK(PumpVisualOutput(copy,5,5,packed)&&copy.packed[5][7]==wrong&&copy.input[5]!=copy.packed[5]);
    CHECK(!PumpVisualOutput(copy,0,6,packed));
    // Skipped private write and no existing destination read stay unavailable.
    PumpVisualCopy missing{};CHECK(!missing.changed&&!missing.packedRead&&!missing.inputMask&&!missing.packedMask);
    return 0;
}
int CompletedSourceCannotBeRewritten(){
    auto journal=std::make_unique<PumpVisualJournal<1,1>>();
    PumpVisualSource source{};source.observedNs=10;source.generation=11;source.flags=PvFresh;
    CHECK(journal->Begin(source,true));source.stage=6;
    source.companionCount=2;source.companionReturning=true;source.fixedCount=3;
    source.parts[1].found=true;source.parts[1].requested=true;source.parts[1].requestedWorld[12]=0x3f800000;
    source.parts[1].finalKnown=true;source.parts[1].finalSkin[12]=0x40000000;journal->Finish(source);
    source.parts[1].finalSkin[12]=0;source.stage=8;journal->Finish(source);
    CHECK(journal->Snapshot()->sources[0].parts[1].finalSkin[12]==0x40000000);
    CHECK(journal->Snapshot()->sources[0].stage==6);
    std::ostringstream report;journal->Report(report);const auto text=report.str();
    CHECK(text.find("\"native_frame_eye_association\":false")!=std::string::npos);
    CHECK(text.find("\"matrix_encoding\":\"uint32_float_bits\"")!=std::string::npos);
    CHECK(text.find("\"fixed_count\":3,\"companion_count\":2,\"companion_returning\":1")!=std::string::npos);
    CHECK(text.find("\"final_skin\":[0,0,0,0,0,0,0,0,0,0,0,0,1073741824")!=std::string::npos);
    return 0;
}
}
int main(){if(OriginalWindowAndBounds()||PaletteStagesRemainDistinct()||CompletedSourceCannotBeRewritten())return 1;
    std::puts("PumpVisualObservation: 3 focused groups PASS");return 0;}

