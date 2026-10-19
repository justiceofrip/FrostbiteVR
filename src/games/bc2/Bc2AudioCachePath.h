#pragma once
#include <filesystem>

namespace fvr::bc2 {
// Public installs choose their own drive. This is a path-shape check only;
// each reader still requires its exact byte count, SHA-256 and PCM WAV layout.
// Drive-relative, network and device namespace paths are not cache locations.
inline bool LocalAudioCacheFile(const std::filesystem::path& path) {
    if(!path.is_absolute()||!path.has_filename())return false;
    const auto root=path.root_name().native();
    if(root.size()!=2||root[1]!=L':'||
       !((root[0]>=L'A'&&root[0]<=L'Z')||(root[0]>=L'a'&&root[0]<=L'z')))return false;
    const auto& text=path.native();
    for(std::size_t i=2;i<text.size();++i) {
        const auto c=text[i];
        if(c<32||c==L':'||c==L'"'||c==L'<'||c==L'>'||c==L'|'||c==L'?'||c==L'*')return false;
    }
    for(const auto& component:path.relative_path())
        if(component==L"."||component==L"..")return false;
    return true;
}
}
