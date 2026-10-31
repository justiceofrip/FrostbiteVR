"""Pinned four-geometry/25-configuration shotgun equipment acceptance checks."""
import copy
import json
import math
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import bc2_shotgun_equipment as equipment


class ShotgunEquipmentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.batch=json.loads((ROOT/'handoff/tasks/WEAPONS/20261030-shotgun43/batch.json').read_text())
        cls.samples=json.loads((ROOT/'handoff/tasks/WEAPONS/20261030-shotgun44/samples.json').read_text())
        cls.actions=equipment.load_actions(cls.batch['inputs']['actions']['path'])
        cls.result=equipment.build(cls.batch,cls.samples,cls.actions)

    def test_audio_roles_follow_the_existing_exact_action_join(self):
        for row in self.result['profiles']:
            audio=row['audio'];self.assertEqual(equipment.ACTION_HASH,audio['provenance'])
            self.assertEqual('generic_physical_cues_from_owner_spas_cache',audio['cue_policy'])
            for role in audio['roles'].values():
                source=self.actions['records'][role['record']]['source']
                self.assertEqual('exact_authored_animation',role['status'])
                self.assertEqual(source['asset_guid'],role['guid'])
                self.assertEqual(source['asset_document_sha256'],role['asset_document_sha256'])
        partial=equipment.build(self.batch,self.samples)
        for row in partial['profiles']:
            self.assertEqual('unsupported_missing_exact_authored_binding',row['audio']['roles']['finish']['status'])
            self.assertIsNone(row['audio']['roles']['finish']['guid'])

    def test_rejects_foreign_finish_binding_with_consistent_digests(self):
        for donor_asset in ('SPAS12','NS2000'):
            with self.subTest(donor_asset=donor_asset):
                b=copy.deepcopy(self.batch)
                selected=next(r for r in b['inventory']if r['asset']=='NS2000')
                donor=next(r for r in b['inventory']if r['asset']==donor_asset and r['resource']!=selected['resource'])
                foreign=copy.deepcopy(next(p for p in donor['mechanical_inputs']if p['role']=='1p_reload_out'))
                record=self.actions['records'][int(foreign['source']['pointer'].removeprefix('/records/'))]
                # The complete foreign binding is internally valid; only its
                # relationship to the selected equipment is wrong.
                self.assertEqual(record['digest'],foreign['mechanism_digest'])
                self.assertEqual(record['binding_digest'],foreign['binding_digest'])
                self.assertEqual('1p_reload_out',record['source']['role'])
                selected['mechanical_inputs']=[foreign if p['role']=='1p_reload_out' else p
                                               for p in selected['mechanical_inputs']]
                with self.assertRaisesRegex(ValueError,'Exact finish audio equipment identity differs'):
                    equipment.build(b,self.samples,self.actions)

    def test_rejects_finish_identity_changes_even_with_recomputed_digest(self):
        for field in ('configured_mesh','animation_tree','binding_digest','weapon_configuration'):
            with self.subTest(field=field):
                b=copy.deepcopy(self.batch)
                selected=next(r for r in b['inventory']if r['asset']=='NS2000')
                binding=next(p for p in selected['mechanical_inputs']if p['role']=='1p_reload_out')
                index=int(binding['source']['pointer'].removeprefix('/records/'))
                actions=dict(self.actions,records=self.actions['records'].copy())
                record=copy.deepcopy(actions['records'][index]);actions['records'][index]=record
                if field=='animation_tree':record['source'][field]['instance_guid']='00000000-0000-0000-0000-000000000000'
                elif field=='weapon_configuration':record[field]['resource']='ForeignWeapon.dbx'
                elif field=='binding_digest':record[field]='0'*64
                else:record[field]='ForeignMesh'
                record['digest']=equipment.digest({k:v for k,v in record.items()if k!='digest'})
                binding.update(mechanism_digest=record['digest'],binding_digest=record['binding_digest'])
                with self.assertRaisesRegex(ValueError,'Exact finish audio equipment identity differs'):
                    equipment.build(b,self.samples,actions)

    def test_finish_without_selected_state_proof_stays_unsupported(self):
        b=copy.deepcopy(self.batch)
        selected=next(r for r in b['inventory']if r['asset']=='NS2000')
        selected['authored_states']=[]
        result=equipment.build(b,self.samples,self.actions)
        row=next(r for r in result['profiles']if r['key']==selected['key'])
        finish=row['audio']['roles']['finish']
        self.assertEqual('unsupported_missing_exact_authored_binding',finish['status'])
        self.assertIsNone(finish['guid'])

    def test_all_exact_tube_variants_have_independent_native_joins(self):
        rows=self.result['profiles']
        self.assertEqual(25,len(rows));self.assertEqual(25,len({r['profile_id']for r in rows}))
        self.assertEqual({d['key']for d in self.batch['tube_descriptors']},{r['key']for r in rows})
        for row in rows:
            d=self.batch['tube_descriptors'][row['native_index']]
            self.assertEqual(row['descriptor_digest'],d['descriptor_digest'])
            self.assertEqual(row['asset_path'],d['identity']['assetPath'])
            self.assertFalse(row['native_admission_changed'])

    def test_four_independent_pump_geometries(self):
        groups=self.result['groups'];self.assertEqual(4,len(groups))
        by_index={g['sources']['pump_record']:g for g in groups}
        for index,(part,direction,low,high,companions) in {
            189:('jntWpn_4',-1,.18,.19,[]),200:('jntWpn_4',1,.094,.096,['jntWpn_3','jntWpn_5']),
            205:('jntWpn_4',1,.094,.096,['jntWpn_5']),386:('jntWpn_5',1,.089,.091,['jntWpn_3','jntWpn_4'])}.items():
            p=by_index[index]['pump'];self.assertEqual(part,p['part'])
            self.assertGreater(p['axis'][2]*direction,.99999)
            self.assertLess(low,p['stroke']);self.assertLess(p['stroke'],high)
            self.assertEqual(companions,[c['name']for c in p['companions']])
            self.assertTrue(p['authoredReviewed']);self.assertFalse(p['measured'])
            self.assertFalse(p['policy']['native_boundary_verified'])

    def test_shell_endpoints_and_complete_same_frame_hands(self):
        centers=[];grips=[]
        for g in self.result['groups']:
            s=g['shell'];centers.append(s['terminalItem'][12:15]);grips.append(s['itemFromHand'])
            self.assertEqual(set(equipment.FINGERS),set(s['handShape']))
            self.assertEqual(0,s['source']['stable_contact_windows'])
            self.assertFalse(s['policy']['native_transfer_frame_verified'])
            self.assertAlmostEqual(1,math.hypot(*s['travelDirection']),places=8)
            endpoint=s['weaponFromEntry'].copy()
            for k in range(3):endpoint[12+k]+=sum(s['travelDirection'][j]*endpoint[j*4+k]for j in range(3))*s['travelMeters']
            item=equipment.multiply(equipment.inverse_rigid(s['itemFromInsertion']),endpoint)
            self.assertLess(max(abs(a-b)for a,b in zip(item,s['terminalItem'])),1e-7)
            self.assertGreater(s['travelMeters'],2*s['postCaptureTravelMeters'])
        self.assertEqual(4,len({tuple(v)for v in centers}))
        self.assertEqual(4,len({tuple(v)for v in grips}))

    def test_single_shell_policy_hides_only_ns_second_round(self):
        for g in self.result['groups']:
            s=g['shell'];names=[p['name']for p in g['pump']['fixedParts']]
            self.assertNotIn(s['bone'],names)
            self.assertFalse(set(s['descendants'])&set(names))
            self.assertEqual(['jntWpnwpnJnt_14'] if g['sources']['shell_record']==190 else [],s['descendants'])

    def test_companions_have_exact_closed_and_shared_rear_endpoints(self):
        for g in self.result['groups']:
            for p in g['pump']['companions']:
                self.assertEqual(17,len(p['rear']));self.assertEqual(17,len(p['forward']))
                self.assertEqual(p['closed'],p['rear'][0]);self.assertEqual(p['closed'],p['forward'][0])
                self.assertEqual(p['rear'][-1],p['forward'][-1])
                self.assertNotEqual(p['closed'],p['rear'][-1])

    def test_rejects_foreign_action_hash_and_modified_sample(self):
        for mutate in (
            lambda b,s:s.update(actions_sha256='0'*64),
            lambda b,s:s['records'][0]['samples'][0]['poses']['LeftHand'].__setitem__(12,999),
            lambda b,s:b['inputs']['actions'].update(sha256='0'*64),
        ):
            b=copy.deepcopy(self.batch);s=copy.deepcopy(self.samples);mutate(b,s)
            with self.assertRaises(ValueError):equipment.build(b,s)

    def test_rejects_unreviewed_geometry_or_ambiguous_configuration(self):
        for mutation in ('mesh','duplicate','rig','missing'):
            b=copy.deepcopy(self.batch)
            if mutation=='duplicate':b['inventory'].append(copy.deepcopy(b['inventory'][0]))
            elif mutation=='missing':b['tube_descriptors'].pop()
            else:
                r=next(r for r in b['inventory']if r['route']=='SingleBulletPump')
                inp=next(i for i in r['mechanical_inputs']if i['role']=='1p_boltaction')
                g=b['geometry_groups'][inp['group']]
                if mutation=='mesh':g['identity']['mesh']='ForeignMesh'
                else:g['rig_fingerprint']='fnv1a64:0000000000000000'
            with self.assertRaises(ValueError):equipment.build(b,self.samples)

    def test_generated_source_is_current_and_never_asserts_native_measurement(self):
        expected=equipment.header(self.result)
        actual=(ROOT/'profiles/weapons43/ShotgunEquipment.generated.h').read_text()
        self.assertEqual(expected,actual)
        self.assertEqual(25,actual.count('out.measured=false;out.authoredReviewed=true;'))
        self.assertEqual(25,actual.count('static_assert(ShotgunReloadRegistrations['))
        self.assertIn('FindShotgunShellGeometry',actual)


if __name__=='__main__':unittest.main()
