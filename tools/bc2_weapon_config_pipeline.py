"""Bounded offline BC2 binary DBX trees and exact weapon/config references.

No process access, native writes, geometry admission, or runtime installation.
Only explicit scalar schemas are interpreted; other byte arrays stay opaque.
"""
from __future__ import annotations
import argparse
from dataclasses import dataclass
import json
import math
from pathlib import Path
import struct
import uuid
from inspect_bc2_mesh_asset import Archive, sha
from bc2_weapon_asset_pipeline import dbx_dictionary

@dataclass(frozen=True)
class Value:
    kind: int
    width: int
    count: int
    data: bytes | str

@dataclass(frozen=True)
class Node:
    tag: str
    attributes: dict[str,str]
    value: Value
    children: tuple['Node',...]
    offset: int

@dataclass(frozen=True)
class Document:
    resource: str
    sha256: str
    root: Node
    instances: dict[str,Node]

class Reader:
    def __init__(self,data,names,start):
        self.data=data;self.names=names;self.at=start;self.nodes=0
    def take(self,n):
        if n<0 or self.at+n>len(self.data):raise ValueError('Truncated DBX value')
        result=self.data[self.at:self.at+n];self.at+=n;return result
    def integer(self):
        result=0
        for n in range(5):
            b=self.take(1)[0]
            if n==4 and b>15:raise ValueError('DBX integer overflow')
            result|=(b&127)<<(7*n)
            if not b&128:
                if n and result<1<<(7*n):raise ValueError('Noncanonical DBX integer')
                return result
        raise ValueError('DBX integer overflow')
    def text(self,index):
        if index>=len(self.names):raise ValueError('DBX string index outside dictionary')
        return self.names[index]
    def node(self,depth=0):
        if depth>128:raise ValueError('DBX nesting limit')
        offset=self.at;name=self.integer()
        if not name:return None
        self.nodes+=1
        if self.nodes>100000:raise ValueError('DBX node limit')
        tag=self.text(name);flags=self.take(1)[0];attributes={}
        for _ in range(flags&15):
            key=self.text(self.integer());value=self.text(self.integer())
            if not key or key in attributes:raise ValueError('Empty/duplicate DBX attribute')
            attributes[key]=value
        kind=(flags>>4)&7
        if kind==2:value=Value(kind,0,1,self.text(self.integer()))
        elif kind in (6,7):
            count=self.integer();width=1 if kind==6 else self.integer()
            if not 0<width<=16 or count>1000000 or count*width>16*1024*1024:
                raise ValueError('DBX value array bound')
            value=Value(kind,width,count,self.take(count*width))
        else:raise ValueError('Unsupported DBX value kind')
        children=[]
        if flags&128:
            if kind!=2:raise ValueError('Unexpected binary-valued DBX container')
            while True:
                child=self.node(depth+1)
                if child is None:break
                children.append(child)
        return Node(tag,attributes,value,tuple(children),offset)

def guid(value):
    try:return str(uuid.UUID(value))
    except (ValueError,TypeError,AttributeError) as exc:raise ValueError('Invalid instance GUID') from exc

def parse(resource,data):
    if len(data)>16*1024*1024:raise ValueError('DBX resource size bound')
    names=dbx_dictionary(data);reader=Reader(data,names,struct.unpack_from('>I',data,8)[0])
    root=reader.node()
    if root is None or root.tag!='partition' or reader.at!=len(data):raise ValueError('DBX root or trailing data')
    guid(root.attributes.get('guid'));primary=guid(root.attributes.get('primaryInstance'))
    instances={}
    for child in root.children:
        if child.tag!='instance':raise ValueError('Unknown partition child')
        key=guid(child.attributes.get('guid'))
        if key in instances:raise ValueError('Duplicate instance GUID')
        if not child.attributes.get('type'):raise ValueError('Instance type missing')
        instances[key]=child
    if primary not in instances:raise ValueError('Primary instance missing')
    return Document(resource,sha(data),root,instances)

def named(node,name):
    matches=[child for child in node.children if child.attributes.get('name')==name]
    if len(matches)!=1:raise ValueError('Missing/ambiguous field: '+name)
    return matches[0]

def scalar(node,kind):
    v=node.value
    if node.children:raise ValueError('Scalar has children')
    if kind=='string':
        if v.kind!=2 or not isinstance(v.data,str):raise ValueError('Expected dictionary string')
        return v.data
    if kind=='bool':
        if v.kind!=6 or v.width!=1 or v.count!=1 or v.data not in (b'\0',b'\1'):raise ValueError('Expected strict boolean')
        return bool(v.data[0])
    if kind not in ('int32','float32') or v.kind!=7 or v.width!=4 or v.count!=1 or not isinstance(v.data,bytes):
        raise ValueError('Expected one explicit 32-bit scalar')
    result=struct.unpack('>i' if kind=='int32' else '>f',v.data)[0]
    if kind=='float32' and not math.isfinite(result):raise ValueError('Nonfinite scalar')
    return result

class Resolver:
    def __init__(self,documents):
        self.documents={}
        for doc in documents:
            if not doc.resource.lower().endswith('.dbx'):raise ValueError('Document extension')
            key=doc.resource[:-4].lower()
            if key in self.documents:raise ValueError('Ambiguous resource path')
            self.documents[key]=doc
    def resolve(self,origin,reference,expected_type):
        if not reference or reference=='null':raise ValueError('Absent reference')
        if '/' in reference:
            path,identifier=reference.rsplit('/',1)
            if '\\' in path or any(x in ('','..','.') for x in path.split('/')):raise ValueError('Invalid resource reference')
            target=self.documents.get(path.lower())
            if target is None:raise ValueError('Missing referenced document: '+path)
        else:target=origin;identifier=reference
        instance=target.instances.get(guid(identifier))
        if instance is None:raise ValueError('Missing referenced GUID')
        if instance.attributes['type']!=expected_type:raise ValueError('Referenced instance type mismatch')
        return target,instance

SCALARS={
 'FireLogic.ReloadLogic':'string','FireLogic.ReloadType':'string',
 'FireLogic.ReloadTime':'float32','FireLogic.ReloadDelay':'float32',
 'FireLogic.ReloadThreshold':'float32','FireLogic.PostReloadSequenceTime':'float32',
 'FireLogic.RateOfFire':'float32','FireLogic.FireLogicType':'string',
 'FireLogic.FireInputAction':'string','FireLogic.ReloadInputAction':'string',
 'FireLogic.BoltAction.BoltActionDelay':'float32','FireLogic.BoltAction.BoltActionTime':'float32',
 'FireLogic.BoltAction.HoldBoltActionUntilFireRelease':'bool',
 'FireLogic.BoltAction.HoldBoltActionUntilZoomRelease':'bool',
 'Ammo.MagazineCapacity':'int32','Ammo.NumberOfMagazines':'int32',
 'Ammo.AmmoPickupMinAmount':'int32','Ammo.AmmoPickupMaxAmount':'int32',
 'Ammo.AutoReplenishMagazine':'bool','Shot.NumberOfBulletsPerShell':'int32',
 'Shot.NumberOfBulletsPerShot':'int32',
}

def effective_weapon(resolver,document,weapon):
    if weapon.attributes['type']!='GameSharedResources.SoldierWeaponData':raise ValueError('Not SoldierWeaponData')
    firing_document,firing=resolver.resolve(document,named(weapon,'WeaponFiring').attributes.get('ref'),'GameSharedResources.WeaponFiringData')
    function_document,function=resolver.resolve(firing_document,named(firing,'PrimaryFire').attributes.get('ref'),'GameSharedResources.FiringFunctionData')
    fields={};missing=[]
    for path,kind in SCALARS.items():
        node=function
        try:
            for segment in path.split('.'):node=named(node,segment)
            fields[path]=dict(value=scalar(node,kind),encoding=kind,offset=node.offset)
        except ValueError as exc:missing.append(dict(field=path,reason=str(exc)))
    abort=scalar(named(firing,'AbortReloadOnSprint'),'bool');states=[]
    for index,state in enumerate(named(weapon,'WeaponStates').children):
        values={};state_missing=[]
        for key,kind in {'IsPumpAction':'bool','IsOneHanded':'bool','AnimatedFireType':'string','AnimatedAimingType':'string','SkipReloadAnimation':'bool'}.items():
            try:values[key]=scalar(named(state,key),kind)
            except ValueError as exc:state_missing.append(dict(field=key,reason=str(exc)))
        try:
            meshes=[item.attributes['ref'] for item in named(state,'Meshes1p').children if item.tag=='item']
        except (ValueError,KeyError) as exc:meshes=[];state_missing.append(dict(field='Meshes1p',reason=str(exc)))
        animation_refs={}
        for key in ('AnimTree1p','AnimTree3p'):
            try:
                reference=named(state,key).attributes.get('ref')
                if not isinstance(reference,str) or not reference:raise ValueError('Explicit animation tree reference absent')
                animation_refs[key]=reference
            except ValueError as exc:state_missing.append(dict(field=key,reason=str(exc)))
        states.append(dict(index=index,mesh_asset_references=meshes,animation_tree_references=animation_refs,
            fields=values,missing=state_missing))
    return dict(native_name=scalar(named(weapon,'Name'),'string'),weapon_class=scalar(named(weapon,'WeaponClass'),'string'),
        resource=document.resource,resource_sha256=document.sha256,instance_guid=weapon.attributes['guid'],
        firing_resource=firing_document.resource,firing_sha256=firing_document.sha256,firing_guid=firing.attributes['guid'],
        function_resource=function_document.resource,function_sha256=function_document.sha256,function_guid=function.attributes['guid'],
        fields=fields,weapon_states=states,abort_reload_on_sprint=abort,missing_fields=missing,
        runtime_admitted=False,scope='Exact authored data/reference chain, not a native object ABI or current live configuration')

def inspect(root,relative):
    path=(root/relative).resolve()
    if not path.is_relative_to(root.resolve()):raise ValueError('Archive leaves game root')
    archive=Archive(path)
    entries=[e for e in archive.entries if e.flags==65536 and e.name.lower().startswith('objects/weapons/handheld/')
             and e.name.endswith('.dbx') and not any(s in e.name.lower() for s in ('animtree','mesh','aiclones/'))]
    documents=[];errors=[]
    for name,data in archive.read_selected([e.name for e in entries]).items():
        try:documents.append(parse(name,data))
        except ValueError as exc:errors.append(dict(resource=name,sha256=sha(data),reason=str(exc)))
    resolver=Resolver(documents);weapons=[];unresolved=[]
    for doc in documents:
        for instance in doc.instances.values():
            if instance.attributes['type']!='GameSharedResources.SoldierWeaponData':continue
            try:weapons.append(effective_weapon(resolver,doc,instance))
            except ValueError as exc:unresolved.append(dict(resource=doc.resource,sha256=doc.sha256,instance=instance.attributes,reason=str(exc)))
    return dict(schema='fvr.bc2.authored_weapon_configuration',schema_version=1,archive=relative.as_posix(),index_sha256=archive.index_sha256,
        parsed_documents=len(documents),parse_errors=errors,resolved_weapons=weapons,unresolved_weapons=unresolved,
        limits=['No runtime/native configuration admission.','No implicit inheritance/default substitution. Missing references/fields remain explicit.',
                'Typed interpretation is restricted to the explicit field schema; other binary values stay opaque.',
                'Live campaign modifiers and current WeaponFiringData identity require native corroboration.'])

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--game',type=Path,required=True)
    parser.add_argument('--archive',type=Path,default=Path('Dist/win32/levels/sp_common/level-00.fbrb'))
    parser.add_argument('--output',type=Path,required=True);args=parser.parse_args(argv)
    result=inspect(args.game,args.archive);args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    print(json.dumps(dict(documents=result['parsed_documents'],errors=len(result['parse_errors']),resolved=len(result['resolved_weapons']),unresolved=len(result['unresolved_weapons']))))
    return 0
if __name__=='__main__':raise SystemExit(main())
