"""Exact roster/source joins and rejection boundaries, no game access."""
import copy
import json
from pathlib import Path
import re
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import bc2_firearm_magazine_batch as batch
import bc2_firearm_magazine_enrollment as enrollment
import bc2_magazine_registry_header as registry


class FirearmMagazineBatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.output=ROOT/'profiles/weapons45-magazines'
        cls.plan=json.loads((cls.output/'batch.json').read_text(encoding='utf-8'))
        cls.jobs=json.loads((cls.output/'descriptor-jobs.json').read_text(encoding='utf-8'))
        cls.geometry=json.loads((cls.output/'geometry.json').read_text(encoding='utf-8'))
        cls.docs,cls.bindings,cls.meshes=enrollment.source_inputs(cls.plan)

    def test_entire_union_exactly_once(self):
        rows=self.plan['rows']
        roster=self.docs['roster']['rows']
        expected={batch.roster_key(r) for r in roster if r['mechanism'] in ('detachable_magazine','bolt_magazine')}
        self.assertEqual(128,len(expected))
        self.assertEqual(expected,{batch.weapon_key(r['identity']['weapon']) for r in rows})
        self.assertEqual(len(rows),len({r['key'] for r in rows}))
        for name in ('sniper43','shotgun43'):
            filename='descriptor-jobs.json' if name=='sniper43' else 'magazine-jobs.json'
            source=json.loads((ROOT/'handoff/tasks/WEAPONS'/('20261030-'+name)/filename).read_text(encoding='utf-8'))
            self.assertTrue({r['key'] for r in source['descriptors']} <= {r['key'] for r in rows})

    def test_every_descriptor_uses_supported_exact_static_dispatch(self):
        self.assertTrue(all(registry.eligible(j) for j in self.jobs['descriptors']))
        self.assertTrue(all(j['descriptor_digest']==batch.descriptors.digest(j['configuration']) for j in self.jobs['descriptors']))
        reviewed=json.loads((self.output/'exact-native-reviews.json').read_text(encoding='utf-8'))
        keys={r['key'] for r in self.plan['rows'] if not r['preserved_registration']}
        _,receipt=registry.build(dict(self.jobs,descriptors=[j for j in self.jobs['descriptors'] if j['key'] in keys]),reviewed)
        self.assertEqual(sum((r.get('geometry_available') or r['strict_geometry']) and not r['preserved_registration'] for r in self.plan['rows']),receipt['enabled'])

    def test_foreign_review_key_and_changed_descriptor_rejected(self):
        j=copy.deepcopy(next(j for j in self.jobs['descriptors'] if j['configuration']['values']['fireLogicType']==2 and not j.get('automatic_stock_bolt_proof_digest')))
        family,proof=batch.family(j)
        review=dict(key=j['key'],descriptor_digest=j['descriptor_digest'],family_proof=family,evidence_sha256='1'*64)
        if proof:review['instruction_proof_digest']=proof
        foreign=dict(review,key='bc2:magazine-descriptor:'+'0'*64)
        with self.assertRaises(ValueError):registry.build(dict(self.jobs,descriptors=[j]),[foreign])
        j['configuration']['values']['baseCapacity']+=1
        with self.assertRaises(ValueError):registry.build(dict(self.jobs,descriptors=[j]),[review])

    def test_content_reuse_requires_all_coordinate_hashes(self):
        row=next(r for r in self.plan['rows'] if r['strict_geometry'] and r['action_record'] is not None and r.get('geometry_rebound'))
        p=next(p for p in self.geometry['profiles'] if p['profile_digest']==row['geometry_digest'])
        b=self.bindings[row['binding_digest']]
        a=self.docs['actions']['records'][row['action_record']]
        batch.rebind(p,b,a)
        for name in ('mesh_sha256','lod_sha256','reference_clip_sha256','sha256'):
            changed=copy.deepcopy(a);changed['source'][name]='0'*64
            with self.subTest(name=name),self.assertRaises(ValueError):batch.rebind(p,b,changed)
        changed=copy.deepcopy(b);changed['weapon']['resource']='Objects/Foreign.dbx'
        with self.assertRaises(ValueError):batch.rebind(p,changed,a)

    def test_existing_registrations_have_no_duplicate_additions(self):
        expected=batch.preserved_identities(ROOT)
        self.assertEqual(expected,{(r['asset'],r['configuration']) for r in self.plan['rows'] if r['preserved_registration']})
        header=(self.output/'Registry.additions.generated.h').read_text(encoding='utf-8')
        additions=set(re.findall(r'RegistryProfile\d+\{\{"([^"\n]+)","([^"\n]+)"',header))
        self.assertFalse(expected & additions)
        for kind,symbol in [('Registry','MagazineNativeRegistrations'),('Geometry','ExperimentalMagazineGeometry')]:
            text=(self.output/('Combined'+kind+'.h')).read_text(encoding='utf-8')
            self.assertIn('inline const auto Magazines45Preserved'+symbol+'=',text)
            self.assertEqual(1,text.count('inline const auto '+symbol+'='))
            for include in re.findall(r'#include "([^"]+)"',text):self.assertTrue((self.output/include).is_file(),include)

    def test_no_name_fallback_for_missing_source(self):
        original_gaps={r['key'] for r in self.plan['source_jobs']}
        recovered=[r for r in self.plan['rows'] if r['key'] in original_gaps]
        self.assertEqual({'G3','M1A1 Thompson'},{r['asset'] for r in recovered})
        self.assertEqual(8,len(recovered))
        self.assertTrue(all(not r['strict_geometry'] and r['mod_authored_geometry'] and r['binding_digest'] for r in recovered))
        for row in recovered:
            p=next(p for p in self.geometry['profiles'] if p['profile_digest']==row['geometry_digest'])
            self.assertEqual(batch.weapon_key(row['identity']['weapon']),batch.weapon_key(p['weapon']))
            self.assertEqual('explicit_mod_authored_detachable_magazine',p['role_evidence']['role'])

    def test_changed_pinned_document_rejected(self):
        parent=ROOT/'handoff/tasks/WEAPONS/20261030-magazines45'
        parent.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=parent) as temporary:
            path=Path(temporary)/'source.json';path.write_text('{"value":1}',encoding='utf-8')
            pin=batch.pin(path);path.write_text('{"value":2}',encoding='utf-8')
            with self.assertRaises(ValueError):batch.read_pin(pin)


if __name__=='__main__':unittest.main()
