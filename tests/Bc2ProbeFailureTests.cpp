#include "Test.h"
#include "Bc2ProbeFailure.h"
#include "Bc2ProbeOutcome.h"
#include <array>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace fvr::bc2;
namespace {
std::vector<std::string> reports;
std::string Render(std::string_view value,bool disabled=true) {
    std::ostringstream out;WriteProbeFailureJson(out,value,disabled);
    reports.push_back(out.str());return out.str();
}
std::string Expected(std::string_view escaped,bool disabled=true,bool truncated=false,bool replaced=false) {
    return "{\"state\":\"failed\",\"error\":\""+std::string(escaped)+"\",\"hooks_disabled\":"+
        (disabled?"true":"false")+",\"error_truncated\":"+(truncated?"true":"false")+
        ",\"error_utf8_replaced\":"+(replaced?"true":"false")+"}\n";
}
int BootstrapOutcome() {
    const auto render=[](const NativeProbeRunOutcome& value){std::ostringstream out;WriteNativeProbeRunOutcomeJson(out,100,value);return out.str();};
    NativeProbeRunOutcome value;value.reportBytesKnown=true;value.reportBytes=123;
    auto row=render(value);
    CHECK(row.find("\"outcome\":\"remote_thread_exited\"")!=std::string::npos);
    CHECK(row.find("\"probe_exit\":0,\"game_exit_code\":null")!=std::string::npos);
    CHECK(row.find("\"report_bytes\":123,\"report_nonempty\":true")!=std::string::npos);
    value.remoteThreadExitCode=3221225477u;row=render(value);
    CHECK(row.find("\"probe_exit\":3221225477")!=std::string::npos);
    value.gameExited=true;value.gameExitCodeKnown=true;value.reportBytes=0;row=render(value);
    CHECK(row.find("\"remote_thread_exit_observed\":false,\"probe_exit\":null,\"game_exit_code\":0")!=std::string::npos);
    CHECK(row.find("\"report_bytes\":0,\"report_nonempty\":false")!=std::string::npos);
    value.gameExitCode=3221225477u;row=render(value);
    CHECK(row.find("\"probe_exit\":null,\"game_exit_code\":3221225477")!=std::string::npos);
    value.gameExitCodeKnown=false;value.reportBytesKnown=false;row=render(value);
    CHECK(row.find("\"game_exit_code\":null,\"report_bytes\":null,\"report_nonempty\":null")!=std::string::npos);
    CHECK(row.size()<1024);return 0;
}
int JsonEscaping() {
    CHECK(Render("Native gameplay binding unverified")==Expected("Native gameplay binding unverified"));
    CHECK(Render("",false)==Expected("",false));
    CHECK(Render("G:\\EA Games\\BC2\\\"profile\"\nnext\r\t\b\f")
        ==Expected("G:\\\\EA Games\\\\BC2\\\\\\\"profile\\\"\\nnext\\r\\t\\b\\f"));
    std::string controls;for(int c=0;c<32;++c)controls.push_back(static_cast<char>(c));
    CHECK(Render(controls)==Expected("\\u0000\\u0001\\u0002\\u0003\\u0004\\u0005\\u0006\\u0007"
        "\\b\\t\\n\\u000b\\f\\r\\u000e\\u000f\\u0010\\u0011\\u0012\\u0013\\u0014\\u0015"
        "\\u0016\\u0017\\u0018\\u0019\\u001a\\u001b\\u001c\\u001d\\u001e\\u001f"));
    CHECK(Render("\"},\"hooks_disabled\":true,\"extra\":\"$()")
        ==Expected("\\\"},\\\"hooks_disabled\\\":true,\\\"extra\\\":\\\"$()"));
    return 0;
}
int Utf8() {
    // Literal bytes keep this regression independent of the compiler code page.
    const std::string valid="\xc2\xa3\xe6\x97\xa5\xf0\x9f\x98\x80\xf4\x8f\xbf\xbf";
    CHECK(Render(valid)==Expected(valid));
    const std::string replacement="\xef\xbf\xbd";
    CHECK(Render("\xff")==Expected(replacement,true,false,true));
    CHECK(Render("\xe2\x82")==Expected(replacement+replacement,true,false,true));
    for(const auto invalid:std::array<std::string_view,5>{"\xc0\xaf","\xed\xa0\x80","\xf0\x80\x80\x80","\xf4\x90\x80\x80","\xf5\x80\x80\x80"}) {
        std::string expected;for(std::size_t n=0;n<invalid.size();++n)expected+=replacement;
        CHECK(Render(invalid)==Expected(expected,true,false,true));
    }
    CHECK(Render("\xe2" "A")==Expected(replacement+"A",true,false,true));
    return 0;
}
int Bounds() {
    const auto limit=MaximumProbeFailureErrorBytes;
    const std::string exact(limit,'x');
    CHECK(Render(exact)==Expected(exact));
    CHECK(Render(exact+"y")==Expected(exact,true,true));
    for(const auto codepoint:std::array<std::string_view,3>{"\xc2\xa3","\xe6\x97\xa5","\xf0\x9f\x98\x80"}) {
        for(std::size_t left=1;left<codepoint.size();++left) {
            const std::string prefix(limit-left,'x');
            CHECK(Render(prefix+std::string(codepoint))==Expected(prefix,true,true));
        }
        const std::string fitting=std::string(limit-codepoint.size(),'x')+std::string(codepoint);
        CHECK(Render(fitting)==Expected(fitting));
        CHECK(Render(fitting+"y")==Expected(fitting,true,true));
    }
    // Invalid bytes near the limit must not disguise an ASCII byte as the tail
    // of a truncated valid sequence. Both the replacement and the 'A' survive.
    const std::string prefix(limit-2,'x');
    CHECK(Render(prefix+"\xe2" "A!")==Expected(prefix+"\xef\xbf\xbd" "A",true,true,true));
    const auto worst=Render(std::string(limit+1,'\x01'));
    CHECK(worst.size()<=MaximumProbeFailureJsonBytes);
    std::string escaped;for(std::size_t i=0;i<limit;++i)escaped+="\\u0001";
    CHECK(worst==Expected(escaped,true,true));
    // Exercise the actual e.what() overload, including empty/null and both
    // sides of its bounded scan, rather than only the length-aware test API.
    for(const auto& value:std::array<std::string,3>{"",exact,exact+"y"}) {
        std::ostringstream out;WriteProbeFailureJson(out,value.c_str(),false);
        CHECK(out.str()==Expected(value.substr(0,limit),false,value.size()>limit));
    }
    std::ostringstream empty;WriteProbeFailureJson(empty,nullptr,false);CHECK(empty.str()==Expected("",false));
    return 0;
}
}
int main(int argc,char** argv) {
    CHECK(BootstrapOutcome()==0);CHECK(JsonEscaping()==0);CHECK(Utf8()==0);CHECK(Bounds()==0);
    // Optional independent JSON-parser validation without adding a runtime or
    // parser dependency to the native target; default CTest does no file IO.
    if(argc==2&&std::string_view(argv[1])=="--json-lines") {
        for(const auto& report:reports)std::cout<<report;
    } else std::cout<<"Bc2ProbeFailure: escaped controls/paths, UTF-8 validity, bounded code points and native C-string path passed\n";
}
