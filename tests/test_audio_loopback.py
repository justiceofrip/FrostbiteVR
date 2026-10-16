import math,struct,sys,unittest,uuid
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from bc2_audio_loopback import parse_format,pcm16

def fmt(tag=3,channels=2,rate=48000,bits=32):
    align=channels*bits//8
    return struct.pack('<HHIIHHH',tag,channels,rate,rate*align,align,bits,0)

class LoopbackFormatTests(unittest.TestCase):
    def test_native_float_and_pcm(self):
        value=parse_format(fmt());self.assertEqual(value['rate'],48000)
        raw=struct.pack('<6f',-2,-1,-.5,.5,1,2)
        self.assertEqual(struct.unpack('<6h',pcm16(raw,value)),(-32768,-32768,-16384,16384,32767,32767))
        pcm=parse_format(fmt(1,1,44100,16));self.assertEqual(pcm16(b'\x12\x34',pcm),b'\x12\x34')
    def test_extensible(self):
        header=bytearray(fmt());struct.pack_into('<H',header,0,0xfffe);struct.pack_into('<H',header,16,22)
        raw=bytes(header)+struct.pack('<HI',32,3)+uuid.UUID('00000003-0000-0010-8000-00aa00389b71').bytes_le
        self.assertEqual(parse_format(raw)['tag'],3)
        bad=bytearray(raw);struct.pack_into('<H',bad,18,24)
        with self.assertRaises(ValueError):parse_format(bad)
    def test_unsupported_and_over_budget_formats(self):
        for raw in [b'',fmt(channels=8),fmt(rate=96000),fmt(tag=1,bits=24),fmt(tag=4)]:
            with self.subTest(raw=raw),self.assertRaises(ValueError):parse_format(raw)
    def test_bad_packet_values(self):
        value=parse_format(fmt())
        for raw in [b'\0',struct.pack('<2f',math.nan,0),struct.pack('<2f',0,math.inf)]:
            with self.subTest(raw=raw),self.assertRaises(ValueError):pcm16(raw,value)

if __name__=='__main__':unittest.main()
