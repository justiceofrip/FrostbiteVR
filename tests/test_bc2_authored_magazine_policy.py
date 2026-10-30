"""Portable authored evidence must retain exact frames and reject drift."""
import copy
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import bc2_authored_magazine_policy as policy
import bc2_firearm_magazine_batch as batch


class AuthoredMagazinePolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        rows=json.loads((ROOT/'profiles/weapons45-magazines/geometry.json').read_text())['profiles']
        cls.original=[p for p in rows if p.get('status')=='explicit_mod_authored_geometry']
        cls.portable=[p if 'mod_policy_evidence'in p else policy.embed_profile(p) for p in cls.original]

    def test_all_authored_variants_embed_without_numerical_geometry_change(self):
        self.assertEqual(48,len(self.portable))
        for old,new in zip(self.original,self.portable):
            for field in ('attached_item','item_from_hand','item_from_insertion','weapon_from_entry','wrist_from_fingers'):
                self.assertEqual(old['geometry'][field],new['geometry'][field])
            self.assertEqual(old['mod_authored_members'],new['mod_authored_members'])
            self.assertFalse(new['strict_inference_passed'])

    def test_validation_never_reads_private_provenance(self):
        with patch.object(batch,'read_pin',side_effect=AssertionError('Private path read')):
            for p in self.portable:policy.validate_profile(p)
            header=policy.header(self.portable)
        self.assertIn('g.assemblyCount=',header)
        self.assertNotIn('G:\\',header)

    def test_changed_wrist_and_child_transform_rejected(self):
        p=copy.deepcopy(next(p for p in self.portable if p['mod_authored_members']))
        p['geometry']['item_from_hand'][12]+=.01
        p['profile_digest']=batch.descriptors.digest({k:v for k,v in p.items() if k!='profile_digest'})
        with self.assertRaises(ValueError):policy.validate_profile(p)
        p=copy.deepcopy(next(p for p in self.portable if p['mod_authored_members']))
        p['mod_authored_members'][0]['item_from_bone'][12]+=.01
        p['profile_digest']=batch.descriptors.digest({k:v for k,v in p.items() if k!='profile_digest'})
        with self.assertRaises(ValueError):policy.validate_profile(p)

    def test_changed_feed_axis_or_inference_claim_rejected(self):
        p=copy.deepcopy(self.portable[0]);review=p['geometry']['design']['mod_authored_policy']
        review['entry_axis']=(review['entry_axis']+1)%3
        review['review_digest']=batch.descriptors.digest({k:v for k,v in review.items() if k!='review_digest'})
        p['profile_digest']=batch.descriptors.digest({k:v for k,v in p.items() if k!='profile_digest'})
        with self.assertRaises(ValueError):policy.validate_profile(p)
        p=copy.deepcopy(self.portable[0]);p['strict_inference_passed']=True
        p['profile_digest']=batch.descriptors.digest({k:v for k,v in p.items() if k!='profile_digest'})
        with self.assertRaises(ValueError):policy.validate_profile(p)

    def test_installed_content_is_checked_before_pose_replay(self):
        with self.assertRaisesRegex(ValueError,'Installed authored policy content changed'):
            policy.validate_installed_sample(self.portable[0],dict(mesh=b'foreign content'))


if __name__=='__main__':unittest.main()
