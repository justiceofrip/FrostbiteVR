import copy
import struct
import unittest
from bc2_shader_database import parse_shader_database,resolve_material

def fixture():
    u=lambda *v:struct.pack('<'+'I'*len(v),*v)
    code=b'DXBC'+bytes(16)+u(1,52,1,36)+b'ISGN'+u(8,0,8)
    sized=u(len(code))+code
    key=bytes(range(16));name=b'Mesh/Material\0'
    data=bytes.fromhex('7800000000')+u(1)+u(32)+bytes(28)+u(0,0)
    shader_offset=len(data)+4+16+4
    data+=u(1)+bytes(16)+sized+bytes(12)+sized
    data+=u(1)+u(0x12345678,5,10,0,32,0,0)+u(1)+b'TEXCOORD\0'
    data+=u(1)+bytes(16)+sized
    data+=bytes(12)+u(1)+key+u(0,0,0,0,0,0,0,0)
    data+=u(0,0)+u(1)+name+key+u(0,0)
    data+=u(1)+key+u(1)+struct.pack('<H',0)
    return data,shader_offset

class MaterialTables(unittest.TestCase):
    def test_resolves_exact_pair_without_claiming_active_pass(self):
        raw,_=fixture();db=parse_shader_database(raw)
        result=resolve_material(db,'mesh','MATERIAL')
        self.assertTrue(db['fully_consumed'])
        self.assertFalse(result['active_pass_verified'])
        self.assertFalse(result['rendered_material_verified'])
        element=result['passes'][0]['vertex']['elements'][0]
        self.assertEqual((element['semantic'],element['index'],element['byte_offset']),('TEXCOORD',5,32))
        self.assertNotIn(0x12345678,element.values())

    def test_truncation_and_trailing_data_rejected(self):
        raw,_=fixture()
        for size in (0,4,7,35,len(raw)-1):
            with self.subTest(size=size),self.assertRaises(ValueError):parse_shader_database(raw[:size])
        with self.assertRaises(ValueError):parse_shader_database(raw+b'\0')

    def test_version_count_and_parameter_bounds(self):
        raw,_=fixture()
        for offset,value in ((0,119),(5,100001),(9,31),(9,0x7fffffff)):
            bad=bytearray(raw);struct.pack_into('<I',bad,offset,value)
            with self.subTest(offset=offset,value=value),self.assertRaises(ValueError):parse_shader_database(bytes(bad))

    def test_shader_size_and_chunk_offsets(self):
        raw,offset=fixture()
        for relative,value in ((20,2),(24,51),(28,100),(32,4),(32,0xfffffff0)):
            bad=bytearray(raw);struct.pack_into('<I',bad,offset+relative,value)
            with self.subTest(relative=relative,value=value),self.assertRaises(ValueError):parse_shader_database(bytes(bad))

    def test_ambiguous_missing_and_broken_links(self):
        raw,_=fixture();db=parse_shader_database(raw)
        for fault in range(6):
            bad=copy.deepcopy(db)
            if fault==0:bad['names'].append(bad['names'][0])
            if fault==1:bad['names'].clear()
            if fault==2:bad['links'].clear()
            if fault==3:bad['links'][bad['names'][0]['reference']]=[1]
            if fault==4:bad['bindings'][0]['vertex']=1
            if fault==5:bad['bindings'][0]['parameters']=1
            with self.subTest(fault=fault),self.assertRaises(ValueError):resolve_material(bad,'Mesh','Material')

if __name__=='__main__':unittest.main()
