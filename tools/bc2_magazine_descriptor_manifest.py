"""Export disabled ReloadConfigDescriptor-shaped jobs from the existing catalog.

No binary parsing, runtime registration, hooks, process access or native grants.
An optional reviewed-baseline document describes existing registrations only.
It never promotes a candidate. Identical SP/MP content keeps both origins.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path, PurePosixPath
import struct
import uuid

VALUE_FIELDS = {
    'baseCapacity': ('Ammo.MagazineCapacity', 'int'),
    'numberOfMagazines': ('Ammo.NumberOfMagazines', 'int'),
    'reloadDelay': ('FireLogic.ReloadDelay', 'float'),
    'reloadTime': ('FireLogic.ReloadTime', 'float'),
    'reloadThreshold': ('FireLogic.ReloadThreshold', 'float'),
    'postReloadTime': ('FireLogic.PostReloadSequenceTime', 'float'),
    'boltDelay': ('FireLogic.BoltAction.BoltActionDelay', 'float'),
    'boltTime': ('FireLogic.BoltAction.BoltActionTime', 'float'),
    'holdBoltUntilFireRelease': ('FireLogic.BoltAction.HoldBoltActionUntilFireRelease', 'bool'),
    'holdBoltUntilZoomRelease': ('FireLogic.BoltAction.HoldBoltActionUntilZoomRelease', 'bool'),
}
ENUM_FIELDS = {
    'fireLogicType': ('FireLogic.FireLogicType', 'fltAutomaticFire', 2),
    'reloadType': ('FireLogic.ReloadType', 'rtMagazine', 1),
    'fireInputAction': ('FireLogic.FireInputAction', 'EiaFire', 8),
    'reloadInputAction': ('FireLogic.ReloadInputAction', 'EiaReload', 29),
}
CLASSES = frozenset(('wcAssault', 'wcSmg', 'wcLmg', 'wcPistol', 'wcSniper'))

def digest(value):
    return hashlib.sha256(json.dumps(value,sort_keys=True,separators=(',',':'),allow_nan=False).encode()).hexdigest()

def text(value,limit):
    if not isinstance(value,str) or not value or '\0' in value or len(value.encode('utf-8'))>=limit:
        raise ValueError('Invalid bounded identity text')
    return value

def resource(value):
    value=text(value,192)
    p=PurePosixPath(value)
    if '\\' in value or ':' in value or p.is_absolute() or '..' in p.parts or str(p)!=value or not value.endswith('.dbx'):
        raise ValueError('Invalid exact DBX resource path')
    return value

def sha(value):
    if not isinstance(value,str) or len(value)!=64 or any(c not in '0123456789abcdef' for c in value):
        raise ValueError('Invalid source SHA256')
    return value

def f32(value):
    if type(value) not in (int,float) or not math.isfinite(value):
        raise ValueError('Invalid finite float32')
    try: result=struct.unpack('<f',struct.pack('<f',value))[0]
    except (OverflowError,struct.error) as exc: raise ValueError('Float32 overflow') from exc
    if not math.isfinite(result): raise ValueError('Float32 overflow')
    return result

def bits(value):return f'{struct.unpack("<I",struct.pack("<f",f32(value)))[0]:08x}'

def observed_field(fields,name,kind):
    field=fields.get(name)
    if not isinstance(field,dict) or 'value' not in field:raise ValueError('Missing exact field: '+name)
    value=field['value']
    if kind=='float':return f32(value)
    if kind=='int':
        if type(value) is not int or not 0<=value<=1000000:raise ValueError('Invalid integer field: '+name)
    elif kind=='bool':
        if type(value) is not bool:raise ValueError('Invalid boolean field: '+name)
    elif not isinstance(value,str):raise ValueError('Invalid enum field: '+name)
    return value

def descriptor(row):
    name=text(row.get('native_name'),64);path=resource(row.get('resource'));fields=row.get('fields')
    if not isinstance(fields,dict):raise ValueError('Missing typed fields')
    identity={'assetName':name,'assetPath':path[:-4],'weapon':{'resource':path,'sha256':sha(row.get('resource_sha256')),
        'guid':str(uuid.UUID(row.get('instance_guid')))}}
    for kind,prefix in [('firing','firing'),('function','function')]:
        identity[kind]={'resource':resource(row.get(prefix+'_resource')),'sha256':sha(row.get(prefix+'_sha256')),
            'guid':str(uuid.UUID(row.get(prefix+'_guid')))}
    values={};deferred=[];symbols={}
    for target,(field,expected,numeric) in ENUM_FIELDS.items():
        actual=observed_field(fields,field,'enum');symbols[field]=actual
        if actual!=expected:deferred.append('unreviewed_dispatch_symbol:'+field+':'+actual)
        else:values[target]=numeric
    for target,(field,kind) in VALUE_FIELDS.items():values[target]=observed_field(fields,field,kind)
    logic=observed_field(fields,'FireLogic.ReloadLogic','enum');symbols['FireLogic.ReloadLogic']=logic
    if logic!='rlWeaponSwitchCancelsUnfinishedReload':deferred.append('unreviewed_reload_logic:'+logic)
    if values['baseCapacity']<=0 or values['numberOfMagazines']<=0:deferred.append('nonfinite_ammunition_configuration')
    if not 0<values['reloadTime']<=10 or not 0<values['reloadThreshold']<=1 or any(values[k]<0 or values[k]>10 for k in ['reloadDelay','postReloadTime','boltDelay','boltTime']):
        deferred.append('timing_outside_existing_finite_profile_bounds')
    if values['boltDelay'] or values['boltTime'] or values['holdBoltUntilFireRelease'] or values['holdBoltUntilZoomRelease']:
        deferred.append('authored_bolt_fields_require_shared_dispatch_review')
    # Reuse the existing six-word magazine descriptor layout. Unknown enum/logic
    # symbols never receive guessed numeric words or an eligible descriptor.
    timing=[]
    if len([k for k in ENUM_FIELDS if k in values])==len(ENUM_FIELDS) and logic=='rlWeaponSwitchCancelsUnfinishedReload':
        timing=[{'offset':offset,'expected':int(word,16),'expected_bits':word,'comparison':'Bits'} for offset,word in [
            (0x10,bits(values['reloadThreshold'])),(0x14,bits(values['reloadDelay'])),
            (0x18,bits(values['reloadTime'])),(0x20,f'{values["reloadType"]:08x}'),
            (0x24,f'{values["fireLogicType"]:08x}'),(0x2c,'00000000')]]
    allowance=math.ceil((values['reloadDelay']+values['reloadTime']+values['postReloadTime'])*1e9)+700000000
    if not 0<allowance<=10000000000:deferred.append('completion_allowance_exceeds_existing_bound')
    config={'assetName':name,'assetPath':path[:-4],'values':values,'timing':timing,'admission':'Candidate'}
    origin={'archive':text(row.get('origin_archive'),512),'index_sha256':sha(row.get('origin_index_sha256'))}
    return {'key':'bc2:magazine-descriptor:'+digest(identity),'identity':identity,'configuration':config,
        'authored_symbols':symbols,'source_class':row.get('weapon_class'),'origins':[origin],
        'same_reviewed_dispatch_shape':not deferred,'deferred_reasons':sorted(set(deferred)),
        'proposed_completion_deadline_ns':allowance,'completion_margin_ns':700000000,
        'cycle_admission':'Candidate','runtime_enabled':False,'registry_change':False,
        'descriptor_digest':digest(config)}

def build(catalog,baselines=None):
    if catalog.get('schema')!='fvr.bc2.manual_reload_catalog' or catalog.get('schema_version')!=1:
        raise ValueError('Expected existing manual reload catalog schema1')
    rows=catalog.get('configurations')
    if not isinstance(rows,list) or len(rows)>2048:raise ValueError('Configuration count bound')
    unique={};rejected=[]
    for row in rows:
        if not isinstance(row,dict):raise ValueError('Invalid configuration row')
        if row.get('weapon_class') not in CLASSES:continue
        try:d=descriptor(row)
        except (ValueError,TypeError,KeyError,AttributeError) as exc:
            rejected.append({'native_name':row.get('native_name'),'resource':row.get('resource'),'reason':str(exc)});continue
        old=unique.get(d['key'])
        if old:
            if old['configuration']!=d['configuration'] or old['authored_symbols']!=d['authored_symbols']:
                old['deferred_reasons'].append('conflicting_content_for_exact_identity');old['same_reviewed_dispatch_shape']=False
            for origin in d['origins']:
                if origin not in old['origins']:old['origins'].append(origin)
        else:unique[d['key']]=d
    # Different resource paths are valid variants; differing content behind the
    # same observed name/path is not selected by a first-match fallback.
    groups={}
    for d in unique.values():groups.setdefault((d['configuration']['assetName'],d['configuration']['assetPath']),[]).append(d)
    for group in groups.values():
        if len(group)>1:
            for d in group:d['deferred_reasons'].append('conflicting_content_for_exact_native_path');d['same_reviewed_dispatch_shape']=False
    baseline_rows=[] if baselines is None else baselines
    if not isinstance(baseline_rows,list) or len(baseline_rows)>64:raise ValueError('Baseline count bound')
    references=[];baseline_keys=set()
    for b in baseline_rows:
        key=text(b.get('key'),256)
        if key in baseline_keys:raise ValueError('Duplicate reviewed baseline')
        baseline_keys.add(key);d=unique.get(key)
        if not d or d['deferred_reasons'] or sha(b.get('descriptor_digest'))!=d['descriptor_digest']:
            raise ValueError('Reviewed baseline does not match exact existing descriptor')
        references.append({'key':key,'descriptor_digest':d['descriptor_digest'],
            'registry_source_sha256':sha(b.get('registry_source_sha256')),
            'descriptor_source_sha256':sha(b.get('descriptor_source_sha256')),
            'status':'existing_reviewed_registration_reference','runtime_change':False})
    entries=sorted(unique.values(),key=lambda x:x['key'])
    for d in entries:
        d['origins'].sort(key=lambda x:(x['archive'],x['index_sha256']))
        d['deferred_reasons']=sorted(set(d['deferred_reasons']))
        d['existing_baseline_reference']=d['key'] in baseline_keys
        d['jobs']=([] if d['existing_baseline_reference'] else
            ['exact_configuration_registry_join','independent_grip_and_magazine_geometry','current_native_config_and_cycle_fixture'])
        if d['deferred_reasons']:d['jobs'].insert(0,'resolve_shared_dispatch_or_data_gap')
    return {'schema':'fvr.bc2.magazine_descriptor_jobs','schema_version':1,'runtime_enabled':False,'registry_change':False,
        'existing_baselines':sorted(references,key=lambda x:x['key']),'descriptors':entries,
        'rejected':sorted(rejected,key=lambda x:str((x['native_name'],x['resource'],x['reason']))),
        'summary':{'descriptors':len(entries),'existing_baselines':len(references),'disabled_candidates':len(entries)-len(references),
            'deferred':sum(bool(x['deferred_reasons'])for x in entries),'rejected':len(rejected)},
        'limits':['Candidates are never native admission. Exact-name/path ambiguity is a blocking job.',
            'Class/rtMagazine do not prove physical detachable-magazine or belt semantics.',
            'Authored capacity is not current native capacity; no ammo ledger is emitted.',
            'Completion margin is proposed shared policy data, not a native clock change.',
            'New empty control requires separate actual native acceptance.']}

def load(path):
    # Hash the exact bounded byte sequence decoded, not a second path read.
    limit=128*1024*1024
    with path.open('rb') as stream:data=stream.read(limit+1)
    if len(data)>limit:raise ValueError('Input JSON size bound')
    return json.loads(data.decode('utf-8')),hashlib.sha256(data).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--catalog',type=Path,required=True)
    p.add_argument('--reviewed-baselines',type=Path);p.add_argument('--automatic-stock-bolt-proof',type=Path)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    catalog,catalog_hash=load(a.catalog);baselines=None;baseline_hash=None
    if a.reviewed_baselines:baselines,baseline_hash=load(a.reviewed_baselines)
    result=build(catalog,baselines)
    proof_hash=None
    if a.automatic_stock_bolt_proof:
        from bc2_automatic_stock_bolt_proof import apply
        proof,proof_hash=load(a.automatic_stock_bolt_proof);result=apply(result,proof)
    result['input_sha256']={'catalog':catalog_hash}
    if proof_hash:result['input_sha256']['automatic_stock_bolt_proof']=proof_hash
    if baseline_hash:result['input_sha256']['reviewed_baselines']=baseline_hash
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    print(json.dumps(result['summary'],sort_keys=True))

if __name__=='__main__':main()
