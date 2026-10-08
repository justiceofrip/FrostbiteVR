"""Compile immutable optional magazine rows from exact descriptor jobs.

No row is enabled without a separate exact-key/digest review list. The only
currently supported review is the existing AutomaticFire/rtMagazine/zero-bolt
implementation. Unknown dispatch remains data, never guessed C++ enum values.
This tool performs no process, game, geometry, ammo or native operations.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import bc2_magazine_descriptor_manifest as descriptor

FAMILY = 'bc2.automatic-magazine-zero-bolt.v1'
ORDER = ('fireLogicType','reloadType','fireInputAction','reloadInputAction','baseCapacity','numberOfMagazines',
         'reloadDelay','reloadTime','reloadThreshold','postReloadTime','boltDelay','boltTime',
         'holdBoltUntilFireRelease','holdBoltUntilZoomRelease')

def stable_id(value):
    value=descriptor.sha(value)
    result=int(value[:16],16)
    if result<=1:raise ValueError('Digest collides with a compatibility key')
    return result

def eligible(row):
    c=row['configuration'];v=c['values']
    if c.get('admission')!='Candidate' or set(v)!=set(ORDER):return False
    if any(type(v[k]) is not int for k in ORDER[:6]) or any(type(v[k]) is not bool for k in ORDER[-2:]):return False
    if [v[k] for k in ORDER[:4]]!=[2,1,8,29]:return False
    if not 0<v['baseCapacity']<=1000000 or not 0<v['numberOfMagazines']<=1000000:return False
    if any(descriptor.f32(v[k])!=v[k] for k in ORDER[6:12]):return False
    if any(v[k]!=0 for k in ('boltDelay','boltTime','holdBoltUntilFireRelease','holdBoltUntilZoomRelease')):
        from bc2_automatic_stock_bolt_proof import candidate_dispatch
        if not candidate_dispatch(row):return False
    if not 0<v['reloadTime']<=10 or not 0<v['reloadThreshold']<=1 or not 0<=v['reloadDelay']<=10 or not 0<=v['postReloadTime']<=10:return False
    expected=[(0x10,int(descriptor.bits(v['reloadThreshold']),16)),(0x14,int(descriptor.bits(v['reloadDelay']),16)),
              (0x18,int(descriptor.bits(v['reloadTime']),16)),(0x20,1),(0x24,2),(0x2c,0)]
    if c['timing']!=[dict(offset=o,expected=w,expected_bits=f'{w:08x}',comparison='Bits') for o,w in expected]:return False
    deadline=row['proposed_completion_deadline_ns']
    expected_deadline=descriptor.math.ceil((v['reloadDelay']+v['reloadTime']+v['postReloadTime'])*1e9)+700000000
    return type(deadline) is int and deadline==expected_deadline and row.get('completion_margin_ns')==700000000 and 0<deadline<=10000000000 and not row.get('deferred_reasons') and row.get('same_reviewed_dispatch_shape') is True

def build(jobs,reviews=None):
    if jobs.get('schema')!='fvr.bc2.magazine_descriptor_jobs' or jobs.get('schema_version')!=1:raise ValueError('Expected descriptor jobs schema1')
    rows=jobs.get('descriptors');reviews=[] if reviews is None else reviews
    if not isinstance(rows,list) or len(rows)>256 or not isinstance(reviews,list) or len(reviews)>256:raise ValueError('Registry row bound')
    approved={}
    for review in reviews:
        if not isinstance(review,dict) or review.get('family_proof')!=FAMILY:raise ValueError('Unreviewed dispatch family')
        key=descriptor.text(review.get('key'),256)
        if key in approved:raise ValueError('Duplicate review')
        approved[key]=(descriptor.sha(review.get('descriptor_digest')),descriptor.sha(review.get('evidence_sha256')))
    emitted=[];seen_id={};seen_path=set();seen_key=set();matched=set()
    for row in rows:
        key=descriptor.text(row.get('key'),256);c=row['configuration'];digest=descriptor.sha(row.get('descriptor_digest'))
        if descriptor.digest(c)!=digest:raise ValueError('Descriptor digest does not describe consumed configuration')
        identity=row.get('identity')
        if not isinstance(identity,dict) or key!='bc2:magazine-descriptor:'+descriptor.digest(identity) or any(identity.get(k)!=c.get(k) for k in ('assetName','assetPath')):
            raise ValueError('Exact source identity does not match configuration/key')
        ident=(descriptor.text(c.get('assetName'),64),descriptor.resource(c.get('assetPath')+'.dbx'))
        if key in seen_key or ident in seen_path:raise ValueError('Ambiguous exact native identity')
        seen_key.add(key);seen_path.add(ident)
        id_=stable_id(digest)
        if id_ in seen_id:raise ValueError('Truncated digest collision')
        seen_id[id_]=digest
        requested=key in approved
        if requested:
            if row.get('automatic_stock_bolt_proof_digest'):
                raise ValueError('Stock-bolt candidates cannot be enabled by a zero-bolt family review')
            if approved[key][0]!=digest or not eligible(row):raise ValueError('Review does not match eligible exact descriptor')
            if row.get('existing_baseline_reference'):raise ValueError('Existing compatibility registration must not be overridden')
            matched.add(key)
        # Unknown numeric dispatch cannot be materialized by substituting zero.
        if not eligible(row):continue
        if row.get('existing_baseline_reference'):continue
        emitted.append((row,id_,requested))
    if matched!=set(approved):raise ValueError('Review key not found')
    lines=['#pragma once','#include "Bc2MagazineNativeProfile.h"','namespace fvr::bc2::generated {']
    for index,(row,id_,enabled) in enumerate(emitted):
        c=row['configuration'];v=c['values']
        def literal(k):
            x=v[k]
            if type(x) is bool:return 'true' if x else 'false'
            if k in ORDER[6:12]:return f'std::bit_cast<float>(0x{descriptor.bits(x)}u)'
            return str(x)
        words=','.join('{0x%x,0x%08xu}'%(w['offset'],w['expected']) for w in c['timing'])
        lines.append(f'inline constexpr std::array<ReloadTimingWord,6> RegistryTiming{index}{{{{{words}}}}};')
        admission='ReviewedNative' if enabled else 'Candidate';cycle='ReviewedReload11Transfer12' if enabled else 'Candidate'
        values=','.join(literal(k) for k in ORDER)
        lines.append(f'inline constexpr MagazineNativeProfile RegistryProfile{index}{{{{{json.dumps(c["assetName"])},{json.dumps(c["assetPath"])},{{{values}}},RegistryTiming{index},ReloadDescriptorAdmission::{admission}}},MagazineIdentityRoute::SelectedCarriedItem,MagazineCycleAdmission::{cycle},{row["proposed_completion_deadline_ns"]}ll}};')
    lines.append(f'inline constexpr std::array<MagazineNativeRegistration,{len(emitted)}> MagazineNativeRegistrations{{{{')
    for index,(row,id_,enabled) in enumerate(emitted):
        lines.append(f' {{static_cast<NativeMagazineProfileId>(0x{id_:016x}ull),&RegistryProfile{index},"{row["descriptor_digest"]}",{str(enabled).lower()}}},')
    lines+=['}};','} // namespace fvr::bc2::generated','']
    return '\n'.join(lines),{'rows':len(emitted),'enabled':sum(x[2] for x in emitted),'omitted_unreviewed_or_builtin':len(rows)-len(emitted)}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--jobs',type=Path,required=True)
    p.add_argument('--reviewed',type=Path);p.add_argument('--header',type=Path,required=True);p.add_argument('--receipt',type=Path,required=True);a=p.parse_args()
    jobs,jobs_hash=descriptor.load(a.jobs);reviews=None;review_hash=None
    if a.reviewed:reviews,review_hash=descriptor.load(a.reviewed)
    header,summary=build(jobs,reviews);raw=header.encode('utf-8')
    a.header.parent.mkdir(parents=True,exist_ok=True);a.header.write_bytes(raw)
    receipt={'schema':'fvr.bc2.magazine_registry_header','schema_version':1,'input_sha256':{'jobs':jobs_hash,'reviewed':review_hash},
             'header_sha256':descriptor.hashlib.sha256(raw).hexdigest(),'summary':summary,'native_test_claim':False}
    a.receipt.parent.mkdir(parents=True,exist_ok=True);a.receipt.write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(summary,sort_keys=True))
if __name__=='__main__':main()
