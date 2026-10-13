import io,struct,unittest
from bc2_texture import texture_metadata,private_dds

def fixture(fmt=0):
    size=8 if fmt==0 else 16
    return struct.pack('<8I',3,0,fmt,0,4,4,0,1)+struct.pack('<15I',size,*([0]*14))+bytes(size)

class TexturePayload(unittest.TestCase):
    def test_three_block_encodings_preserve_payload(self):
        for fmt,encoding,fourcc in ((0,'BC1',b'DXT1'),(1,'BC2',b'DXT3'),(2,'BC3',b'DXT5')):
            raw=fixture(fmt);meta=texture_metadata(raw);dds=private_dds(raw)
            self.assertEqual(meta['encoding'],encoding)
            self.assertEqual(dds[84:88],fourcc)
            self.assertEqual(dds[128:],raw[92:])
            self.assertEqual(struct.unpack_from('<I',dds,4)[0],124)
            self.assertFalse(meta['color_space_verified'])

    def test_independent_bc1_decoder(self):
        from PIL import Image
        # Solid RGB565 red block, every pixel selects endpoint zero.
        raw=fixture()[:92]+struct.pack('<HHI',0xf800,0,0)
        im=Image.open(io.BytesIO(private_dds(raw))).convert('RGB')
        self.assertEqual(im.size,(4,4))
        self.assertEqual(im.tobytes(),bytes((255,0,0))*16)

    def test_mip_chain_sizes_and_small_block_rounding(self):
        raw=struct.pack('<8I',3,0,0,0,4,4,0,3)+struct.pack('<15I',8,8,8,*([0]*12))+bytes(24)
        meta=texture_metadata(raw)
        self.assertEqual([(m['width'],m['size']) for m in meta['mips']],[(4,8),(2,8),(1,8)])
        self.assertEqual(struct.unpack_from('<I',private_dds(raw),28)[0],3)

    def test_reject_unsupported_and_malformed(self):
        raw=fixture()
        for offset,value in ((0,4),(4,1),(8,18),(16,0),(20,8193),(24,6),(28,4),(32,7),(36,8)):
            bad=bytearray(raw);struct.pack_into('<I',bad,offset,value)
            with self.subTest(offset=offset),self.assertRaises(ValueError):private_dds(bytes(bad))
        for bad in (raw[:-1],raw+b'\0',raw[:80]):
            with self.assertRaises(ValueError):private_dds(bad)

if __name__=='__main__':unittest.main()
