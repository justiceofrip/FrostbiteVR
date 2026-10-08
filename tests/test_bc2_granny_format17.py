"""Synthetic scalar-compressed vector curve tests; no installed assets needed."""
import struct,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from bc2_granny_curves import decode

def fixture():
    return {'CurveData':{'CurveDataHeader_D3I1K16uC16u':{'Format':17,'Degree':1},
        'OneOverKnotScaleTrunc':struct.unpack('<I',struct.pack('<f',65536.))[0]>>16,
        'ControlScales':[1/65535.,-2/65535.,0.],'ControlOffsets':[3.,4.,5.],
        'KnotsControls':[0,32768,65535,0,16384,65535]}}

class Format17Tests(unittest.TestCase):
    def test_one_scalar_drives_three_affine_axes(self):
        c=decode(fixture(),3)
        self.assertEqual(c.knots,(0.,.5,65535/65536.))
        self.assertEqual(c.controls[0],(3.,4.,5.))
        self.assertEqual(c.controls[-1],(4.,2.,5.))
        self.assertEqual(len(c.controls),3)
    def test_interpolation_retains_negative_and_constant_axes(self):
        value=decode(fixture(),3).evaluate(.25)
        self.assertAlmostEqual(value[0],3.+8192/65535.)
        self.assertAlmostEqual(value[1],4.-16384/65535.)
        self.assertEqual(value[2],5.)
    def test_wrong_dimension_or_format_rejected(self):
        with self.assertRaises(ValueError):decode(fixture(),4)
        bad=fixture();bad['CurveData']['CurveDataHeader_D3I1K16uC16u']['Format']=18
        with self.assertRaises(ValueError):decode(bad,3)
if __name__=='__main__':unittest.main()
