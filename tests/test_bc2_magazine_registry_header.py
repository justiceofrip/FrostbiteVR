import copy
import importlib.util
from pathlib import Path
import sys
import unittest

root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'tools'))
import bc2_magazine_descriptor_manifest as d
import bc2_magazine_registry_header as h
spec=importlib.util.spec_from_file_location('fixture',Path(__file__).with_name('test_bc2_magazine_descriptor_manifest.py'))
fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)

def jobs(*rows):return d.build(fixture.catalog(*(rows or [fixture.row()])))
def review(row):return dict(key=row['key'],descriptor_digest=row['descriptor_digest'],family_proof=h.FAMILY,evidence_sha256='e'*64)

class HeaderTests(unittest.TestCase):
    def test_default_disabled(self):
        text,summary=h.build(jobs());self.assertEqual(summary['enabled'],0)
        self.assertIn('ReloadDescriptorAdmission::Candidate',text);self.assertIn('MagazineCycleAdmission::Candidate',text)
    def test_explicit_exact_review(self):
        j=jobs();text,s=h.build(j,[review(j['descriptors'][0])]);self.assertEqual(s['enabled'],1)
        self.assertIn('ReviewedReload11Transfer12',text)
    def test_same_name_distinct_paths(self):
        j=jobs(fixture.row(variant='A'),fixture.row(variant='B'));text,s=h.build(j,[review(x) for x in j['descriptors']])
        self.assertEqual(s['enabled'],2);self.assertIn('Objects/Weapons/A',text);self.assertIn('Objects/Weapons/B',text)
    def test_changed_consumed_config_rejects(self):
        j=jobs();j['descriptors'][0]['configuration']['values']['baseCapacity']=99
        with self.assertRaises(ValueError):h.build(j)
    def test_wrong_review_digest_rejects(self):
        j=jobs();r=review(j['descriptors'][0]);r['descriptor_digest']='f'*64
        with self.assertRaises(ValueError):h.build(j,[r])
    def test_unknown_review_key_rejects(self):
        j=jobs();r=review(j['descriptors'][0]);r['key']='other'
        with self.assertRaises(ValueError):h.build(j,[r])
    def test_unknown_dispatch_no_invented_words(self):
        r=fixture.row();r['fields']['FireLogic.FireLogicType']['value']='fltSingleFire';j=jobs(r)
        text,s=h.build(j);self.assertEqual(s['rows'],0)
        with self.assertRaises(ValueError):h.build(j,[review(j['descriptors'][0])])
    def test_bolt_fields_not_zeroed(self):
        r=fixture.row();r['fields']['FireLogic.BoltAction.BoltActionTime']['value']=1;j=jobs(r)
        self.assertEqual(h.build(j)[1]['rows'],0)
        with self.assertRaises(ValueError):h.build(j,[review(j['descriptors'][0])])
    def test_unknown_family_review_rejects(self):
        j=jobs();r=review(j['descriptors'][0]);r['family_proof']='invented'
        with self.assertRaises(ValueError):h.build(j,[r])
    def test_exact_path_conflict_rejects(self):
        a=fixture.row();b=fixture.row();b['resource_sha256']='f'*64
        with self.assertRaises(ValueError):h.build(jobs(a,b))
    def test_prefix_collisions_and_reserved_keys(self):
        self.assertEqual(h.stable_id('f'*16+'a'*48),h.stable_id('f'*16+'b'*48))
        with self.assertRaises(ValueError):h.stable_id('0'*64)
        from unittest.mock import patch
        with patch.object(h,'stable_id',return_value=5):
            with self.assertRaises(ValueError):h.build(jobs(fixture.row(variant='A'),fixture.row(variant='B')))
    def test_wrong_timing_even_self_consistent_digest_not_reviewed(self):
        j=jobs();r=j['descriptors'][0];r['configuration']['timing'][0]['expected']=0
        r['descriptor_digest']=d.digest(r['configuration'])
        with self.assertRaises(ValueError):h.build(j,[review(r)])
    def test_baselines_cannot_be_overridden(self):
        j=jobs();r=j['descriptors'][0];r['existing_baseline_reference']=True
        self.assertEqual(h.build(j)[1]['rows'],0)
        with self.assertRaises(ValueError):h.build(j,[review(r)])
    def test_deadline_cannot_be_changed_outside_config_digest(self):
        j=jobs();r=j['descriptors'][0];r['proposed_completion_deadline_ns']+=1000000
        with self.assertRaises(ValueError):h.build(j,[review(r)])
    def test_source_identity_cannot_be_relabelled(self):
        j=jobs();j['descriptors'][0]['identity']['assetPath']='another/path'
        with self.assertRaises(ValueError):h.build(j)
    def test_future_firearm_classes_are_data_not_admission(self):
        for cls in ('wcPistol','wcSniper'):
            r=fixture.row();r['weapon_class']=cls;r['fields']['FireLogic.FireLogicType']['value']='fltSingleFire'
            j=jobs(r);self.assertEqual(len(j['descriptors']),1);self.assertEqual(h.build(j)[1]['enabled'],0)

if __name__=='__main__':unittest.main()

