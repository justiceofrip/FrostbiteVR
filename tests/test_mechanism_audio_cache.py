import importlib.util
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from bc2_mechanism_audio_cache import packets,cue

def sample():
    stream=b'H\0\0\x0c'+struct.pack('>II',0x1400bb80,0x40000080)
    stream+=b'D\0\0\x54'+struct.pack('>I',128)+bytes(76)+b'E\0\0\x04'
    return b'\0\0\x03\0'+struct.pack('<I',128)+bytes(4)+struct.pack('<I',1)+bytes(12)+struct.pack('<I',len(stream))+stream+bytes(8)

class CacheTests(unittest.TestCase):
    def test_packet_boundary_and_counts(self):
        self.assertEqual(packets(sample()),(48000,128,bytes(76)))
    def test_reject_wrong_codec_channel_rate(self):
        for offset,value in ((36,0x15),(37,0x04),(39,0)):
            blob=bytearray(sample());blob[offset]=value
            with self.assertRaises(ValueError):packets(blob)
    def test_reject_truncated_or_overstated_data(self):
        for blob in (sample()[:70],sample()[:-9],sample()+b'x',bytes(48)):
            with self.assertRaises(ValueError):packets(blob)
    def test_reject_frame_alignment_and_count(self):
        for offset,value in ((47,83),(51,127),(4,129)):
            blob=bytearray(sample());blob[offset]=value
            with self.assertRaises(ValueError):packets(blob)
    def test_private_cue_has_bounded_gain_and_edge_fade(self):
        pcm=struct.pack('<'+'h'*1000,*([20000]*1000));encoded=cue(pcm,48000,10,990)
        self.assertEqual(encoded[:4],b'RIFF');values=struct.unpack('<'+'h'*980,encoded[44:])
        self.assertEqual((values[0],values[-1]),(0,0));self.assertEqual(max(values),6000)
        for first,last,gain in ((-1,20,.3),(0,1001,.3),(5,5,.3),(0,20,.6)):
            with self.assertRaises(ValueError):cue(pcm,48000,first,last,gain)
if __name__=='__main__':unittest.main()
