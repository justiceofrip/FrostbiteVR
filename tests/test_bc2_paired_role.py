from pathlib import Path
import sys,copy,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import bc2_authored_magazine_geometry as p
from test_bc2_authored_magazine_geometry import PairedClip,paired_rig,fixture,at

class TwoPartClip(PairedClip):
 def evaluate(self,sk,t,names):
  row=super().evaluate(sk,t,names)
  row['weapon_relative']['Bolt']=row['weapon_relative']['Magazine'] if self.mode=='both' else at()
  return row

class PairedRole(unittest.TestCase):
 def args(self,mode='valid'):
  part=fixture()[0]
  return ([{'bone':n,'native_role_verified':False} for n in ('Bolt','Magazine')],
          {'Bolt':part,'Magazine':part},{'weapon_relative':{'Bolt':at(),'Magazine':at()}},TwoPartClip(mode),paired_rig())
 def test_actual_sampler_distinguishes_moving_bolt_from_held_magazine(self):
  role,pair,rejected=p.select_paired_role(*self.args())
  self.assertEqual(role['bone'],'Magazine');self.assertFalse(role['native_role_verified'])
  self.assertEqual(role['ambiguity_resolution'],'unique_complete_stable_paired_contact')
  self.assertEqual(role['geometric_candidate_count'],2);self.assertEqual(len(pair['wrist_from_fingers']),15)
  self.assertEqual(rejected[0]['bone'],'Bolt');self.assertIn('no complete stable paired contact',rejected[0]['reasons'][0])
 def test_multiple_or_missing_contacts_never_first_match(self):
  for mode in ('both','sliding'):
   with self.subTest(mode=mode),self.assertRaisesRegex(ValueError,'not unique'):
    p.select_paired_role(*self.args(mode))
 def test_duplicate_and_excess_candidates_reject_before_sampling(self):
  for candidates in ([{'bone':'Magazine'}]*2,[{'bone':str(i)} for i in range(129)]):
   args=list(self.args());args[0]=candidates
   with self.assertRaisesRegex(ValueError,'bounds/identity'):p.select_paired_role(*args)
 def test_single_role_predecessor_is_not_reinterpreted(self):
  args=list(self.args());args[0]=args[0][1:]
  with self.assertRaisesRegex(ValueError,'bounds/identity'):p.select_paired_role(*args)

if __name__=='__main__':unittest.main()
