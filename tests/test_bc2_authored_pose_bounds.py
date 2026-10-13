import math
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from bc2_granny_curves import Curve,IDENTITY
from bc2_weapon_animation_pipeline import Clip,identity,multiply,inverse_rigid
from bc2_authored_pose_bounds import relative_bounds,within_tolerance
from bc2_authored_support_bindings import derive,derive_bounded,header
from bc2_authored_grip_bindings import digest


def curve(dimension, values):
    return Curve('synthetic',1 if len(values)>1 else 0,dimension,
                 tuple(i/max(1,len(values)-1) for i in range(len(values))),tuple(map(tuple,values)))


def track(position=(0.,0.,0.)):
    return (curve(3,[position]),curve(4,[IDENTITY[4]]),curve(9,[IDENTITY[9]]))


def fixture():
    clip=Clip.__new__(Clip)
    clip.duration=1.;clip.basis='synthetic'
    clip.tracks={'root':track(),'weapon':track((.1,.1,.1)),'arm':track((.3,0.,0.)), 'hand':track((.2,0.,0.))}
    sk=SimpleNamespace(names=['root','weapon','arm','hand'],parents=[-1,0,0,2])
    return clip,sk


def yaw(angle):
    return (0.,math.sin(angle/2),0.,math.cos(angle/2))


class AuthoredPoseBoundsTests(unittest.TestCase):
    def test_constant_reference_is_not_native_admission(self):
        c,s=fixture();b=relative_bounds(c,s,'hand','weapon')
        self.assertEqual(b['translation_hull_bound_m'],0.)
        self.assertEqual(b['rotation_hull_bound_rad'],0.)
        self.assertEqual(b['reference_pose'],c.evaluate(s,0,['hand'],'weapon')['weapon_relative']['hand'])
        self.assertFalse(b['runtime_admission']);self.assertFalse(b['target_runtime_interpolation_verified'])
        self.assertTrue(within_tolerance(b,.00025,math.radians(.05)))

    def test_common_parent_motion_cancels_but_evaluation_stays_strict(self):
        c,s=fixture()
        c.tracks['root']=(curve(3,[(0,0,0),(.7,.3,.5)]),curve(4,[yaw(0),yaw(.01)]),track()[2])
        b=relative_bounds(c,s,'hand','weapon')
        self.assertEqual(b['common_ancestor'],'root')
        self.assertEqual(b['translation_hull_bound_m'],0.)
        self.assertEqual(b['rotation_hull_bound_rad'],0.)
        self.assertEqual(c.evaluate(s,.5,['hand'],'weapon')['evaluation_status'],'decoded_spline_candidate')
        self.assertLess(max(abs(x-y) for x,y in zip(c.evaluate(s,.5,['hand'],'weapon')['weapon_relative']['hand'],b['reference_pose'])),1e-12)

    def test_translation_interior_control_is_not_hidden_by_equal_endpoints(self):
        c,s=fixture();old=c.tracks['hand']
        c.tracks['hand']=(curve(3,[(.2,0,0),(.203,0,0),(.2,0,0)]),old[1],old[2])
        b=relative_bounds(c,s,'hand','weapon')
        self.assertAlmostEqual(b['translation_hull_bound_m'],.003)
        self.assertFalse(within_tolerance(b,.00025,math.radians(.05)))
        self.assertEqual(c.evaluate(s,0,['hand'],'weapon')['weapon_relative'],c.evaluate(s,1,['hand'],'weapon')['weapon_relative'])

    def test_parent_rotation_bounds_long_child_lever(self):
        c,s=fixture();old=c.tracks['arm']
        c.tracks['arm']=(old[0],curve(4,[yaw(0),yaw(.001)]),old[2])
        b=relative_bounds(c,s,'hand','weapon')
        self.assertGreaterEqual(b['translation_hull_bound_m'],.00019)
        self.check_samples(c,s,b)

    def test_reference_rotation_and_translation_both_contribute(self):
        c,s=fixture();old=c.tracks['weapon']
        c.tracks['weapon']=(curve(3,[(.1,.1,.1),(.10002,.10003,.1)]),curve(4,[yaw(0),yaw(.0002)]),old[2])
        b=relative_bounds(c,s,'hand','weapon')
        self.assertGreater(b['translation_hull_bound_m'],math.hypot(.00002,.00003))
        self.check_samples(c,s,b)

    def test_large_cancelled_world_origin_does_not_change_reference(self):
        c,s=fixture();base=relative_bounds(c,s,'hand','weapon')
        c.tracks['root']=track((1e12,-1e12,1e12))
        result=relative_bounds(c,s,'hand','weapon')
        self.assertEqual(result['reference_pose'],base['reference_pose'])
        self.assertEqual(result['translation_bound_m'],base['translation_bound_m'])

    def test_both_branches_with_noncommuting_rotation_and_quadratic_controls(self):
        c,s=fixture()
        x=lambda a:(math.sin(a/2),0.,0.,math.cos(a/2))
        for name,orientation in [('arm',x),('hand',yaw),('weapon',x)]:
            old=c.tracks[name]
            q=curve(4,[orientation(0),orientation(.004),orientation(-.002)])
            q=Curve(q.format,2,q.dimension,q.knots,q.controls)
            p0=old[0].controls[0]
            p=curve(3,[p0,tuple(v+.00005 for v in p0),tuple(v-.00008 for v in p0)])
            c.tracks[name]=(p,q,old[2])
        self.check_samples(c,s,relative_bounds(c,s,'hand','weapon'))

    def check_samples(self,c,s,b):
        base=b['reference_pose']
        for t in (0.,.13,.27,.51,.82,1.):
            pose=c.evaluate(s,t,['hand'],'weapon')['weapon_relative']['hand']
            distance=math.dist(pose[12:15],base[12:15])
            relative=multiply(pose,inverse_rigid(base))
            angle=math.acos(max(-1.,min(1.,(relative[0]+relative[5]+relative[10]-1)/2)))
            self.assertLessEqual(distance,b['translation_bound_m'])
            self.assertLessEqual(angle,b['rotation_bound_rad']+1e-7)

    def test_antipodal_quaternions_rejected_despite_same_endpoint_rotation(self):
        c,s=fixture();old=c.tracks['hand'];c.tracks['hand']=(old[0],curve(4,[(0,0,0,1),(0,0,0,-1)]),old[2])
        with self.assertRaisesRegex(ValueError,'normalization'):relative_bounds(c,s,'hand','weapon')

    def test_nonrigid_shared_parent_cannot_be_hidden_by_cancellation(self):
        c,s=fixture();old=c.tracks['root'];scaled=list(IDENTITY[9]);scaled[0]=1.00001
        c.tracks['root']=(old[0],old[1],curve(9,[IDENTITY[9],scaled]))
        with self.assertRaisesRegex(ValueError,'identity scale'):relative_bounds(c,s,'hand','weapon')

    def test_unrelated_bad_track_does_not_affect_requested_reference(self):
        c,s=fixture();c.tracks['unrelated']=None
        self.assertEqual(relative_bounds(c,s,'hand','weapon')['translation_hull_bound_m'],0.)

    def test_missing_track_and_invalid_hierarchy_rejected(self):
        c,s=fixture();del c.tracks['arm']
        with self.assertRaisesRegex(ValueError,'absent'):relative_bounds(c,s,'hand','weapon')
        c,s=fixture();s.parents[2]=3
        with self.assertRaisesRegex(ValueError,'parent order'):relative_bounds(c,s,'hand','weapon')

    def test_same_bone_identity_and_unconnected_roots(self):
        c,s=fixture()
        self.assertEqual(relative_bounds(c,s,'weapon','weapon')['translation_hull_bound_m'],0.)
        s.parents[1]=-1
        b=relative_bounds(c,s,'hand','weapon')
        self.assertIsNone(b['common_ancestor']);self.assertEqual(b['translation_hull_bound_m'],0.)

    def test_malformed_curve_and_tolerance_rejected(self):
        for value in (float('nan'),float('inf'),True):
            c,s=fixture();old=c.tracks['hand'];c.tracks['hand']=(curve(3,[(value,0,0)]),old[1],old[2])
            with self.assertRaises(ValueError):relative_bounds(c,s,'hand','weapon')
        c,s=fixture();b=relative_bounds(c,s,'hand','weapon')
        for value in (-1,float('nan'),float('inf'),True):
            with self.assertRaises(ValueError):within_tolerance(b,value,.1)
        b['translation_bound_m']=-1
        with self.assertRaises(ValueError):within_tolerance(b,.1,.1)


def support_fixture():
    c,s=fixture()
    c.tracks['jntWpn_1']=c.tracks.pop('weapon')
    c.tracks['LeftHand']=c.tracks.pop('hand')
    s.names=['root','jntWpn_1','arm','LeftHand']
    old=c.tracks['LeftHand']
    c.tracks['LeftHand']=(curve(3,[(.2,0,0),(.20002,0,0)]),old[1],old[2])
    c.sha256='1'*64;s.sha256='2'*64;s.fingerprint='fnv1a64:0123456789abcdef'
    row=dict(native_asset_name='Synthetic',configured_mesh_path='Objects/Synthetic',rig_fingerprint=s.fingerprint,
             animation_sha256=c.sha256,skeleton_sha256=s.sha256,left_hand_in_weapon=c.evaluate(s,0,['LeftHand'])['weapon_relative']['LeftHand'],
             left_hand_status='decoded_spline_candidate',right_hand_status='decoded_spline_candidate',state_count=1,state_index=0,
             weapon={'resource':'Synthetic.dbx'},weapon_class='wcAssault',animation_chain={'resource':'Synthetic.res'})
    row['binding_digest']=digest(row)
    return c,s,dict(schema='fvr.bc2.authored_reload_reference_bindings',runtime_admission=False,profiles=[row])


class BoundedSupportTests(unittest.TestCase):
    def derive(self,c,s,doc):
        return derive_bounded(doc,{'Synthetic'},{(c.sha256,s.sha256):(c,s)},translation_m=.00025,
                              rotation_rad=math.radians(.05),enable_contact=True)

    def test_opt_in_reference_keeps_default_strict_and_right_hand_unmodified(self):
        c,s,d=support_fixture()
        with self.assertRaisesRegex(ValueError,'constant'):derive(d,{'Synthetic'},enable_contact=True)
        result=self.derive(c,s,d);row=result['profiles'][0]
        self.assertEqual(row['left_hand_status'],'bounded_decoded_reference')
        self.assertFalse(row['right_hand_retargeted']);self.assertFalse(row['native_admission'])
        self.assertFalse(result['runtime_admission'])
        self.assertEqual(len(row['reference_envelopes']),1)
        self.assertEqual(row['binding_digest'],digest({k:v for k,v in row.items() if k!='binding_digest'}))
        self.assertIn('AuthoredSupportProfile',header(result))

    def test_changed_resources_pose_and_source_digest_fail(self):
        c,s,d=support_fixture();s.fingerprint='fnv1a64:ffffffffffffffff'
        with self.assertRaisesRegex(ValueError,'identity'):self.derive(c,s,d)
        c,s,d=support_fixture();d['profiles'][0]['left_hand_in_weapon'][12]+=.1
        with self.assertRaisesRegex(ValueError,'reproduce'):self.derive(c,s,d)
        c,s,d=support_fixture();d['profiles'][0]['weapon']['resource']='Changed.dbx'
        with self.assertRaisesRegex(ValueError,'digest'):self.derive(c,s,d)

    def test_large_envelope_fails_and_static_output_is_identical(self):
        c,s,d=support_fixture();old=c.tracks['LeftHand']
        c.tracks['LeftHand']=(curve(3,[(.2,0,0),(.203,0,0)]),old[1],old[2])
        with self.assertRaisesRegex(ValueError,'exceeds'):self.derive(c,s,d)
        row=d['profiles'][0];row['left_hand_status']='static_authored_pose'
        row['binding_digest']=digest({k:v for k,v in row.items() if k!='binding_digest'})
        # Existing strict references never enter the opt-in bound machinery.
        self.assertEqual(self.derive(c,s,d),derive(d,{'Synthetic'},enable_contact=True))


if __name__=='__main__':unittest.main()
