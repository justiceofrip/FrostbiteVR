int BodyMaterialCacheChecks(){
    using namespace fvr::graphics;
    RigidPropSectionUpload section;section.mesh.sourceVertexSkinHash=11;section.mesh.sourcePositionHash=12;
    section.mesh.vertices={{0,0,1,0,1,0},{0,1,1,0,1,0},{1,0,1,0,1,0}};
    BodyPropCatalog source{{{1,2,3},{section}}};std::vector<std::byte> bytes;
    auto number=[&](std::uint64_t value,unsigned n){for(unsigned i=0;i<n;++i){bytes.push_back(std::byte(value&255));value>>=8;}};
    auto floating=[&](float value){number(std::bit_cast<std::uint32_t>(value),4);};
    for(char c:std::string_view("FVRTEX01"))bytes.push_back(std::byte(c));number(1,4);number(1,4);
    number(1,4);number(1,4);number(0,4);number(0,4);number(1,4);number(4,4);
    number(0xff003fff,4);const auto assignment=bytes.size();
    number(1,8);number(2,8);number(3,8);number(0,4);number(0,4);number(11,8);number(12,8);number(3,4);
    for(float c:{1.f,1.f,1.f,1.f})floating(c);
    for(float uv:{-.25f,.25f,1.25f,.25f,.25f,1.25f})floating(uv);
    const auto result=ApplyBodyPropMaterials(bytes,source);CHECK(result.catalog&&result.sections==1&&result.textures==1);
    const auto& actual=result.catalog->front().sections[0];CHECK(actual.albedo&&actual.uv.size()==3&&actual.uv[0][0]==-.25f);
    CHECK(actual.albedo->mips[0].rgba==std::vector<std::uint8_t>({255,63,0,255}));
    CHECK(source[0].sections[0].uv.empty()&&!source[0].sections[0].albedo); // No partial source mutation.
    for(std::size_t n=0;n<bytes.size();++n)CHECK(!ApplyBodyPropMaterials(std::span(bytes).first(n),source).catalog);
    for(auto offset:{std::size_t(0),std::size_t(16),assignment,assignment+24,assignment+28,assignment+32,assignment+40,assignment+48}){
        auto bad=bytes;bad[offset]^=std::byte{0x80};CHECK(!ApplyBodyPropMaterials(bad,source).catalog);}
    {auto bad=bytes;const auto n=std::bit_cast<std::array<std::byte,4>>(NAN);std::copy(n.begin(),n.end(),bad.end()-24);
        CHECK(!ApplyBodyPropMaterials(bad,source).catalog);}
    {auto bad=bytes;bad.push_back(std::byte{});CHECK(!ApplyBodyPropMaterials(bad,source).catalog);}
    {auto bad=bytes;bad[12]=std::byte{2};bad.insert(bad.end(),bytes.begin()+assignment,bytes.end());
        CHECK(!ApplyBodyPropMaterials(bad,source).catalog);}
    {auto duplicate=source;duplicate.push_back(source[0]);CHECK(!ApplyBodyPropMaterials(bytes,duplicate).catalog);}
    PropAlbedoTexture mip;mip.width=3;mip.height=2;mip.mips.push_back({std::vector<std::uint8_t>(24)});mip.mips.push_back({{0,0,0,255}});
    CHECK(ValidPropTexture(mip));mip.mips.push_back({{0,0,0,255}});CHECK(!ValidPropTexture(mip));
    return 0;
}
