#pragma once
#include <cstddef>
#include <ostream>
#include <string_view>

namespace fvr::bc2 {
inline constexpr std::size_t MaximumProbeFailureErrorBytes=4096;
// Each input byte can require at most six JSON bytes; metadata has fixed size.
inline constexpr std::size_t MaximumProbeFailureJsonBytes=6*MaximumProbeFailureErrorBytes+128;

namespace probeFailureDetail {
// Input has already been bounded. No allocation or native/runtime work belongs
// in this exception-path writer. Invalid encoding is visible, never emitted as
// malformed JSON; a valid code point crossing the limit is omitted as a whole.
inline void Write(std::ostream& out,std::string_view input,bool truncated,bool hooksDisabled) {
    out<<"{\"state\":\"failed\",\"error\":\"";
    bool replaced=false;
    for(std::size_t i=0;i<input.size();) {
        const auto first=static_cast<unsigned char>(input[i]);
        if(first<0x80) {
            switch(first) {
            case '"':out<<"\\\"";break;
            case '\\':out<<"\\\\";break;
            case '\b':out<<"\\b";break;
            case '\f':out<<"\\f";break;
            case '\n':out<<"\\n";break;
            case '\r':out<<"\\r";break;
            case '\t':out<<"\\t";break;
            default:
                if(first<0x20) {
                    constexpr char hex[]="0123456789abcdef";
                    out<<"\\u00";out.put(hex[first>>4]);out.put(hex[first&15]);
                } else out.put(static_cast<char>(first));
            }
            ++i;continue;
        }
        const std::size_t width=first>=0xc2&&first<=0xdf?2:
            first>=0xe0&&first<=0xef?3:first>=0xf0&&first<=0xf4?4:0;
        const std::size_t remaining=input.size()-i;
        const std::size_t available=remaining<width?remaining:width;
        bool valid=width!=0;
        for(std::size_t n=1;n<available&&valid;++n) {
            const auto next=static_cast<unsigned char>(input[i+n]);
            valid=next>=0x80&&next<=0xbf;
            if(n==1) {
                if(first==0xe0)valid=valid&&next>=0xa0; // no overlong code point
                if(first==0xed)valid=valid&&next<=0x9f; // no UTF-16 surrogate
                if(first==0xf0)valid=valid&&next>=0x90;
                if(first==0xf4)valid=valid&&next<=0x8f; // at most U+10FFFF
            }
        }
        if(valid&&available==width) {
            out.write(input.data()+i,static_cast<std::streamsize>(width));i+=width;
        } else if(valid&&truncated&&available<width) {
            break;
        } else {
            out.write("\xef\xbf\xbd",3);replaced=true;++i;
        }
    }
    out<<"\",\"hooks_disabled\":"<<(hooksDisabled?"true":"false")
       <<",\"error_truncated\":"<<(truncated?"true":"false")
       <<",\"error_utf8_replaced\":"<<(replaced?"true":"false")<<"}\n";
}
}

inline void WriteProbeFailureJson(std::ostream& out,std::string_view error,bool hooksDisabled) {
    const bool truncated=error.size()>MaximumProbeFailureErrorBytes;
    if(truncated)error=error.substr(0,MaximumProbeFailureErrorBytes);
    probeFailureDetail::Write(out,error,truncated,hooksDisabled);
}

// std::exception::what() supplies a NUL-terminated string. Do not implicitly
// construct a string/string_view from it: that would scan an unbounded message.
inline void WriteProbeFailureJson(std::ostream& out,const char* error,bool hooksDisabled) {
    std::size_t size=0;
    if(error)while(size<MaximumProbeFailureErrorBytes&&error[size]!='\0')++size;
    const bool truncated=error&&size==MaximumProbeFailureErrorBytes&&error[size]!='\0';
    probeFailureDetail::Write(out,{error?error:"",size},truncated,hooksDisabled);
}
}
