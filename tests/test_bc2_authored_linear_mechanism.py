import math
from pathlib import Path
import sys
import unittest
from types import SimpleNamespace
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from bc2_granny_curves import Curve,IDENTITY
from bc2_authored_linear_mechanism import measure,projection_extrema

def curve(points,degree=1,knots=None):return Curve('fixture',degree,3,tuple(knots or range(len(points))),tuple(map(tuple,points)))
def constant(d,value=None):return Curve('fixture',0,d,(0.,),(tuple(IDENTITY[d] if value is None else value),))
def fixture(position,orientation=None):
    sk=SimpleNamespace(names=['root','weapon','part'],parents=[-1,0,1],sha256='skeleton',fingerprint='rig')
    tracks={n:(constant(3),constant(4),constant(9)) for n in sk.names}
    closed=SimpleNamespace(duration=3.,tracks=dict(tracks),sha256='closed')
    tracks['part']=(position,orientation or constant(4),constant(9))
    moving=SimpleNamespace(duration=3.,tracks=tracks,sha256='moving')
    return moving,closed,sk

class LinearMechanism(unittest.TestCase):
    def test_linear_peak_and_reflection(self):
        a,b,s=fixture(curve([(0,0,0),(0,0,-.12),(0,0,0)]))
        p=measure(a,b,s,'part','weapon')
        self.assertEqual(p['axis'],[0.,0.,1.]);self.assertAlmostEqual(p['decoded_travel']['maximum_m'],.12)
        self.assertFalse(p['runtime_enabled']);self.assertFalse(p['native_admission'])
    def test_quadratic_interior_extremum_not_control_max(self):
        c=curve([(0,0,0),(0,0,1),(0,0,0),(0,0,0)],2)
        p=projection_extrema(c,(0,0,1),(0,0,0),3.)
        # On [1,2], the decoded projection is .5+u-u*u, u=t-1.
        self.assertAlmostEqual(p['maximum_time_seconds'],1.5)
        self.assertAlmostEqual(p['maximum_m'],.75)
        self.assertLess(p['maximum_m'],1.)
    def test_ranges_contain_decoded_values(self):
        for degree in (1,2):
            for knots in ((0.,.2,1.7,3.),(0.,0.,1.7,3.),(0.,1.,1.,3.)):
                c=curve([(1,0,0),(-2,3,1),(.3,-1,0),(0,0,0)],degree,knots)
                for axis in ((1,0,0),(0,0,1),(.6,.8,0)):
                    p=projection_extrema(c,axis,(.1,.2,.3),3.)
                    for i in range(301):
                        point=c.evaluate(i/100);v=sum((point[k]-(.1,.2,.3)[k])*axis[k] for k in range(3))
                        self.assertGreaterEqual(v,p['minimum_m']-1e-10);self.assertLessEqual(v,p['maximum_m']+1e-10)
    def test_diagonal_and_reverse_axis(self):
        for sign in (-1,1):
            a,b,s=fixture(curve([(0,0,0),(.06*sign,.08*sign,0),(0,0,0)]))
            p=measure(a,b,s,'part','weapon');self.assertAlmostEqual(p['decoded_travel']['maximum_m'],.1)
            for x,y in zip(p['axis'],(.6*sign,.8*sign,0)):self.assertAlmostEqual(x,y)
    def test_quantized_knot_beyond_duration_is_clipped(self):
        c=curve([(0,0,0),(0,0,1)],1,(0.,1.005))
        p=projection_extrema(c,(0,0,1),(0,0,0),1.)
        self.assertAlmostEqual(p['maximum_m'],1/1.005)
        self.assertEqual(p['maximum_time_seconds'],1.)
        for duration in (0,-1,True,float('inf')):
            with self.assertRaises(ValueError):projection_extrema(c,(0,0,1),(0,0,0),duration)
    def test_reference_inside_stroke_is_reported_not_silently_clamped(self):
        a,b,s=fixture(curve([(0,0,.01),(0,0,-.12),(0,0,.01)]))
        p=measure(a,b,s,'part','weapon')
        self.assertAlmostEqual(p['decoded_travel']['minimum_m'],-.01)
        self.assertAlmostEqual(p['decoded_travel']['maximum_m'],.12)
        self.assertFalse(p['reference_is_minimum_endpoint'])
        self.assertFalse(p['runtime_closed_boundary_verified'])
        self.assertFalse(p['native_admission'])
    def test_moving_shared_ancestor_cancels(self):
        a,b,s=fixture(curve([(0,0,0),(0,0,-.12),(0,0,0)]))
        a.tracks['root']=(curve([(0,0,0),(1000,2000,3000),(0,0,0)]),constant(4),constant(9))
        self.assertAlmostEqual(measure(a,b,s,'part','weapon')['decoded_travel']['maximum_m'],.12)
    def test_large_shared_rotation_uses_local_norm_proof(self):
        a,b,s=fixture(curve([(0,0,0),(0,0,-.12),(0,0,0)]))
        controls=tuple((0,math.sin(math.radians(i*15)/2),0,math.cos(math.radians(i*15)/2)) for i in range(7))
        q=Curve('fixture',2,4,tuple(i*.5 for i in range(7)),controls)
        a.tracks['root']=(constant(3),q,constant(9))
        self.assertAlmostEqual(measure(a,b,s,'part','weapon')['decoded_travel']['maximum_m'],.12)
        a.tracks['root']=(constant(3),Curve('fixture',1,4,(0.,3.),((0,0,0,1),(0,0,0,-1))),constant(9))
        with self.assertRaises(ValueError):measure(a,b,s,'part','weapon')
    def test_rotation_nonlinear_closed_and_missing_fail(self):
        for fault in range(6):
            a,b,s=fixture(curve([(0,0,0),(0,0,-.12),(0,0,0)]))
            if fault==0:a.tracks['part']=(a.tracks['part'][0],constant(4,(0,math.sin(.1),0,math.cos(.1))),constant(9))
            if fault==1:a.tracks['part']=(curve([(0,0,0),(.03,0,-.06),(0,0,-.12)]),constant(4),constant(9))
            if fault==2:b.tracks['part']=a.tracks['part']
            if fault==3:del a.tracks['root']
            if fault==4:s.parents[2]=0
            if fault==5:a.tracks['root']=(constant(3),constant(4),constant(9,(2,0,0,0,1,0,0,0,1)))
            with self.subTest(fault=fault),self.assertRaises(ValueError):measure(a,b,s,'part','weapon')
    def test_unbounded_or_absent_stroke_fails(self):
        for travel in (0,.001,1.,float('nan')):
            a,b,s=fixture(curve([(0,0,0),(0,0,travel),(0,0,0)]))
            with self.subTest(travel=travel),self.assertRaises(ValueError):measure(a,b,s,'part','weapon')
    def test_malformed_skeleton_and_duration_fail_before_cancellation(self):
        for fault in range(9):
            a,b,s=fixture(curve([(0,0,0),(0,0,-.12),(0,0,0)]))
            if fault==0:s.names[0]='weapon'
            if fault==1:s.parents.pop()
            if fault==2:s.parents[1]=1
            if fault==3:s.parents[1]=True
            if fault==4:s.parents[0]=-2
            if fault==5:s.parents[1]=2
            if fault==6:a.duration=float('nan')
            if fault==7:b.duration=True
            if fault==8:b.duration=601
            with self.subTest(fault=fault),self.assertRaises(ValueError):measure(a,b,s,'part','weapon')

if __name__=='__main__':unittest.main()
