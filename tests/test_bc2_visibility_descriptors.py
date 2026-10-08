import unittest
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import bc2_visibility_descriptors as d
class DescriptorSafety(unittest.TestCase):
    def test_complete_union_and_weapon_root(self):
        self.assertEqual(d.weighted_names({1,2},{3},{1:'jntWpn_1',2:'jntWpn_6'},
          {'jntWpn_6':'jntWpn_2','jntWpn_2':'jntWpn_1'}),['jntWpn_1','jntWpn_6'])
    def test_arm_overlap_fails(self):
        with self.assertRaisesRegex(ValueError,'overlap'):d.weighted_names({1},{1},{1:'jntWpn_1'},{})
    def test_unknown_or_unverified_subtree_fails(self):
        for known,parents in [({},{}),({1:'jntWpn_6'},{}),({1:'jntWpn_6'},{'jntWpn_6':'LeftHand'}),
                              ({1:'jntWpn_6'},{'jntWpn_6':'jntWpn_6'})]:
            with self.assertRaises(ValueError):d.weighted_names({1},set(),known,parents)
    def test_empty_or_oversized_weight_union_fails(self):
        for hashes in [set(),set(range(65))]:
            with self.assertRaisesRegex(ValueError,'bound'):d.weighted_names(hashes,set(),{},{})
    def test_generated_rows_cannot_auto_admit(self):
        report={'rows':[{'asset':'A','meshes':['exact/A'],'weighted_names':['jntWpn_1'],
                          'rig_fingerprint':'a7f219a1426216ab','native_admitted':True}]}
        text=d.header(report);self.assertIn('0xa7f219a1426216abull,false',text)
        self.assertEqual(text,d.header(report))
if __name__=='__main__':unittest.main()
