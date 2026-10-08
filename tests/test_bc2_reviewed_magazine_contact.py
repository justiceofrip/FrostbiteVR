from pathlib import Path
import copy,json,sys,unittest
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
import bc2_authored_magazine_geometry as g
import bc2_reviewed_magazine_contact as reviewed
from test_bc2_authored_magazine_geometry import PairedClip,paired_rig,fixture,at
def measured_fixture():
 clip=PairedClip('finger_motion');sk=paired_rig();part=fixture()[0];diagnostics={}
 try:g.paired_reload_grasp(clip,sk,'Magazine',at(),part,diagnostics)
 except ValueError:pass
 windows=[w for w in diagnostics['windows'] if w['failed']==['finger_angle_rad'] and w['minimum_item_motion_m']>g.PAIRED_GRASP['min_item_motion_m']]
 w=windows[len(windows)//2];time=(w['begin_seconds']+w['end_seconds'])/2;pose=clip.evaluate(sk,time,[])['weapon_relative'];wrist=pose['LeftHand']
 pair=dict(item_from_hand=g.multiply(wrist,g.inverse_rigid(pose['Magazine'])),wrist_from_fingers={n:g.multiply(pose[n],g.inverse_rigid(wrist)) for n in g.FINGERS})
 review=dict(schema='fvr.bc2.reviewed-authored-magazine-contact.v1',native_asset_name='fixture',weapon=dict(resource='fixture.dbx',sha256='a'*64,instance_guid='fixture'),
  binding_digest='b'*64,mesh_sha256='c'*64,lod_sha256='d'*64,skeleton_sha256=sk.sha256,rig_fingerprint=sk.fingerprint,clip_resource='reload.res',clip_sha256=clip.sha256,
  render_receipt_sha256='e'*64,contact_sheet_sha256='f'*64,pose_digest=g.digest(pair),semantic_role='detachable_magazine',magazine_bone='Magazine',contact='complete_same_authored_frame',frame_seconds=time,native_role_verified=False,headset_tested=False)
 review['review_digest']=g.digest(review)
 return review,clip,sk,part,pair
class ReviewedContact(unittest.TestCase):
 def test_selected_role_and_complete_pose_are_exact_not_per_gun_logic(self):
  r,clip,sk,part,expected=measured_fixture();candidates=[dict(bone='OtherPart'),dict(bone='Magazine',native_role_verified=False)]
  role,pair,reject=reviewed.select_reviewed_pair(r,candidates,{'Magazine':part},{'weapon_relative':{'Magazine':at()}},clip,sk)
  self.assertEqual(role['bone'],'Magazine');self.assertEqual(reject[0]['bone'],'OtherPart')
  self.assertEqual({k:pair[k] for k in expected},expected);self.assertFalse(pair['receipt']['active_native_animation_verified'])
 def test_mutated_role_frame_pose_or_native_claim_reject(self):
  original,clip,sk,part,_=measured_fixture()
  for fault in range(7):
   r=copy.deepcopy(original)
   if fault==0:r['magazine_bone']='OtherPart'
   elif fault==1:r['frame_seconds']+=.001
   elif fault==2:r['pose_digest']='0'*64
   elif fault==3:r['native_role_verified']=True
   elif fault==4:r['headset_tested']=True
   elif fault==5:r['semantic_role']='slide'
   else:r['frame_seconds']=100.
   r['review_digest']=g.digest({k:v for k,v in r.items() if k!='review_digest'})
   with self.subTest(fault=fault),self.assertRaises(ValueError):reviewed.select_reviewed_pair(r,[dict(bone='Magazine')],{'Magazine':part},{'weapon_relative':{'Magazine':at()}},clip,sk)
 def test_all_rigid_contact_requirements_remain_mandatory(self):
  r,_,sk,part,_=measured_fixture()
  for mode in ('sliding','disconnected','not_removed'):
   with self.subTest(mode=mode),self.assertRaises(ValueError):reviewed.select_reviewed_pair(r,[dict(bone='Magazine')],{'Magazine':part},{'weapon_relative':{'Magazine':at()}},PairedClip(mode),sk)
 def test_review_context_cannot_cross_configuration_clip_or_binding(self):
  r,clip,sk,_,_=measured_fixture();profile=dict(native_asset_name=r['native_asset_name'],weapon=r['weapon'],binding_digest=r['binding_digest'])
  reviewed.verify_context(r,profile,clip,sk,r['clip_resource'],r['mesh_sha256'],r['lod_sha256'])
  for key in ('native_asset_name','weapon','binding_digest','mesh_sha256','lod_sha256','skeleton_sha256','clip_sha256','clip_resource'):
   wrong=copy.deepcopy(r);wrong[key]=dict(r['weapon'],resource='other.dbx') if key=='weapon' else '0'*64
   wrong['review_digest']=g.digest({k:v for k,v in wrong.items() if k!='review_digest'})
   with self.subTest(key=key),self.assertRaises(ValueError):reviewed.verify_context(wrong,profile,clip,sk,r['clip_resource'],r['mesh_sha256'],r['lod_sha256'])
 def test_duplicate_or_non_opted_in_annotations_reject(self):
  r,*_=measured_fixture()
  for rows,assets in (([r,r],{'fixture'}),([r],set())):
   with self.assertRaises(ValueError):reviewed.index_reviews(rows,assets)
 def test_exact_modular_contact_validates_and_missing_rail_cannot_emit_header(self):
  folder=ROOT/'profiles/reviewed-contact241-mp443';doc=json.loads((folder/'exact-geometry.json').read_text())
  self.assertEqual(len(doc['profiles']),0)
  with self.assertRaises(ValueError):g.cpp_header(doc,{'MP443_sp'})
  p=reviewed.validate_contact_candidate(doc['reviewed_contact_candidates'][0]);self.assertEqual(p['bones']['magazine'],'jntWpn_12');self.assertEqual(len(p['assembly']['members']),3)
  self.assertEqual(p['pair']['receipt']['selection_mode'],'reviewed_authored_frame');self.assertFalse(p['runtime_admitted']);self.assertEqual(p['rail_status'],'unresolved')
 def test_forged_export_window_or_review_cannot_hide_behind_new_profile_digest(self):
  original=json.loads((ROOT/'profiles/reviewed-contact241-mp443/exact-geometry.json').read_text())
  for fault in range(6):
   doc=copy.deepcopy(original);p=doc['reviewed_contact_candidates'][0];r=p['pair']['receipt']
   if fault==0:r['window']['max_wrist_translation_m']=.1
   elif fault==1:r['window']['minimum_item_motion_m']=0
   elif fault==2:r['window']['time_seconds']+=1/60
   elif fault==3:p['role_evidence']['review_digest']='0'*64
   elif fault==4:r['semantic_contact_review']['magazine_bone']='jntWpn_3'
   else:r['temporal_finger_motion_is_pose_selection_not_rigid_contact']=False
   p['candidate_digest']=g.digest({k:v for k,v in p.items() if k!='candidate_digest'})
   with self.subTest(fault=fault),self.assertRaises(ValueError):reviewed.validate_contact_candidate(p)
if __name__=='__main__':unittest.main()
