"""Build exact shotgun family data from bounded, already decoded evidence.

Tube reloads use the existing shared shell/pump native dispatch. Magazine-fed
shotguns produce ordinary magazine jobs for the existing registry exporter.
Configuration, native-family review and physical geometry remain separate.
No archives, game processes, native memory, assets or ammunition are modified.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import bc2_magazine_descriptor_manifest as descriptor
import bc2_singlefire_magazine_proof as singlefire

FAMILY = 'bc2.singlebullet-pump-reload11-transfer12.v1'
ORDER = ('fireLogicType', 'reloadType', 'fireInputAction', 'reloadInputAction',
         'baseCapacity', 'numberOfMagazines', 'reloadDelay', 'reloadTime',
         'reloadThreshold', 'postReloadTime', 'boltDelay', 'boltTime',
         'holdBoltUntilFireRelease', 'holdBoltUntilZoomRelease')
BASELINE = ('SPAS12_sp', 'Objects/Weapons/Handheld/UL_shg_SPAS12/SP_shg_SPAS12')
BASELINE_VALUES = (1, 0, 8, 29, 4, 4, .06, .72, 1., 1., .5, 0., False, False)
NATIVE_PROOF = {
    'schema': 'fvr.bc2.shotgun_shared_dispatch.v1',
    'executable_sha256': singlefire.EXPECTED_PROOF['executable_sha256'],
    'fire_logic_enum_proof_digest': singlefire.PROOF_DIGEST,
    'native_function_fingerprints': {
        'Update': '4a6ecc3d95700d53', 'Step': '71118b4d657473d3',
        'Timed': '7344b32b39a9bd85', 'Order': '3cae7f07de53d0f7',
        'Transfer': 'd83485215d90a16a'},
    'native_source_symbols': {
        'layout': 'Bc2ReloadState.cpp::ReadConfig',
        'caller_table': 'Bc2ReloadFlow.cpp::DiscoverReloadFlow',
        'existing_shell': 'Bc2ReloadConfigDescriptor.h::SpasReloadDescriptor',
        'existing_cycle': 'Bc2NativeCycleService.cpp::SpasBoundary'},
    'object_chain': {'weapon_to_SoldierWeaponData': 4,
                     'SoldierWeaponData_to_WeaponFiringData': 0x98,
                     'WeaponFiringData_to_FiringFunctionData': 0x40,
                     'FiringFunctionData_to_ammo': 0x170},
    'primary_fire_offsets': {
        'postReloadTime': 0x0c, 'reloadThreshold': 0x10, 'reloadDelay': 0x14,
        'reloadTime': 0x18, 'reloadType': 0x20, 'fireLogicType': 0x24,
        'fireInputAction': 0x4c, 'boltTime': 0x50, 'boltDelay': 0x54,
        'holdBoltUntilZoomRelease': 0x58, 'holdBoltUntilFireRelease': 0x59,
        'reloadInputAction': 0x8c, 'numberOfMagazines': 0x180,
        'baseCapacity': 0x184},
    'tube_dispatch': [1, 0, 8, 29],
    'reload_hold': {'current': 11, 'next': 12, 'maximum_timer': 1.},
    'pump_hold': {'current': 7, 'previous': 6, 'next': 8, 'maximum_timer': .5},
    'empty_hold': {'current': 6, 'previous': 5, 'next': 1, 'maximum_timer': 1.},
    'ordinary_transfer_return': 'verified Step + 0x561',
    'shell_delta': {'loaded': 1, 'reserve': -1},
    'native_runtime_acceptance_for_new_configurations': False,
    'physical_geometry_admission': False,
}
PROOF_DIGEST = descriptor.digest(NATIVE_PROOF)


def load(path):
    return descriptor.load(path)


def pointer(path, sha256, value):
    return {'path': str(path).replace('\\', '/'), 'sha256': sha256, 'pointer': value}


def exact_key(row):
    return (row['native_name'], row['resource'], row['resource_sha256'],
            str(descriptor.uuid.UUID(row['instance_guid'])))


def action_key(row):
    config = row['weapon_configuration']
    return (row['asset'], config['resource'], config['sha256'],
            str(descriptor.uuid.UUID(config['instance_guid'])))


def tube_job(row, origin):
    # Reuse validation/provenance extraction, then apply the exact existing
    # SPAS scalar dispatch tuple. DBX byte offsets never become native offsets.
    job = descriptor.descriptor(dict(row, **origin))
    fields = row['fields']; values = job['configuration']['values']
    if fields['FireLogic.FireLogicType']['value'] != 'fltSingleFireWithBoltAction':
        raise ValueError('Tube row lacks exact SingleFireWithBoltAction symbol')
    if fields['FireLogic.ReloadType']['value'] != 'rtSingleBullet':
        raise ValueError('Tube row lacks exact SingleBullet symbol')
    values.update(fireLogicType=1, reloadType=0)
    expected = dict(zip(ORDER, BASELINE_VALUES))
    for key in ('fireInputAction', 'reloadInputAction', 'baseCapacity',
                'numberOfMagazines', 'boltDelay', 'boltTime',
                'holdBoltUntilFireRelease', 'holdBoltUntilZoomRelease'):
        if values[key] != expected[key]:
            raise ValueError('Outside reviewed shared tube dispatch shape: '+key)
    if (values['reloadThreshold'] != 1 or not 0 <= values['reloadDelay'] <= descriptor.f32(.1)
            or not .1 < values['reloadTime'] <= 1 or not 0 <= values['postReloadTime'] <= 2):
        raise ValueError('Outside shared shell hold/transfer timer envelope')
    if job['authored_symbols']['FireLogic.ReloadLogic'] != 'rlWeaponSwitchCancelsUnfinishedReload':
        raise ValueError('Unreviewed shell reload logic')
    states = row.get('weapon_states', [])
    if not states or len(states) > 8 or any(s['fields'].get('IsPumpAction') is not True for s in states):
        raise ValueError('Mixed or missing exact authored pump handling')
    job['configuration']['timing'] = [dict(offset=o, expected=int(descriptor.bits(v),16),
        expected_bits=descriptor.bits(v), comparison='Float') for o,v in
        ((0x10, values['reloadThreshold']), (0x18, values['reloadTime']))]
    job['key'] = 'bc2:shotgun-descriptor:'+descriptor.digest(job['identity'])
    job['descriptor_digest'] = descriptor.digest(job['configuration'])
    job.update(native_family=FAMILY, instruction_proof_digest=PROOF_DIGEST,
               same_reviewed_dispatch_shape=True, deferred_reasons=[],
               jobs=['exact_native_family_review', 'exact_shell_and_pump_geometry'],
               physical_geometry_admission=False, authored_state_count=len(states))
    return job


def geometry_groups(actions, wanted, action_input):
    groups = {}; by_config = {}
    roles = {'1p_boltaction', '1p_reload', '1p_reload_in', '1p_reload_out', '1p_deploy'}
    for index, row in enumerate(actions['records']):
        key = action_key(row)
        if key not in wanted or row['authored_role'] not in roles:
            continue
        source = row['source']
        identity = {k:source[k] for k in ('mesh_sha256', 'lod_sha256', 'sha256', 'reference_clip_sha256')}
        identity.update(mesh=row['configured_mesh'], skeleton=row['skeleton_sha256'], role=row['authored_role'])
        group_key = descriptor.digest(identity)
        ref = dict(action_input, pointer='/records/'+str(index))
        by_config.setdefault(key, []).append({'role':row['authored_role'], 'group':group_key,
            'source':ref, 'binding_digest':row['binding_digest'], 'mechanism_digest':row['digest']})
        if group_key in groups:
            continue
        parts = []
        for part_index, part in enumerate(row['parts']):
            parts.append({k:part[k] for k in ('bone', 'parent', 'direct_weapon_child',
                'independent_rigid_geometry', 'triangles', 'mixed_triangles', 'bounds_m',
                'motion', 'weapon_from_baseline', 'weapon_from_peak', 'static_authored_closed_candidate',
                'peak_time_s', 'contact') if k in part})
            parts[-1]['source'] = dict(action_input, pointer=f'/records/{index}/parts/{part_index}')
        groups[group_key] = {'identity':identity, 'source':ref, 'rig_fingerprint':row['rig_fingerprint'],
            'animation_source':{k:v for k,v in source.items() if k!='skin_sections'},
            'parts':parts, 'linear_candidates':row['linear_candidates'], 'gaps':row['gaps'],
            'physical_role_reviewed':False, 'native_closed_boundary_reviewed':False}
    return groups, by_config


def build(configurations, actions, inputs, origin):
    if configurations.get('schema') != 'fvr.bc2.mechanism_batch_inputs':
        raise ValueError('Expected cached mechanism batch configurations')
    selected = [(i,r) for i,r in enumerate(configurations['rows']) if r['weapon_class']=='wcShotgun']
    if not 1 <= len(selected) <= 128:
        raise ValueError('Shotgun configuration count bound')
    wanted = {exact_key(r) for _,r in selected}
    if len(wanted) != len(selected):
        raise ValueError('Duplicate exact shotgun configuration')
    groups, bindings = geometry_groups(actions, wanted, inputs['actions'])
    tube=[]; magazine=[]; inventory=[]; seen=set()
    for index,row in selected:
        key=exact_key(row); fields=row['fields']; observed=(row['native_name'],row['resource'])
        if observed in seen:
            raise ValueError('Ambiguous observed shotgun name/path')
        seen.add(observed)
        reload_symbol=fields['FireLogic.ReloadType']['value']
        fire_symbol=fields['FireLogic.FireLogicType']['value']
        if (reload_symbol,fire_symbol)==('rtSingleBullet','fltSingleFireWithBoltAction'):
            job=tube_job(row,origin);tube.append(job);route='SingleBulletPump'
        elif reload_symbol=='rtMagazine' and fire_symbol in ('fltAutomaticFire','fltSingleFire'):
            job=descriptor.descriptor(dict(row,**origin));magazine.append(job)
            route='AutomaticMagazine' if fire_symbol=='fltAutomaticFire' else 'SingleFireMagazine'
        else:
            raise ValueError('Unknown shotgun dispatch tuple')
        expected_roles=({'1p_boltaction','1p_reload','1p_reload_in','1p_reload_out','1p_deploy'}
                        if route=='SingleBulletPump' else {'1p_reload','1p_deploy'})
        bound_roles=[binding['role'] for binding in bindings.get(key,[])]
        if set(bound_roles)!=expected_roles or len(bound_roles)!=len(expected_roles):
            raise ValueError('Missing or ambiguous exact shotgun action binding')
        source=dict(inputs['configurations'],pointer='/rows/'+str(index))
        job['source']=source
        inventory.append({'key':job['key'], 'asset':row['native_name'], 'resource':row['resource'],
            'route':route, 'configuration':job['configuration'], 'source':source,
            'field_evidence':row['fields'], 'authored_states':row['weapon_states'],
            'mechanical_inputs':bindings.get(key,[]), 'physical_geometry_admission':False})
    magazine_jobs={'schema':'fvr.bc2.magazine_descriptor_jobs','schema_version':1,
        'runtime_enabled':False,'registry_change':False,'existing_baselines':[],
        'descriptors':magazine,'rejected':[],'summary':{'descriptors':len(magazine)}}
    magazine_jobs=singlefire.apply(magazine_jobs,singlefire.EXPECTED_PROOF)
    by_key={r['key']:r for r in magazine_jobs['descriptors']}
    for item in inventory:
        if item['key'] in by_key:
            item['configuration']=by_key[item['key']]['configuration']
    return {'schema':'fvr.bc2.shotgun_reload_batch.v1','inputs':inputs,
        'native_dispatch_proof':NATIVE_PROOF,'instruction_proof_digest':PROOF_DIGEST,
        'tube_descriptors':sorted(tube,key=lambda x:x['key']),
        'magazine_jobs':magazine_jobs,'inventory':inventory,'geometry_groups':groups,
        'summary':{'configurations':len(inventory),'tube_pump':len(tube),
            'automatic_magazine':sum(r['route']=='AutomaticMagazine' for r in inventory),
            'singlefire_magazine':sum(r['route']=='SingleFireMagazine' for r in inventory),
            'mechanical_records':sum(len(r['mechanical_inputs']) for r in inventory),
            'geometry_content_groups':len(groups),'new_native_acceptance':False}}


def header(batch, reviews=None):
    reviews=[] if reviews is None else reviews
    if not isinstance(reviews,list) or len(reviews)>128:
        raise ValueError('Review count bound')
    approved={}
    for review in reviews:
        if (review.get('native_family')!=FAMILY or review.get('instruction_proof_digest')!=PROOF_DIGEST
                or not isinstance(review.get('reviewer'),str) or not review['reviewer'].strip()):
            raise ValueError('Exact shared native dispatch review required')
        key=descriptor.text(review.get('key'),256)
        if key in approved:raise ValueError('Duplicate exact native review')
        approved[key]=(descriptor.sha(review.get('descriptor_digest')),descriptor.sha(review.get('evidence_sha256')))
    lines=['#pragma once','#include "Bc2ReloadConfigDescriptor.h"',
        '// Generated by bc2_shotgun_reload_registry.py. Native data and physical',
        '// geometry are independent; candidates never grant native authority.',
        'namespace fvr::interaction {struct ReloadInsertionProfile;}',
        'namespace fvr::bc2 {',
        'enum class ShotgunReloadNativeClass : std::uint8_t { SingleBulletPump };',
        'struct ShotgunReloadRegistration {',
        ' std::uint64_t id=0;const ReloadConfigDescriptor* config=nullptr;',
        ' std::string_view descriptorDigest,sourceIdentityDigest,instructionProofDigest;',
        ' ShotgunReloadNativeClass nativeClass=ShotgunReloadNativeClass::SingleBulletPump;',
        '};',
        'struct Bc2PumpCalibration;',
        '// One immutable equipment row joins independently reviewed geometry to',
        '// its exact configuration. Never populate a new row with SPAS defaults.',
        'struct ShotgunReloadEquipmentProfile {',
        ' const ShotgunReloadRegistration* native=nullptr;',
        ' std::string_view meshPath,geometryDigest;std::uint64_t rigFingerprint=0;',
        ' const interaction::ReloadInsertionProfile* shellInsertion=nullptr;',
        ' const Bc2PumpCalibration* pump=nullptr;',
        ' bool physicalReviewed=false;',
        '};']
    entries=[];ids=set();names=set();matched=set()
    for i,row in enumerate(batch['tube_descriptors']):
        c=row['configuration'];v=c['values'];digest=row['descriptor_digest'];ident=(c['assetName'],c['assetPath'])
        if (descriptor.digest(c)!=digest or row['key']!='bc2:shotgun-descriptor:'+descriptor.digest(row['identity'])
                or row['instruction_proof_digest']!=PROOF_DIGEST):
            raise ValueError('Changed exact tube descriptor')
        ident_id=int(digest[:16],16)
        if not ident_id or ident_id in ids or ident in names:raise ValueError('Ambiguous tube identity')
        ids.add(ident_id);names.add(ident)
        baseline=ident==BASELINE
        if baseline and any(v[k]!=(descriptor.f32(x) if 6<=n<12 else x)
                for n,(k,x) in enumerate(zip(ORDER,BASELINE_VALUES))):
            raise ValueError('Existing SPAS baseline values differ')
        enabled=baseline or row['key'] in approved
        if row['key'] in approved:
            if approved[row['key']][0]!=digest:raise ValueError('Native review descriptor differs')
            matched.add(row['key'])
        def literal(k):
            x=v[k]
            if type(x) is bool:return str(x).lower()
            if k in ORDER[6:12]:return 'std::bit_cast<float>(0x'+descriptor.bits(x)+'u)'
            return str(x)
        if baseline:
            config='SpasReloadDescriptor'
        else:
            config=f'ShotgunReloadConfig{i}'
            words=','.join('{0x%x,0x%08xu,ReloadTimingComparison::Float}'%(w['offset'],w['expected']) for w in c['timing'])
            lines.append(f'inline constexpr std::array<ReloadTimingWord,2> ShotgunReloadTiming{i}{{{{{words}}}}};')
            lines.append(f'inline constexpr ReloadConfigDescriptor {config}{{{json.dumps(c["assetName"])},{json.dumps(c["assetPath"])},'
                '{'+','.join(literal(k) for k in ORDER)+f'}},ShotgunReloadTiming{i},ReloadDescriptorAdmission::'+('ReviewedNative' if enabled else 'Candidate')+'};')
        entries.append(f' {{0x{ident_id:016x}ull,&{config},"{digest}","{descriptor.digest(row["identity"])}","{PROOF_DIGEST}"}},')
    if matched!=set(approved):raise ValueError('Review key not found in tube batch')
    lines.append(f'inline constexpr std::array<ShotgunReloadRegistration,{len(entries)}> ShotgunReloadRegistrations{{{{')
    lines+=entries+['}};',
        '// Native resolution deliberately includes the shared coherent object-chain',
        '// and complete scalar matcher. No name-only or class fallback exists.',
        'inline const ShotgunReloadRegistration* ResolveShotgunReloadRegistration(const ReloadObservedConfig& c)noexcept {',
        ' const ShotgunReloadRegistration* found=nullptr;',
        ' for(const auto& row:ShotgunReloadRegistrations)if(row.config&&MatchesReloadDescriptor(c,*row.config)){',
        '  if(found)return nullptr;found=&row;',
        ' }return found;', '}',
        'inline const ReloadConfigDescriptor* ResolveShotgunReloadConfig(const ReloadObservedConfig& c)noexcept {',
        ' const auto* row=ResolveShotgunReloadRegistration(c);return row?row->config:nullptr;', '}',
        'inline bool ReadShotgunReloadTiming(const ReloadStateMemory& memory,const ReloadObservedConfig& c)noexcept {',
        ' const auto* config=ResolveShotgunReloadConfig(c);return config&&ReadReloadDescriptorTiming(memory,c,*config);', '}',
        '// The caller supplies current same-owner selected-mesh/rig evidence and',
        '// validates the profile mechanics before retaining this row per item.',
        'inline const ShotgunReloadEquipmentProfile* ResolveShotgunReloadEquipment(',
        ' const ReloadObservedConfig& c,std::string_view mesh,std::uint64_t rig,',
        ' std::span<const ShotgunReloadEquipmentProfile> profiles)noexcept {',
        ' const auto* native=ResolveShotgunReloadRegistration(c);if(!native||!rig||mesh.empty())return nullptr;',
        ' const ShotgunReloadEquipmentProfile* found=nullptr;',
        ' for(const auto& row:profiles)if(row.native==native&&row.physicalReviewed&&row.shellInsertion&&row.pump&&',
        '  row.meshPath==mesh&&row.rigFingerprint==rig&&row.geometryDigest.size()==64){',
        '  if(found)return nullptr;found=&row;',
        ' }return found;', '}',
        '} // namespace fvr::bc2','']
    return '\n'.join(lines)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('configurations','actions','metadata','output','header'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--native-reviews',type=Path)
    parser.add_argument('--magazine-jobs',type=Path)
    args=parser.parse_args();docs={};inputs={}
    for name in ('configurations','actions','metadata'):
        docs[name],sha=load(getattr(args,name));inputs[name]=pointer(getattr(args,name),sha,'')
    metadata=docs['metadata']['configuration']
    for row in docs['configurations']['rows']:
        if row.get('weapon_class')=='wcShotgun' and row not in metadata['resolved_weapons']:
            raise ValueError('Selected shotgun row differs from pinned source metadata')
    origin={'origin_archive':metadata['archive'],'origin_index_sha256':metadata['index_sha256']}
    batch=build(docs['configurations'],docs['actions'],inputs,origin)
    reviews=None
    if args.native_reviews:
        reviews,sha=load(args.native_reviews);batch['native_reviews_input']=pointer(args.native_reviews,sha,'')
    generated=header(batch,reviews)
    batch['header_sha256']=descriptor.hashlib.sha256(generated.encode()).hexdigest()
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(batch,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    args.header.parent.mkdir(parents=True,exist_ok=True);args.header.write_bytes(generated.encode('utf-8'))
    if args.magazine_jobs:
        args.magazine_jobs.parent.mkdir(parents=True,exist_ok=True)
        args.magazine_jobs.write_text(json.dumps(batch['magazine_jobs'],indent=2,allow_nan=False)+'\n',encoding='utf-8')
    print(json.dumps(batch['summary'],sort_keys=True))


if __name__=='__main__':main()
