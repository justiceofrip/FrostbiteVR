struct Reader {
    const ReloadStateMemory& m;unsigned base,size;
    bool Read(std::uint64_t at,void* p,std::size_t n)const noexcept {
        return at>=0x10000&&n&&n<=4096&&at+n<=UINT32_MAX&&m.read&&m.read(m.context,unsigned(at),p,n);
    }
    template<class T>bool Get(std::uint64_t at,T& out)const noexcept{return Read(at,&out,sizeof(out));}
    bool Image(unsigned at,std::size_t n)const noexcept{return at>=base&&std::uint64_t(at)+n<=std::uint64_t(base)+size;}
    bool Name(unsigned at,const char* text)const noexcept {
        std::array<char,96> value{};const auto n=std::strlen(text)+1;
        return n<=value.size()&&Image(at,n)&&Read(at,value.data(),n)&&!std::memcmp(value.data(),text,n);
    }
    bool Field(unsigned object,const char* type,unsigned sizeWanted,const char* name,unsigned offset,bool unused=false)const noexcept {
        (void)unused;unsigned table=0,getter=0,info=0,meta=0,fields=0;std::array<unsigned char,6> code{};std::array<unsigned,7> words{};
        if(!m.type||!m.type(m.context,object,type)||!Get(object,table)||!Image(table,12)||!Get(std::uint64_t(table)+8,getter)||
           !Image(getter,6)||!Read(getter,code.data(),6)||code[0]!=0xb8||code[5]!=0xc3)return false;
        std::memcpy(&info,code.data()+1,4);
        if(!Image(info,40)||!Get(std::uint64_t(info)+4,meta)||!Image(meta,28)||!Read(meta,words.data(),28)||
           !Name(words[0],type)||(words[1]&0xffff)!=0x35||(sizeWanted&&(words[1]>>16)!=sizeWanted)||
           (words[1]>>16)<=offset||(words[1]>>16)>4096||!Get(std::uint64_t(info)+36,fields))return false;
        const unsigned count=(words[3]>>8)&255;if(!count||count>64||!Image(fields,count*24))return false;
        unsigned matches=0;
        for(unsigned n=0;n<count;++n){std::array<unsigned,6> f{};if(!Read(std::uint64_t(fields)+n*24,f.data(),24))return false;
            if(!Name(f[0],name))continue;
            unsigned booleanMeta=0;std::array<unsigned,7> b{};
            if(++matches!=1||f[4]!=offset||!Image(f[2],8)||!Get(std::uint64_t(f[2])+4,booleanMeta)||
               !Image(booleanMeta,28)||!Read(booleanMeta,b.data(),28))return false;
            if(!Name(b[0],"Boolean")||(b[1]>>16)!=1)return false;
        }return matches==1;
    }
};
