#include "Test.h"
#include "Bc2AudioCachePath.h"
#include <array>
#include <iostream>
#include <string>

using fvr::bc2::LocalAudioCacheFile;
int main() {
    // These are path values only. No file is opened or created on C:, X: or any
    // other drive; actual native readers retain their independent content gates.
    for(const auto path:std::array<const wchar_t*,8>{
        L"X:\\BC2VR\\data\\runtime\\cache\\e197fd19371bb73686cf804a882940e3b11374193be0956ba87588d4960da251\\pump\\spas-rear.wav",
        L"X:\\BC2VR\\data\\runtime\\cache\\e197fd19371bb73686cf804a882940e3b11374193be0956ba87588d4960da251\\shell\\spas-shell-insert.wav",
        L"C:\\Users\\Player\\BC2 VR\\cache\\pump\\spas-closure.wav",
        L"G:\\cache\\pump\\spas-rear.wav",
        L"D:\\Games and Mods\\BC2 VR\\cache\\shell\\spas-shell-insert.wav",
        L"x:\\BC2VR\\cache\\pump\\spas-rear.wav",
        L"X:/BC2 VR/cache/pump/spas-rear.wav",
        L"X:\\VR\\\u65e5\u672c\u8a9e \u00e9\\pump\\spas-rear.wav"}) {
        CHECK(LocalAudioCacheFile(path));
    }
    for(const auto path:std::array<const wchar_t*,19>{
        L"",L"spas-rear.wav",L"pump\\spas-rear.wav",L"X:spas-rear.wav",L"X:pump\\spas-rear.wav",
        L"\\pump\\spas-rear.wav",L"/pump/spas-rear.wav",L"X:",L"X:\\",L"X:\\cache\\",
        L"\\\\server\\share\\pump\\spas-rear.wav",L"\\\\?\\X:\\cache\\spas-rear.wav",
        L"\\\\.\\X:\\cache\\spas-rear.wav",L"1:\\cache\\spas-rear.wav",
        L"X:\\cache\\..\\spas-rear.wav",L"X:\\cache\\.\\spas-rear.wav",
        L"X:\\cache\\spas-rear.wav:alternate",L"X:\\cache\\*.wav",L"\"X:\\cache\\spas-rear.wav\""}) {
        CHECK(!LocalAudioCacheFile(path));
    }
    std::wstring embedded=L"X:\\cache\\spas-rear.wav";embedded.push_back(L'\0');embedded+=L"ignored";
    CHECK(!LocalAudioCacheFile(std::filesystem::path(embedded)));
    CHECK(!LocalAudioCacheFile(L"X:\\cache\n\\spas-rear.wav"));
    std::cout<<"Bc2AudioCachePath: tester X:, C:, G:, spaces/Unicode and malformed paths passed without file IO\n";
}
