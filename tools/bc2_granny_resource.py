"""Bounded reader for observed Frostbite 1 PC GrannyAnimation resources.

No native library, process, asset export or runtime admission. Explicitly rejects
other container encodings. References remain offsets, never executable pointers.
"""
from __future__ import annotations
from dataclasses import dataclass
import math
import struct

MAGIC=bytes.fromhex('29de6cc0baa4532b25f5b7a5f666e2ee')
MAX_RESOURCE=16*1024*1024
MAX_ITEMS=100000

@dataclass(frozen=True)
class Field:
    kind:int
    name:str
    reference:tuple[int,int]|None
    width:int
    offset:int
    size:int

class Resource:
    def __init__(self,data,kind='GrannyAnimation'):
        if len(data)>MAX_RESOURCE or len(data)<16:raise ValueError('Resource size')
        if kind not in ('GrannyAnimation','GrannyModel') or struct.unpack_from('<I',data)[0]!=10:raise ValueError('Unsupported Frostbite resource wrapper')
        count_offset=8 if kind=='GrannyAnimation' else 4
        if kind=='GrannyAnimation' and struct.unpack_from('<I',data,4)[0]!=0:raise ValueError('Unsupported animation wrapper flags')
        count=struct.unpack_from('<I',data,count_offset)[0]
        if not 0<count<=4096:raise ValueError('String count')
        at=count_offset+4;self.names=[]
        for _ in range(count):
            if at+2>len(data):raise ValueError('Truncated string size')
            n=struct.unpack_from('<H',data,at)[0];at+=2
            if not 0<n<=2048 or at+n>len(data):raise ValueError('String bounds')
            text=data[at:at+n].decode('ascii');at+=n
            if any(ord(c)<32 or ord(c)>126 for c in text):raise ValueError('String characters')
            self.names.append(text)
        if at+4>len(data):raise ValueError('Missing container size')
        size=struct.unpack_from('<I',data,at)[0];self.data=bytes(data[at+4:])
        if size!=len(self.data) or size<104 or self.data[:16]!=MAGIC:raise ValueError('Unsupported Granny magic/size')
        header_size,header_format,r0,r1=struct.unpack_from('<4I',self.data,16)
        if header_format or r0 or r1:raise ValueError('Unsupported header encoding')
        h=struct.unpack_from('<18I',self.data,32)
        if h[0]!=7 or h[1]!=size or h[3]!=72 or not 1<=h[4]<=32 or header_size!=104+44*h[4]:raise ValueError('Unsupported Granny header layout')
        if h[9]!=0x80000028 or any(h[10:]):raise ValueError('Unsupported type tag or header extensions')
        self.sections=[];self.relocations={};self.type_cache={};self.parsing=set();self.objects=0;self.values=0
        for n in range(h[4]):
            s=self.unpack_file('<11I',104+44*n)
            if s[0]!=0 or s[2]!=s[3]:raise ValueError('Compressed Granny section unsupported')
            if s[1]<header_size or s[1]+s[2]>size or s[4] not in (1,2,4,8,16) or not 0<=s[5]<=s[6]<=s[3]:raise ValueError('Section bounds')
            if s[8]>MAX_ITEMS or s[10]>MAX_ITEMS or s[7]+12*s[8]>size or s[9]+16*s[10]>size:raise ValueError('Fixup table bounds')
            self.sections.append(s)
        regions=sorted((s[1],s[1]+s[2]) for s in self.sections if s[2])
        if any(a[1]>b[0] for a,b in zip(regions,regions[1:])):raise ValueError('Overlapping sections')
        for n,s in enumerate(self.sections):
            for k in range(s[8]):
                src,sec,off=self.unpack_file('<3I',s[7]+12*k)
                self.check((n,src),4);self.check((sec,off),1)
                if (n,src) in self.relocations:raise ValueError('Duplicate pointer fixup')
                self.relocations[n,src]=(sec,off)
        self.root_type=(h[5],h[6]);self.root_ref=(h[7],h[8])
        self.fields(self.root_type)

    def unpack_file(self,fmt,at):
        if at<0 or at+struct.calcsize(fmt)>len(self.data):raise ValueError('File read outside container')
        return struct.unpack_from(fmt,self.data,at)
    def check(self,ref,n):
        sec,at=ref
        if not 0<=sec<len(self.sections) or n<0 or at<0 or at+n>self.sections[sec][3]:raise ValueError('Section reference outside data')
        return self.sections[sec][1]+at
    def unpack(self,fmt,ref):return self.unpack_file(fmt,self.check(ref,struct.calcsize(fmt)))
    @staticmethod
    def add(ref,n):return ref[0],ref[1]+n
    def pointer(self,ref):
        self.check(ref,4)
        if ref in self.relocations:return self.relocations[ref]
        if self.unpack('<I',ref)[0]:raise ValueError('Unrelocated non-null pointer')
        return None
    def string(self,ref):
        index=self.unpack('<I',ref)[0]
        if index>=len(self.names):raise ValueError('String index outside wrapper')
        return self.names[index]
    def fields(self,ref):
        if ref in self.type_cache:return self.type_cache[ref]
        if ref in self.parsing:raise ValueError('Recursive inline type')
        if len(self.parsing)>=48:raise ValueError('Inline type nesting limit')
        self.parsing.add(ref);fields=[];offset=0;names=set()
        for n in range(256):
            here=self.add(ref,n*32);v=self.unpack('<8I',here);kind,index,width=v[0],v[1],v[3]
            if kind==0:break
            if index>=len(self.names) or self.names[index] in names or width>4096 or any(v[4:]):raise ValueError('Unsupported member definition')
            name=self.names[index];names.add(name);sub=self.pointer(self.add(here,8))
            if kind==1:
                if sub is None:raise ValueError('Inline type missing')
                subfields=self.fields(sub);size=sum(f.size for f in subfields)
            elif kind in (2,8):size=4
            elif kind in (3,4,5):size=8
            elif kind==7:size=12
            elif kind==9:size=68
            elif kind in (10,19,20):size=4
            elif kind in (11,12,13,14):size=1
            elif kind in (15,16,17,18,21):size=2
            else:raise ValueError(f'Unsupported member type {kind}')
            size*=max(width,1);fields.append(Field(kind,name,sub,width,offset,size));offset+=size
        else:raise ValueError('Type member limit')
        self.parsing.remove(ref);self.type_cache[ref]=tuple(fields);return self.type_cache[ref]
    def read_root_fields(self,names):
        """Decode only explicit root members; original object/value bounds remain.

        Granny exposes TrackGroups both directly and through Animations. Clip
        consumers need only the latter, so decoding both duplicates a large DAG.
        This is projection, not a claim that omitted payloads were validated.
        """
        if not isinstance(names,(tuple,list)) or not 0<len(names)<=32 or len(set(names))!=len(names):
            raise ValueError('Root field selection bound/duplicates')
        fields=self.fields(self.root_type);known={f.name:f for f in fields}
        if any(not isinstance(n,str) or n not in known for n in names):raise ValueError('Missing selected root field')
        self.check(self.root_ref,sum(f.size for f in fields))
        self.objects+=1
        if self.objects>MAX_ITEMS:raise ValueError('Object count limit')
        out={}
        for name in names:
            f=known[name];base=self.add(self.root_ref,f.offset);single=f.size//max(f.width,1)
            values=[self.value(f,self.add(base,n*single),0) for n in range(max(f.width,1))]
            out[name]=values if f.width else values[0]
        return out

    def read(self,type_ref=None,ref=None,depth=0):
        type_ref=self.root_type if type_ref is None else type_ref
        ref=self.root_ref if ref is None else ref
        if depth>96:raise ValueError('Object nesting limit')
        self.objects+=1
        if self.objects>MAX_ITEMS:raise ValueError('Object count limit')
        fields=self.fields(type_ref);self.check(ref,sum(f.size for f in fields))
        out={'_reference':list(ref),'_type':list(type_ref)}
        for f in fields:
            base=self.add(ref,f.offset);single=f.size//max(f.width,1)
            values=[self.value(f,self.add(base,n*single),depth) for n in range(max(f.width,1))]
            out[f.name]=values if f.width else values[0]
        return out
    def value(self,f,ref,depth):
        self.values+=1
        if self.values>1000000:raise ValueError('Decoded value budget')
        k=f.kind
        if k==1:return self.read(f.reference,ref,depth+1)
        if k==2:
            p=self.pointer(ref)
            if p is not None and f.reference is None:raise ValueError('Untyped reference')
            return self.read(f.reference,p,depth+1) if p is not None else None
        if k in (3,4,7):
            typ=f.reference;at=ref
            if k==7:typ=self.pointer(at);at=self.add(at,4)
            count=self.unpack('<I',at)[0];p=self.pointer(self.add(at,4))
            if count>MAX_ITEMS or bool(count)!=(p is not None):raise ValueError('Array count/pointer')
            if not count:return []
            if typ is None:raise ValueError('Untyped array')
            stride=4 if k==4 else sum(x.size for x in self.fields(typ));self.check(p,count*stride)
            result=[]
            for n in range(count):
                item=self.add(p,stride*n)
                if k==4:item=self.pointer(item)
                result.append(self.read(typ,item,depth+1) if item is not None else None)
            return result
        if k==5:
            typ=self.pointer(ref);p=self.pointer(self.add(ref,4))
            if (typ is None)!=(p is None):raise ValueError('Incomplete variant')
            return self.read(typ,p,depth+1) if typ is not None else None
        if k==8:return self.string(ref)
        if k==9:
            words=self.unpack('<I16f',ref)
            if words[0]&~7 or not all(math.isfinite(v) for v in words[1:]):raise ValueError('Transform data')
            return {'flags':words[0],'position':list(words[1:4]),'orientation':list(words[4:8]),'scale_shear':list(words[8:17])}
        fmt={10:'f',11:'b',12:'B',13:'b',14:'B',15:'h',16:'H',17:'h',18:'H',19:'i',20:'I',21:'e'}[k]
        result=self.unpack('<'+fmt,ref)[0]
        if isinstance(result,float) and not math.isfinite(result):raise ValueError('Nonfinite scalar')
        return result
