"""Read-only mesh-link resolver mutation tests using synthetic memory only."""
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from capture_reload_state import Inspector, Image


class Memory:
    base=0x400000
    def __init__(self):self.bytes={};self.calls={};self.mutate=None
    def put(self,at,data):self.bytes.update({at+i:value for i,value in enumerate(data)})
    def u(self,at,*values):self.put(at,struct.pack('<'+'I'*len(values),*values))
    def read(self,at,size):
        key=(at,size);self.calls[key]=self.calls.get(key,0)+1
        if self.mutate:self.mutate(self,key,self.calls[key])
        try:return bytes(self.bytes[at+i] for i in range(size))
        except KeyError:raise ValueError('Synthetic read outside fixture')
    def u32(self,at):return struct.unpack('<I',self.read(at,4))[0]


class FileImage:
    base=0x400000
    def __init__(self,blobs):self.blobs=blobs
    def find(self,_):return 0x100
    def read(self,at,size):return self.blobs[at][:size]
    def section(self,at,size=1):
        if not 0<=at<0x200000:raise ValueError('Outside synthetic image')
        return (0,0x200000,0,0x200000,0)


class Fixture(Inspector):
    def __init__(self):
        self.p=Memory();self.types={}
        self.image=FileImage({0x100:b'P'*0x3e,0x1000:struct.pack('<3I',0x402000,0x402020,0x402010),0x2000:bytes.fromhex('8b410cc3'),0x2010:bytes.fromhex('8b4110c3')})
        for at,data in self.image.blobs.items():self.p.put(self.p.base+at,data)
        self.weapon_value={'weapon':0x800000,'data':0x810000,'asset_name':'SPAS12_sp'}
        self.p.u(0x800004,0x810000);self.p.u(0x810088,0,0x410200,0x820000,0x8200c8)
        self.p.u(0x820080,0x401000,0,1,1,0x830000);self.p.u(0x830000,0x840000)
        self.p.u(0x84000c,0x850000);self.p.put(0x850000,b'Objects/Weapons/Test/SPAS_Mesh\0')
        self.metadata={
          0x410100:{'address':0x410100,'name':'SoldierWeaponData','flags':0x35,'size':304,'fields':[{'name':'WeaponStates','offset':0x88,'type':'ArrayBase','element_info':0x410200}]},
          0x410200:{'address':0x410200,'name':'WeaponStateData','flags':0x29,'size':200,'fields':[{'name':'Meshes1p','offset':0x80,'type':'ArrayBase','element_info':0x410300}]},
          0x410300:{'address':0x410300,'name':'SkinnedMeshAsset','flags':0x35,'size':68,'fields':[]},
          0x410400:{'address':0x410400,'name':'Asset','flags':0x35,'size':16,'fields':[{'name':'Name','offset':12,'type':'String'}]},
          0x410500:{'address':0x410500,'name':'DataContainer','flags':0x35,'size':12,'fields':[]}}
        self.p.u(0x410314,0x410400);self.p.u(0x410414,0x410500);self.p.u(0x410514,0x410500)
    def info(self,at):return self.metadata[at]
    def object_info(self,at):
        return self.metadata[{0x810000:0x410100,0x840000:0x410300}[at]]
    def run(self):return self.mesh_links(self.weapon_value)


class MeshLinkTests(unittest.TestCase):
    def test_verified_link_and_inherited_name(self):
        result=Fixture().run()
        self.assertTrue(result['identity_coherent'])
        self.assertFalse(result['atomic_native_snapshot'])
        self.assertEqual(result['states'][0]['meshes'][0]['asset_path'],'Objects/Weapons/Test/SPAS_Mesh')
        self.assertEqual(result['states'][0]['getter_proof']['data']['field_offset'],16)
    def test_wrong_field_offset_or_element(self):
        for owner,field,changed in [(0x410100,'offset',0x90),(0x410200,'offset',0x84),(0x410200,'element_info',0x410400)]:
            fixture=Fixture();fixture.metadata[owner]['fields'][0][field]=changed
            with self.assertRaises(ValueError):fixture.run()
    def test_malformed_state_and_mesh_array_bounds(self):
        for at,values in [(0x810090,(0x820000,0x8200c9)),(0x820088,(1,9)),(0x820088,(0,1)),(0x820090,(0xfffffff0,))]:
            fixture=Fixture();fixture.p.u(at,*values)
            with self.assertRaises(ValueError):fixture.run()
    def test_wrong_getter_code_or_table(self):
        for at in (0x401000,0x402000,0x402010,0x400100):
            fixture=Fixture();fixture.p.put(at,b'X')
            with self.assertRaises(ValueError):fixture.run()
    def test_array_change_rejected(self):
        fixture=Fixture()
        def mutate(memory,key,count):
            if key==(0x820080,20) and count==2:memory.u(0x82008c,0)
        fixture.p.mutate=mutate
        with self.assertRaises(ValueError):fixture.run()
    def test_name_change_rejected(self):
        fixture=Fixture()
        def mutate(memory,key,count):
            if key==(0x84000c,4) and count==2:memory.u(0x84000c,0x850100)
        fixture.p.mutate=mutate
        with self.assertRaises(ValueError):fixture.run()
    def test_parent_cycle_and_ambiguous_name(self):
        fixture=Fixture();fixture.p.u(0x410414,0x410300)
        with self.assertRaises(ValueError):fixture.run()
        fixture=Fixture();fixture.metadata[0x410300]['fields']=[{'name':'Name','offset':12,'type':'String'}]
        with self.assertRaises(ValueError):fixture.run()
    def test_wrong_class_and_bad_name(self):
        fixture=Fixture();fixture.metadata[0x410300]['name']='RigidMeshAsset'
        with self.assertRaises(ValueError):fixture.run()
        fixture=Fixture();fixture.p.put(0x850000,b'notapath\0')
        with self.assertRaises(ValueError):fixture.run()

if __name__=='__main__':unittest.main()
