from pathlib import Path
import sys,copy,math,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import bc2_authored_magazine_geometry as p
from test_bc2_authored_magazine_geometry import PairedClip,paired_rig,fixture,at,profile,report

def drum():
 points=[[x,y,z] for x in (-.14,.14) for y in (-.06,.06) for z in (-.04,.04)]
 return {'points':points,'triangles':80,'mixed_triangles':0,'sections':[],'extent':[.28,.12,.08]}
def body():return [[0,.09,0],[.01,.09,0],[-.01,.09,0]]
class CoMotionClip(PairedClip):
 def evaluate(self,sk,t,names):
  row=super().evaluate(sk,t,names);old=row['weapon_relative']['Magazine']
  item=at(0,-min(.08,t*.35),0)
  if self.mode=='static_part':item=at()
  if self.mode=='wrong_baseline':item[12]=.02
  if self.mode=='lateral':item=at(min(.08,t*.35),0,0)
  for key,value in row['weapon_relative'].items():row['weapon_relative'][key]=p.multiply(p.multiply(value,p.inverse_rigid(old)),item)
  return row

class DrumGrasp(unittest.TestCase):
 def pair(self,mode='valid'):
  return p.paired_reload_grasp(CoMotionClip(mode),paired_rig(),'Magazine',at(),drum())
 def test_stable_true_comotion_is_explicit_alternative(self):
  pair=self.pair();r=pair['receipt'];self.assertEqual(r['selection_mode'],'stable_relative_co_motion')
  self.assertGreater(r['window']['co_motion_travel_m'],.04);self.assertLess(r['window']['minimum_item_motion_m'],.1)
  self.assertEqual(r['criteria'],p.PAIRED_GRASP);self.assertEqual(len(pair['wrist_from_fingers']),15)
 def test_static_part_or_sliding_wrist_is_not_grasp(self):
  for mode in ('static_part','sliding','finger_motion','disconnected'):
   with self.subTest(mode=mode),self.assertRaises(ValueError):self.pair(mode)
 def test_closed_baseline_remains_exact(self):
  with self.assertRaisesRegex(ValueError,'baseline'):self.pair('wrong_baseline')
 def test_actual_coupled_transform_to_rotated_controller(self):
  pair=self.pair();frame=at(3,4,5);frame[:12]=[0,1,0,0,-1,0,0,0,0,0,1,0]
  item=p.multiply(p.inverse_rigid(pair['item_from_hand']),frame)
  authored=CoMotionClip().evaluate(paired_rig(),pair['receipt']['window']['time_seconds'],[])['weapon_relative']
  for n,finger in pair['wrist_from_fingers'].items():
   expected=p.multiply(p.multiply(authored[n],p.inverse_rigid(authored['Magazine'])),item)
   self.assertLess(max(abs(a-b) for a,b in zip(p.multiply(finger,frame),expected)),1e-8)
 def test_receiver_and_motion_choose_vertical_not_widest_axis(self):
  pair=self.pair();g=p.contact_proposal(drum(),at(),body(),{},pair)
  self.assertEqual(g['design']['long_axis'],0);self.assertEqual(g['design']['entry_axis'],1)
  self.assertEqual(g['design']['inward_direction'],[0,1.,0]);self.assertEqual(g['attached_item'],at())
  self.assertEqual(g['design']['ux_defaults'],p.UX)
  seated=p.multiply(g['item_from_insertion'],g['attached_item']);entry=copy.deepcopy(g['weapon_from_entry'])
  entry[12:15]=p.add(entry[12:15],p.scale(entry[8:11],p.UX['travel_m']))
  self.assertLess(max(abs(a-b) for a,b in zip(entry,seated)),1e-9)
 def test_receiver_ambiguity_and_wrong_withdrawal_reject(self):
  pair=self.pair()
  for points in ([],[[0,0,0]]):
   with self.assertRaises(ValueError):p.contact_proposal(drum(),at(),points,{},pair)
  with self.assertRaisesRegex(ValueError,'disagrees'):
   p.contact_proposal(drum(),at(),body(),{},self.pair('lateral'))
 def test_far_carried_predecessor_branch_keeps_exact_schema(self):
  pair=p.paired_reload_grasp(PairedClip(),paired_rig(),'Magazine',at(),fixture()[0])
  self.assertNotIn('selection_mode',pair['receipt']);self.assertNotIn('entry_probe',pair['receipt'])
  self.assertNotIn('co_motion_travel_m',pair['receipt']['window'])
 def test_header_requires_motion_and_axis_receipts(self):
  row=profile();row['geometry']=p.contact_proposal(drum(),at(),body(),{},self.pair())
  row['bones']['magazine']='Magazine';row['reload_clip']={'sha256':'6'*64}
  row['profile_digest']=p.digest({k:v for k,v in row.items() if k!='profile_digest'})
  self.assertIn('ExperimentalMagazineGeometry',p.cpp_header(report(row),{'SyntheticRifle'}))
  for mode in ('travel','axis','native','unknown','missing_probe','direction','criteria'):
   r=copy.deepcopy(row);pair=r['geometry']['design']['paired_reload_grasp']
   if mode=='travel':pair['window']['co_motion_travel_m']=.01
   if mode=='axis':r['geometry']['design']['entry_evidence']['direction_agreement']=.5
   if mode=='native':pair['entry_probe']['native_trajectory_verified']=True
   if mode=='unknown':pair['selection_mode']='guess'
   if mode=='missing_probe':del pair['entry_probe']['samples']
   if mode=='direction':pair['entry_probe']['outward_in_item']=[1,0,0]
   if mode=='criteria':pair['criteria']['max_wrist_translation_m']=.02
   r['profile_digest']=p.digest({k:v for k,v in r.items() if k!='profile_digest'})
   with self.subTest(mode=mode),self.assertRaises(ValueError):p.cpp_header(report(r),{'SyntheticRifle'})

 def test_valid_receipt_cannot_be_transplanted_to_wrong_rail(self):
  row=profile();row['geometry']=p.contact_proposal(drum(),at(),body(),{},self.pair())
  row['bones']['magazine']='Magazine';row['reload_clip']={'sha256':'6'*64}
  for mode in ('closed_x_rail','reported_axis','reported_direction','entry_only'):
   r=copy.deepcopy(row);g=r['geometry']
   if mode=='closed_x_rail':
    # Internally closed and rigid, but unrelated to the genuine Y witness.
    insertion=p.frame([0.,1.,0.],[0.,0.,1.],[1.,0.,0.],g['item_from_insertion'][12:15])
    g['item_from_insertion']=insertion;g['weapon_from_entry']=p.multiply(insertion,g['attached_item'])
    g['weapon_from_entry'][12:15]=p.sub(g['weapon_from_entry'][12:15],p.scale(g['weapon_from_entry'][8:11],p.UX['travel_m']))
   elif mode=='reported_axis':g['design']['entry_axis']=0
   elif mode=='reported_direction':g['design']['inward_direction']=[1.,0.,0.]
   else:g['weapon_from_entry'][12]+=.01
   r['profile_digest']=p.digest({k:v for k,v in r.items() if k!='profile_digest'})
   with self.subTest(mode=mode),self.assertRaisesRegex(ValueError,'emitted rail'):
    p.cpp_header(report(r),{'SyntheticRifle'})

if __name__=='__main__':unittest.main()
