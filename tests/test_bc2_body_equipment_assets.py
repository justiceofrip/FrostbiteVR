from pathlib import Path
import json,math,struct,sys,unittest
TOOLS=Path(__file__).resolve().parents[1]/'tools';sys.path.insert(0,str(TOOLS))
from bc2_body_equipment_assets import holster_transform,bake_section,equipment_header
from bc2_weapon_animation_pipeline import identity
from bc2_authored_magazine_geometry import point

class EquipmentGeometry(unittest.TestCase):
    def test_grip_at_handle_and_muzzle_up_across_models(self):
        for grip,muzzle in (([.03,-.14,-.27],[0,-.01,-1.03]),([.2,.3,.1],[.1,1.,-.4]),([0,0,0],[0,1,0])):
            transform=holster_transform(grip,muzzle);handle=point(grip,transform);tip=point(muzzle,transform)
            self.assertLess(max(map(abs,handle)),1e-7)
            self.assertAlmostEqual(tip[0],0.);self.assertAlmostEqual(tip[2],0.)
            self.assertAlmostEqual(tip[1],math.dist(grip,muzzle))
    def test_invalid_axis_cannot_mint_placement(self):
        for a,b in (([0,0,0],[0,0,0]),([0,0,0],[float('nan'),0,0]),([0,0],[0,0,1])):
            with self.assertRaises(ValueError):holster_transform(a,b)
    def test_skinning_preserves_weighted_closed_pose(self):
        section={'stride':20,'vertices':1,'vertex_offset':0,'palette':[0,1]};lod={'palette':{0:7,1:8}}
        a=identity();b=identity();a[12]=1.;b[13]=2.
        vertex=struct.pack('<3f8B',.1,.2,.3,0,1,0,0,128,127,0,0)
        baked=bake_section(vertex,section,lod,{7:'jntWpn_1',8:'jntWpn_2'},
                           {'jntWpn_1':identity(),'jntWpn_2':identity()}, {'jntWpn_1':a,'jntWpn_2':b},0)[0]
        xyz=struct.unpack_from('<3f',baked)
        for x,y in zip(xyz,(.1+128/255,.2+2*127/255,.3)):self.assertAlmostEqual(x,y,places=6)
        self.assertEqual(baked[12:],bytes([0,0,0,0,255,0,0,0]))
    def test_bad_weights_nonweapon_unknown_and_nonfinite_reject(self):
        section={'stride':20,'vertices':1,'vertex_offset':0,'palette':[0]};lod={'palette':{0:7}}
        for name,weight,x in (('jntWpn_1',254,0.),('LeftHand',255,0.),('',255,0.),('jntWpn_1',255,float('nan'))):
            vertex=struct.pack('<3f8B',x,0.,0.,0,0,0,0,weight,0,0,0)
            with self.assertRaises(ValueError):bake_section(vertex,section,lod,{7:name},{name:identity()},{name:identity()},0)
    def test_public_header_reproducible_and_metadata_only(self):
        root=TOOLS.parent;rows=json.loads((root/'config/body-ammo-assets.json').read_text())['profiles']
        equipment=[r for r in rows if r.get('display_only')]
        self.assertEqual(equipment_header(equipment),(root/'src/games/bc2/Bc2BodyEquipmentProfiles.h').read_text())
        self.assertTrue(all('vertices' not in r and 'indices' not in r for r in equipment))
if __name__=='__main__':unittest.main()
