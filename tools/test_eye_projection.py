import unittest
import numpy as np
from analyze_eye_projection import decode_geometry_camera, correlate

class ProjectionTests(unittest.TestCase):
    def test_camera_layouts_and_wrong_eye(self):
        left=np.array([[1,0,-.2,-10],[0,1,0,-20],[0,0,1,-30],[0,0,-1,0]], dtype=float)
        right=left.copy();right[0,2]=.2;right[0,3]+=1
        world=np.array([[0,0,1,500],[0,1,0,120],[-1,0,0,-200],[0,0,0,1]], dtype=float)
        cameras=[left,right]
        raw=np.concatenate([(left@world).ravel(),world.ravel()])
        result=decode_geometry_camera(raw,cameras)
        self.assertEqual(result['matches_eye_camera'],0)
        self.assertEqual(result['matrix_layout'],'world_view_projection')
        reversed_layout=np.concatenate([world.ravel(),(left@world).ravel()])
        decoded=decode_geometry_camera(reversed_layout,cameras)
        self.assertEqual(decoded['matches_eye_camera'],0)
        self.assertEqual(decoded['matrix_layout'],'world_then_world_view_projection')
        self.assertEqual(decode_geometry_camera(right.ravel(),cameras)['matches_eye_camera'],1)
        self.assertIsNone(decode_geometry_camera(np.ones(64),cameras)['matches_eye_camera'])
        self.assertIsNone(decode_geometry_camera(left.ravel(),[left,left])['matches_eye_camera'])
    def test_hybrid_projection_is_a_distinct_failure(self):
        left_view=np.eye(4);right_view=np.eye(4)
        left_view[0,3]=.032;right_view[0,3]=-.032
        left_p=np.array([[1,0,-.2,0],[0,1.1,0,0],[0,0,-1,-.1],[0,0,-1,0]],dtype=float)
        right_p=left_p.copy();right_p[0,2]=.2
        cases=[left_p@left_view,right_p@right_view,right_p@left_view,left_p@right_view]
        world=np.array([[1,0,0,500],[0,1,0,120],[0,0,1,-200],[0,0,0,1]],dtype=float)
        for owner in range(4):
            raw=np.concatenate([(cases[owner]@world).ravel(),world.ravel()])
            self.assertEqual(decode_geometry_camera(raw,cases)['matches_eye_camera'],owner)
        self.assertIsNone(decode_geometry_camera(cases[2].ravel(),cases[:2])['matches_eye_camera'])
    def test_pixel_displacement_distinguishes_mono(self):
        left=np.random.default_rng(7).normal(size=(270,480))
        for dx in [-96,0,96]:
            right=np.roll(left,dx,axis=1)
            best=max((correlate(left,right,offset),offset) for offset in range(-120,121))
            self.assertEqual(best[1],dx)

if __name__=='__main__': unittest.main()
