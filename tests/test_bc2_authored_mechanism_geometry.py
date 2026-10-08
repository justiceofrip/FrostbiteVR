from pathlib import Path
import sys,math,unittest
from types import SimpleNamespace
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import bc2_authored_mechanism_geometry as m
from bc2_granny_resource import Resource
from test_bc2_weapon_animation import resource

def tr(x=0.,y=0.,z=0.):
 a=m.mag.identity();a[12:15]=[x,y,z];return a
def rz(angle):
 c,s=math.cos(angle),math.sin(angle)
 return [c,s,0.,0.,-s,c,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.]
def rx(angle):
 c,s=math.cos(angle),math.sin(angle)
 return [1.,0.,0.,0.,0.,c,s,0.,0.,-s,c,0.,0.,0.,0.,1.]
def part():
 return {'points':[[-.025,-.08,-.03],[.025,.08,.03]],'triangles':80,'mixed_triangles':0,
 'extent':[.05,.16,.06],'minimum':[-.025,-.08,-.03],'maximum':[.025,.08,.03],'sections':[]}

class RootProjection(unittest.TestCase):
 def test_matches_full_root(self):
  r=Resource(resource());self.assertEqual(r.read_root_fields(('Value',)),{'Value':42});self.assertEqual(r.objects,1)
 def test_duplicate_missing_or_excessive_fields(self):
  for names in ((),('Value','Value'),('Wrong',),['Value']*33):
   with self.assertRaises(ValueError):Resource(resource()).read_root_fields(names)
 def test_original_value_and_object_budgets(self):
  for attr,limit in (('values',1000000),('objects',100000)):
   r=Resource(resource());setattr(r,attr,limit)
   with self.assertRaises(ValueError):r.read_root_fields(('Value',))
 def test_selected_malformed_pointer_still_rejected(self):
  with self.assertRaises(ValueError):Resource(resource(kind=2,cycle=True)).read_root_fields(('Value',))

class Motion(unittest.TestCase):
 def test_rotated_baseline_hinge(self):
  base=m.mag.multiply(rx(.6),tr(.5,.7,-.2));frames=[m.mag.multiply(rz(a),base) for a in (0,.3,.8,1.5)]
  result=m.classify(frames);self.assertEqual(result['kind'],'hinge_candidate')
  self.assertLess(result['max_translation_m'],1e-10);self.assertAlmostEqual(result['axis_in_baseline_part'][2],1.)
 def test_distinct_axes_not_single_hinge(self):
  self.assertEqual(m.classify([tr(),rz(.8),rx(.8)])['kind'],'general_rigid_rotation')
 def test_slider_and_curved_translation(self):
  self.assertEqual(m.classify([tr(),tr(0,.05),tr(0,.1)])['kind'],'slider_candidate')
  self.assertEqual(m.classify([tr(),tr(.02,.05),tr(0,.1)])['kind'],'general_rigid_translation')
 def test_moving_parent_does_not_move_local_part(self):
  local=m.mag.multiply(rz(.4),tr(.02,.03))
  parents=[tr(),m.mag.multiply(rx(.7),tr(.5,0,.4)),m.mag.multiply(rz(1.2),tr(-.2,.4,.3))]
  frames=[m.mag.multiply(m.mag.multiply(local,p),m.mag.inverse_rigid(p)) for p in parents]
  self.assertEqual(m.classify(frames)['kind'],'static')
 def test_bounds_and_invalid_pose(self):
  for frames in ([tr()], [tr()]*453, [tr(),[float('nan')]*16], [tr(),[0.]*16]):
   with self.assertRaises(ValueError):m.classify(frames)
 def test_unknown_semantics_never_admitted(self):
  r=m.classify([tr(),rz(.8)]);self.assertIsNone(r['semantic_role']);self.assertFalse(r['baseline_is_native_closed'])

class Contact(unittest.TestCase):
 def samples(self):
  samples=[]
  for i in range(19):
   t=i/30;item=tr(0,0,.15 if i else 0);hand=m.mag.multiply(tr(.02),item)
   rel={'part':item,'LeftHand':hand}
   rel.update({n:m.mag.multiply(tr(0,.001*k),hand) for k,n in enumerate(m.mag.FINGERS)})
   samples.append({'time_seconds':t,'weapon_relative':rel})
  return samples
 def test_same_frame_pair_preserves_all_fingers(self):
  r=m.paired_contacts(self.samples(),'part',part());self.assertGreater(r['windows'],0)
  pose=r['pose'];self.assertEqual(set(pose['wrist_from_fingers']),set(m.mag.FINGERS))
  expected=m.mag.multiply(pose['part_from_wrist'],pose['weapon_from_part'])
  self.assertAlmostEqual(expected[12],.02);self.assertAlmostEqual(expected[14],.15)
 def test_stationary_item_not_contact_proof(self):
  rows=self.samples()
  for r in rows:r['weapon_relative']['part']=tr();r['weapon_relative']['LeftHand']=tr(.02)
  self.assertEqual(m.paired_contacts(rows,'part',part())['windows'],0)
 def test_missing_finger_not_partial_grasp(self):
  rows=self.samples();del rows[2]['weapon_relative'][m.mag.FINGERS[0]]
  with self.assertRaises(KeyError):m.paired_contacts(rows,'part',part())
 def test_remote_hand_not_contact(self):
  rows=self.samples()
  for r in rows:
   for n in ['LeftHand',*m.mag.FINGERS]:r['weapon_relative'][n]=m.mag.multiply(r['weapon_relative'][n],tr(10))
  self.assertEqual(m.paired_contacts(rows,'part',part())['windows'],0)

class Composition(unittest.TestCase):
 def analyze(self,mixed=False,missing_hand=False):
  names=['jntWpn_1','part','LeftHand',*m.mag.FINGERS]
  sk=SimpleNamespace(names=names,parents=[-1,0,0]+[2]*15,sha256='s',fingerprint='r')
  class Clip:
   duration=.6
   def evaluate(self,sk,t,names):
    if missing_hand and 'LeftHand' in names:raise ValueError('Missing hand')
    rel={n:tr() for n in names};rel['part']=tr(0,0,t)
    return {'time_seconds':t,'weapon_relative':rel}
  p={'native_asset_name':'synthetic','configured_mesh_path':'exact','binding_digest':'b','weapon':{},'state_index':0}
  receipt={'skin_sections':[{'name':'section','bone_names':['part','weightedOnly'],'multi_weight_vertices':3 if mixed else 0}]}
  return m.analyze(p,{'part':part()},{'weapon_relative':{},'bone_evaluation_status':{}},Clip(),sk,receipt)
 def test_weighted_sections_not_promoted_to_independent_part(self):
  r=self.analyze(True);p=r['parts'][0]
  self.assertFalse(p['independent_rigid_geometry']);self.assertIsNone(p['contact']);self.assertEqual(r['skin_bones_without_rigid_part'],['weightedOnly'])
 def test_missing_hand_still_keeps_part_motion_with_gap(self):
  r=self.analyze(missing_hand=True);self.assertEqual(r['parts'][0]['motion']['kind'],'slider_candidate')
  self.assertIsNone(r['parts'][0]['contact']);self.assertTrue(r['gaps']);self.assertFalse(r['runtime_admitted'])
 def test_clip_bound(self):
  with self.assertRaises(ValueError):m.analyze({}, {}, {},SimpleNamespace(duration=16),None,{})
 def test_cached_motion_never_reuses_another_config_identity(self):
  p={'native_asset_name':'second','configured_mesh_path':'other_mesh','weapon':{'guid':'second'},'state_index':2,'binding_digest':'new'}
  base={'asset':'first','configured_mesh':'old','source':{'archive':'old'},'parts':[],'runtime_admitted':False}
  r=m.bind_cached(base,p,{'archive':'second'})
  self.assertEqual(r['configured_mesh'],'other_mesh');self.assertEqual(r['source']['archive'],'second')
  self.assertEqual(r['weapon_configuration'],p['weapon']);self.assertEqual(base['source']['archive'],'old');self.assertFalse(r['runtime_admitted'])
 def test_coverage_keeps_definitions_distinct_from_models(self):
  r=self.analyze();r['source'].update({k:'hash' for k in ('mesh_sha256','lod_sha256','sha256','asset_document_sha256')})
  data={'mechanisms':[r,{**r,'asset':'second','weapon_configuration':{'guid':'second'}}],
        'configurations':{'resolved_weapons':[{'native_name':a,'resource':a,'instance_guid':a,'fields':{}} for a in ('synthetic','second')]},
        'independent_content_jobs':1,'errors':[],'reference_gaps':[]}
  c=m.coverage(data,{'gaps':[],'profiles':[]})
  self.assertEqual((c['model_count'],c['native_asset_name_count'],c['configuration_count'],c['state_record_count']),(1,2,2,2))
  self.assertFalse(c['models'][0]['native_manual_reload_enabled']);self.assertIsNone(c['models'][0]['parts'][0]['semantic_role'])

if __name__=='__main__':unittest.main()
