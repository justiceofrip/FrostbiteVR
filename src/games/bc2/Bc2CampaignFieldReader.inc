struct Reader {
    const VehicleRouteMemory& memory;bool okay=true;
    bool Read(std::uint64_t at,void* dst,std::size_t size)noexcept {
        if(at<0x10000||at+size>UINT32_MAX||!size||size>4096||!memory.read||!memory.read(memory.context,std::uint32_t(at),dst,size)){okay=false;return false;}return true;
    }
    unsigned Word(std::uint64_t at)noexcept{unsigned value=0;Read(at,&value,4);return value;}
    bool Text(unsigned at,std::string_view expected)noexcept{
        if(expected.size()>=128)return false;for(unsigned n=0;n<=expected.size();++n){char c=0;if(!Read(std::uint64_t(at)+n,&c,1)||c!=(n==expected.size()?0:expected[n]))return false;}return true;
    }
    unsigned Info(unsigned object)noexcept {
        std::array<unsigned char,6> code{};if(!Read(Word(std::uint64_t(Word(object))+8),code.data(),6)||code[0]!=0xb8||code[5]!=0xc3)return 0;
        unsigned info=0;std::memcpy(&info,code.data()+1,4);return info;
    }
    bool Field(unsigned object,std::string_view parent,std::string_view field,unsigned offset,std::string_view type)noexcept {
        auto info=Info(object);std::array<unsigned,12> visited{};
        for(unsigned depth=0;info&&depth<visited.size();++depth){
            if(std::find(visited.begin(),visited.begin()+depth,info)!=visited.begin()+depth)return false;visited[depth]=info;
            const auto meta=Word(std::uint64_t(info)+4);std::array<unsigned char,16> header{};if(!Read(meta+4,header.data()+4,4)||!Read(meta+13,header.data()+13,1))return false;
            const unsigned flags=header[4]|(unsigned(header[5])<<8),size=header[6]|(unsigned(header[7])<<8),count=header[13];
            if(Text(Word(meta),parent)){
                if(flags!=0x35||count>128||offset>=size)return false;
                const auto records=Word(std::uint64_t(info)+36);unsigned matches=0;
                for(unsigned n=0;n<count;++n){std::array<unsigned,6> row{};if(!Read(std::uint64_t(records)+n*24,row.data(),24))return false;
                    if(Text(row[0],field)){if(row[4]!=offset||!Text(Word(Word(std::uint64_t(row[2])+4)),type))return false;++matches;}}
                return okay&&matches==1;
            }
            info=Word(std::uint64_t(info)+20);
        }return false;
    }
};
