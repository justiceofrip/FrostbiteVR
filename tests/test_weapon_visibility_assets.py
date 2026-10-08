from pathlib import Path
import struct,sys,unittest
sys.path.insert(0,str(Path(__file__).parents[1]/'tools'))
from inspect_weapon_visibility import geometry,bone_hash

def sample(stride=48,weights=(255,0,0,0),palette=(11,),indices=(0,1,2),influences=1):
    header=b'\x01\0\0\0\0\0\x01'+b'test\0'+struct.pack('<4I4BH',1,3,0,0,stride,3,influences,len(palette),len(palette))+struct.pack('<'+'H'*len(palette),*palette)
    vertices=[]
    for n in range(3):
        pos=struct.pack('<4e',float(n),.2,.3,1) if stride in (16,48) else struct.pack('<3f',float(n),.2,.3)
        bonebytes=bytes((0,1,0,0))+bytes(weights)
        vertices.append((pos+bonebytes).ljust(stride,b'\0'))
    data=header+struct.pack('<3I',stride*3,6,0)+b''.join(vertices)+struct.pack('<3H',*indices)
    lod={'section_count':1,'buffer_bytes':stride*3+6+12,'palette':{11:bone_hash('jntWpn_1'),12:bone_hash('LeftHand')}}
    known={bone_hash('jntWpn_1'):'jntWpn_1',bone_hash('LeftHand'):'LeftHand'}
    return data,lod,known

class AssetTests(unittest.TestCase):
    def test_half_and_float_layout(self):
        for stride in (16,48,32,64,68):
            r=geometry(*sample(stride))[0]
            self.assertEqual(r['bone_names'],['jntWpn_1']);self.assertTrue(r['all_vertices_normalized'])
    def test_all_weighted_influences_are_reported(self):
        r=geometry(*sample(weights=(204,51,0,0),palette=(11,12),influences=2))[0]
        self.assertEqual(set(r['bone_names']),{'jntWpn_1','LeftHand'})
        self.assertEqual(r['multi_weight_vertices'],3)
    def test_alias_palette_entries_are_not_lost(self):
        r=geometry(*sample(weights=(204,51,0,0),palette=(11,11),influences=2))[0]
        self.assertEqual(r['bone_names'],['jntWpn_1'])
    def test_malformed_weights_indices_and_layout_reject(self):
        for args in [dict(weights=(254,0,0,0)),dict(indices=(0,1,3)),dict(stride=20),dict(weights=(204,51,0,0),palette=(11,),influences=2)]:
            with self.assertRaises(ValueError):geometry(*sample(**args))

if __name__=='__main__':unittest.main()
