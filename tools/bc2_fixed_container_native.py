"""Exact fixed-container reload descriptors using the reviewed shared Transfer path.

Data selection does not grant helper ownership or count-write authority. The
runtime still verifies the executable binding, owner and all three Updates.
"""
import argparse
import json
from pathlib import Path
import bc2_magazine_descriptor_manifest as descriptor
import bc2_singlefire_magazine_proof as single
import bc2_boltaction_magazine_proof as stock
from bc2_magazine_registry_header import ORDER


def generate(rows):
    keys=set()
    jobs={'schema':'fvr.bc2.magazine_descriptor_jobs','schema_version':1,
          'runtime_enabled':False,'registry_change':False,
          'descriptors':[descriptor.descriptor(row) for row in rows]}
    jobs=stock.apply(single.apply(jobs,single.EXPECTED_PROOF),stock.EXPECTED_PROOF)
    lines=['#pragma once','#include "Bc2ReloadConfigDescriptor.h"','namespace fvr::bc2 {']
    for i,row in enumerate(jobs['descriptors']):
        c=row['configuration'];v=c['values'];key=(c['assetName'],c['assetPath'])
        if key in keys or row['deferred_reasons'] or v['fireLogicType']!=0 or v['reloadType']!=1:
            raise ValueError('Exact finite SingleFire fixed-container descriptor required')
        keys.add(key)
        if c['assetName'] not in ('MP412','Garand'):
            raise ValueError('Unreviewed fixed-container model')
        words=','.join('{0x%x,0x%08xu}'%(w['offset'],w['expected']) for w in c['timing'])
        if len(c['timing'])!=6:raise ValueError('Exact six-word timing required')
        lines.append(f'inline constexpr std::array<ReloadTimingWord,6> FixedContainerTiming{i}{{{{{words}}}}};')
        values=','.join(('true' if v[k] else 'false') if type(v[k]) is bool else
            f'std::bit_cast<float>(0x{descriptor.bits(v[k])}u)' if k in ORDER[6:12] else str(v[k]) for k in ORDER)
        lines.append(f'inline constexpr ReloadConfigDescriptor FixedContainerConfig{i}{{{json.dumps(c["assetName"])},{json.dumps(c["assetPath"])},{{{values}}},FixedContainerTiming{i},ReloadDescriptorAdmission::ReviewedNative}};')
    lines.append(f'inline constexpr std::array<const ReloadConfigDescriptor*,{len(keys)}> FixedContainerConfigs{{{{'+','.join(f'&FixedContainerConfig{i}' for i in range(len(keys)))+'}};')
    lines.extend(['inline const ReloadConfigDescriptor* ResolveFixedContainerNative(const ReloadObservedConfig& c)noexcept {',
        ' const ReloadConfigDescriptor* found=nullptr;for(const auto* row:FixedContainerConfigs)if(MatchesReloadDescriptor(c,*row)){if(found)return nullptr;found=row;}return found;',
        '}', 'inline bool FixedContainerNativeConfig(const ReloadObservedConfig& c)noexcept{return ResolveFixedContainerNative(c)!=nullptr;}',
        'inline bool ReadFixedContainerNativeConfig(const ReloadStateMemory& m,const ReloadObservedConfig& c)noexcept {',
        ' const auto* row=ResolveFixedContainerNative(c);return row&&ReadReloadDescriptorTiming(m,c,*row);',
        '}', '} // namespace fvr::bc2',''])
    return '\n'.join(lines),jobs


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--catalog',type=Path,required=True)
    p.add_argument('--extra-native',type=Path)
    p.add_argument('--header',type=Path,required=True)
    p.add_argument('--receipt',type=Path,required=True)
    a=p.parse_args();catalog=json.loads(a.catalog.read_text(encoding='utf-8-sig'))
    rows=[r for r in catalog['configurations'] if r['native_name'] in ('MP412','Garand')]
    if a.extra_native:
        extra=json.loads(a.extra_native.read_text(encoding='utf-8-sig'))
        rows.extend(extra.get('configurations',extra.get('records',[])))
    unique={}
    for row in rows:
        key=(row['resource'],row['instance_guid'].lower())
        if key in unique and descriptor.descriptor(unique[key])['descriptor_digest']!=descriptor.descriptor(row)['descriptor_digest']:
            raise ValueError('Conflicting exact source configuration')
        unique[key]=row
    header,jobs=generate([unique[k] for k in sorted(unique)])
    a.header.parent.mkdir(parents=True,exist_ok=True);a.receipt.parent.mkdir(parents=True,exist_ok=True)
    a.header.write_text(header,encoding='utf-8')
    a.receipt.write_text(json.dumps({'schema':'fvr.bc2.fixed_container_native_review.v1',
        'evidence_level':'static shared instruction/configuration review; no native or headset success',
        'singlefire_instruction_proof':single.PROOF_DIGEST,'inactive_bolt_instruction_proof':stock.PROOF_DIGEST,
        'jobs':jobs},indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'exact_configurations':len(unique),'native_execution':False}))

if __name__=='__main__':main()
