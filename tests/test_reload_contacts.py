"""Geometry regressions for offline native reload contact candidates."""
from pathlib import Path
import sys
import unittest
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import audit_reload_contacts as contacts


class ReloadContactTests(unittest.TestCase):
    def setUp(self):self.tri=np.array([[[0.,0,0],[1,0,0],[0,1,0]]])
    def test_triangle_face_edge_vertex_contacts(self):
        for point,expected in [([.2,.3,1],[.2,.3,0]),([.8,.8,0],[.5,.5,0]),([-1,-1,0],[0,0,0])]:
            result=contacts.triangle_closest(point,self.tri)
            self.assertTrue(np.allclose(result['point'],expected))
            self.assertAlmostEqual(result['distance_m'],np.linalg.norm(np.array(point)-expected))
    def test_degenerate_and_nonfinite_geometry_rejected(self):
        for triangles in (np.zeros((1,3,3)),self.tri*np.nan):
            with self.assertRaises(ValueError):contacts.triangle_closest([0,0,1],triangles)
        with self.assertRaises(ValueError):contacts.ray_hits([0,0,0],[0,0,0],self.tri)
    def test_two_sided_ray_bounds_and_misses(self):
        for origin,direction in [([.2,.2,1],[0,0,-1]),([.2,.2,-1],[0,0,1])]:
            found=contacts.ray_hits(origin,direction,self.tri,2)
            self.assertEqual(len(found),1);self.assertAlmostEqual(found[0]['distance_m'],1)
        self.assertFalse(contacts.ray_hits([.2,.2,1],[0,0,-1],self.tri,.5))
        self.assertFalse(contacts.ray_hits([2,2,1],[0,0,-1],self.tri,2))
    def test_patch_stays_connected_and_coplanar(self):
        tris=np.concatenate([self.tri,np.array([[[1,0,0],[1,1,0],[0,1,0]],[[4,4,0],[5,4,0],[4,5,0]],[[0,0,0],[1,0,0],[0,0,1]]])])
        patch=contacts.coplanar_patch(tris,0)
        self.assertEqual(patch['triangle_indices'],[0,1]);self.assertEqual(patch['boundary_edge_count'],4)
        self.assertAlmostEqual(patch['area_m2'],1);self.assertFalse(patch['aperture_verified'])
    def test_skin_uses_actual_palette_and_units(self):
        identity=np.eye(4);moved=np.eye(4);moved[3,0]=2
        row={'units_per_meter':2,'native':identity.reshape(-1).tolist(),'native_weapon_bones':[
            {'name':'root','hidden':False,'inverse_bind':identity.reshape(-1).tolist(),'native':moved.reshape(-1).tolist()}]}
        geometry=[{'name':'part','vertices':np.array([[0,0,0],[1,0,0],[0,1,0]]),'weights':np.array([[1.,0,0,0]]*3),'bone_ids':np.array([[1,1,1,1]]*3),'indices':np.array([[0,1,2]])}]
        result=contacts.skin_sections(geometry,{1:'root'},row)
        self.assertTrue(np.allclose(result[0]['triangles'],[[[1,0,0],[2,0,0],[1,1,0]]]))
        row['native_weapon_bones'][0]['hidden']=True
        self.assertFalse(contacts.skin_sections(geometry,{1:'root'},row))
    def test_skin_reflection_and_z_only_exclusion(self):
        identity=np.eye(4).reshape(-1).tolist();row={'units_per_meter':1,'native':identity,'native_weapon_bones':[{'name':'bone','hidden':False,'inverse_bind':identity,'native':identity}]}
        part={'name':'part','vertices':np.array([[0,0,1],[1,0,1],[0,1,1]]),'weights':np.array([[1.,0,0,0]]*3),'bone_ids':np.array([[1,1,1,1]]*3),'indices':np.array([[0,1,2]])}
        result=contacts.skin_sections([part,dict(part,name='part_ZOnly')],{1:'bone'},row)
        self.assertEqual(len(result),1);self.assertTrue(np.allclose(result[0]['triangles'][:,:,2],-1))

if __name__=='__main__':unittest.main()
