"""Synthetic malformed-resource regressions; contains no game assets."""
import gzip
from pathlib import Path
import struct
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import inspect_bc2_mesh_asset as mesh


def archive_bytes(payload=b'abc',compressed=True):
    names=b'asset.res\0MeshData\0'
    index=struct.pack('>2I',2,len(names))+names+struct.pack('>I6I',1,0,65536,0,len(payload),len(payload),10)+bytes([compressed])+struct.pack('>I',len(payload))
    packed=gzip.compress(index)
    return b'FbRB'+struct.pack('>I',len(packed))+packed+(gzip.compress(payload) if compressed else payload)


def fixture():
    name=b'jntWpn_7_Ammo_Brass\0'
    section=name+struct.pack('<4I4BH',1,3,0,0,16,3,1,1,1)+struct.pack('<H',40)
    vertices=b''.join(struct.pack('<4e4B4B',float(i),0,0,(-1 if i==1 else 1),0,0,0,0,255,0,0,0) for i in range(3))
    return b'\x01\0\0\0\0\0\x01'+section+struct.pack('<3I',len(vertices),6,0)+vertices+struct.pack('<3H',0,1,2)


class MeshEvidenceTests(unittest.TestCase):
    def test_raw_and_gzip_archive_exact_read(self):
        with tempfile.TemporaryDirectory() as directory:
            for compression in (False,True):
                path=Path(directory)/'fixture.fbrb';path.write_bytes(archive_bytes(compressed=compression))
                archive=mesh.Archive(path)
                self.assertEqual(archive.read_selected(['asset.res']),{'asset.res':b'abc'})
                with self.assertRaises(ValueError):archive.read_selected(['missing.res'])

    def test_archive_corruption_rejected(self):
        original=archive_bytes()
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'fixture.fbrb'
            for data in (original[:5],b'Fail'+original[4:],original[:-1],original+b'junk'):
                path.write_bytes(data)
                with self.assertRaises((ValueError,EOFError,mesh.zlib.error)):
                    mesh.Archive(path).read_selected(['asset.res'])

    def test_index_payload_bounds_and_inflation_limit(self):
        names=b'asset.res\0MeshData\0'
        index=struct.pack('>2I',2,len(names))+names+struct.pack('>I6I',1,0,65536,2,3,3,10)+b'\0'+struct.pack('>I',3)
        packed=gzip.compress(index)
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'fixture.fbrb';path.write_bytes(b'FbRB'+struct.pack('>I',len(packed))+packed+b'abc')
            with self.assertRaises(ValueError):mesh.Archive(path)
        with self.assertRaises(ValueError):mesh.inflate(gzip.compress(b'X'*100),20)

    def test_rigid_shell_palette_and_position_bounds(self):
        result=mesh.mesh_data(fixture());section=result['sections'][0]
        self.assertEqual(section['used_palette_ids'],[40])
        self.assertTrue(section['all_vertices_single_weight'])
        self.assertEqual(section['bind_position_bounds'],{'minimum':[0.,0.,0.],'maximum':[2.,0.,0.]})

    def test_vertex_palette_weight_and_index_corruption(self):
        original=fixture();start=len(original)-54
        mutations=[(start+8,1),(start+12,254),(len(original)-2,3)]
        for offset,value in mutations:
            data=bytearray(original);data[offset]=value
            with self.assertRaises(ValueError):mesh.mesh_data(data)

    def test_nonfinite_and_buffer_length_corruption(self):
        original=fixture();start=len(original)-54
        data=bytearray(original);struct.pack_into('<e',data,start,float('nan'))
        with self.assertRaises(ValueError):mesh.mesh_data(data)
        data=bytearray(original);struct.pack_into('<I',data,start-12,100)
        with self.assertRaises(ValueError):mesh.mesh_data(data)
        with self.assertRaises(ValueError):mesh.mesh_data(original[:-1])

    def test_mesh_skin_hash_mapping_and_collision(self):
        # Lower-case hash is independently corroborated by installed resource.
        self.assertEqual(mesh.bone_hash('jntWpn_7'),0xf7f9bcd4)
        data=b'\x2a\0\0\0\0lod\0'+struct.pack('<I3B',1,0,1,1)+b'\0Ammo_Brass\0'+struct.pack('<6f3I2I',0,0,0,2,1,1,66,0,1,40,0xf7f9bcd4)
        result=mesh.mesh_set(data,{'jntWpn_7'})
        self.assertEqual(result['skin_map'][0]['matched_native_name'],'jntWpn_7')
        result=mesh.mesh_set(data,{'jntWpn_7','JNTWPN_7'})
        self.assertTrue(result['skin_map'][0]['hash_collision'])
        self.assertIsNone(result['skin_map'][0]['matched_native_name'])
        with self.assertRaises(ValueError):mesh.mesh_set(data[:-1],{'jntWpn_7'})

if __name__=='__main__':unittest.main()
