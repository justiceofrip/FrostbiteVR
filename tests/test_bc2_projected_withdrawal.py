"""Synthetic math regressions; no native/gameplay claims."""
import copy,json,sys,unittest
from pathlib import Path
HERE=Path(__file__).resolve().parent;ROOT=HERE.parent
sys.path.insert(0,str(ROOT/'tools'))
import bc2_authored_magazine_geometry as m
def fixture(axis=1):
 attached=m.identity();samples=[]
 for i,d in enumerate((0,.015,.036,.067,.11)):
  item=m.identity();item[12+axis]=-d;samples.append(dict(time=i/60,item=item))
 return samples,attached,dict(points=[[0,0,0],[.025,.166,.083]])
class Withdrawal(unittest.TestCase):
 def test_near_corner_tie_needs_unique_independent_direction(self):
  # Rounded measured SCAR end distances. The axial end is9.3mm farther than
  # a side corner, but85.6mm closer than its opposite end.
  ends=[dict(axis=a,sign=s,distance_to_body_m=d) for a,s,d in ((2,1,.0172),(1,1,.0265),(0,1,.027),(0,-1,.027),(2,-1,.1052),(1,-1,.1121))]
  best,agreement,resolved=m.select_withdrawal_entry_end(ends,[0,-1,0])
  self.assertTrue(resolved);self.assertEqual((best['axis'],best['sign'],agreement),(1,1,1))
  for case in range(5):
   bad=copy.deepcopy(ends);direction=[0,-1,0]
   if case==0:bad[1]['distance_to_body_m']=.04
   elif case==1:bad[-1]['distance_to_body_m']=.03
   elif case==2:direction=[-.707,-.707,0]
   elif case==3:bad.pop()
   else:bad[1]['distance_to_body_m']=float('nan')
   with self.subTest(case=case),self.assertRaises(ValueError):m.select_withdrawal_entry_end(bad,direction)
 def test_thin_axial_measurement(self):
  s,a,p=fixture();r=m.initial_withdrawal(s,a,p)
  self.assertEqual(len(r['samples']),2);self.assertEqual(r['maximum_probe_m'],.1)
  self.assertEqual(r['maximum_probe_basis'],'projected_rigid_extent_along_measured_withdrawal')
  self.assertEqual(r['outward_in_item'],[0,-1,0]);self.assertAlmostEqual(r['projected_extent_m'],.166)
 def test_transverse_thickness_is_not_axial_length(self):
  s,a,p=fixture(0)
  with self.assertRaises(ValueError):m.initial_withdrawal(s,a,p)
 def test_existing_success_is_identical(self):
  s,a,p=fixture();p['points'][1]=[.1,.166,.12]
  self.assertEqual(m.initial_withdrawal(s,a,p),m._initial_withdrawal_probe(s,a,.1))
 def test_failures_remain(self):
  for case in range(6):
   s,a,p=fixture()
   if case==0:s[0]['item'][12]=.02
   elif case==1:s[3]['time']+=1
   elif case==2:s[2]['item'][13]=-.11
   elif case==3:s[3]['item'][13]=+.067
   elif case==4:s[3]['item'][13]=-.04
   else:
    s[2]['item'][0]=s[2]['item'][5]=0;s[2]['item'][1]=1;s[2]['item'][4]=-1
   with self.subTest(case=case),self.assertRaises(ValueError):m.initial_withdrawal(s,a,p)
class MeasuredScarExport(unittest.TestCase):
 def test_exact_installed_metadata_reproduces_compiled_geometry(self):
  folder=ROOT/'profiles/resource-enrollment235-scar'
  doc=json.loads((folder/'exact-geometry.json').read_text())
  self.assertEqual({p['native_asset_name'] for p in doc['profiles']},{'SCAR_sp','SCAR_sp_s'})
  self.assertFalse(doc['gaps'])
  self.assertEqual(m.cpp_header(doc,{'SCAR_sp','SCAR_sp_s'}),(folder/'ScarGeometry.h').read_text())
 def test_recomputed_digest_cannot_hide_malformed_entry_receipt(self):
  original=json.loads((ROOT/'profiles/resource-enrollment235-scar/exact-geometry.json').read_text())
  for case in range(8):
   doc=copy.deepcopy(original);p=doc['profiles'][0];g=p['geometry'];entry=g['design']['entry_evidence']
   probe=g['design']['paired_reload_grasp']['entry_probe']
   if case==0:entry['ends'].pop()
   elif case==1:entry['ends'][0]['distance_to_body_m']=-.01
   elif case==2:entry['axis']=(entry['axis']+1)%3
   elif case==3:entry['inward_sign']*=-1
   elif case==4:entry['direction_agreement']=.96
   elif case==5:probe['outward_in_item'][0]+=.01
   elif case==6:g['item_from_insertion'][8]+=.01
   else:entry['native_axis_verified']=True
   p['profile_digest']=m.digest({k:v for k,v in p.items() if k!='profile_digest'})
   with self.subTest(case=case),self.assertRaises(ValueError):m.cpp_header(doc,{'SCAR_sp','SCAR_sp_s'})
if __name__=='__main__':unittest.main()
