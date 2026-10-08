from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import copy
import math
import struct
from types import SimpleNamespace
import unittest
from unittest.mock import patch
from bc2_granny_resource import Resource,MAGIC
from bc2_granny_curves import decode,Curve
from bc2_weapon_animation_pipeline import Clip,canonical,identity,matrix,multiply,inverse_rigid,rig_fingerprint,archive_identity

def resource(kind=19,depth=1,cycle=False):
    strings=['<file>','Value'];wrapper=struct.pack('<3I',10,0,len(strings))+b''.join(struct.pack('<H',len(v))+v.encode() for v in strings)
    header_size=192;data=struct.pack('<I',42 if kind==19 else 0)
    definitions=b'';fixups=[]
    for n in range(depth):
        field_kind=1 if n+1<depth else kind
        definitions+=struct.pack('<8I',field_kind,1,0,0,0,0,0,0)+bytes(32)
        if n+1<depth:fixups.append((n*64+8,1,(n+1)*64))
    if cycle:fixups.append(((depth-1)*64+8,1,0))
    def_start=header_size+len(data);fix_start=def_start+len(definitions)
    objfix=struct.pack('<3I',0,0,0) if cycle else b'';typefix=b''.join(struct.pack('<3I',*f) for f in fixups)
    total=fix_start+len(objfix)+len(typefix)
    core=MAGIC+struct.pack('<4I',header_size,0,0,0)+struct.pack('<18I',7,total,0,72,2,1,0,0,0,0x80000028,*([0]*8))
    core+=struct.pack('<11I',0,header_size,len(data),len(data),4,len(data),len(data),fix_start,1 if cycle else 0,total,0)
    core+=struct.pack('<11I',0,def_start,len(definitions),len(definitions),4,len(definitions),len(definitions),fix_start+len(objfix),len(fixups),total,0)
    core+=data+definitions+objfix+typefix
    return wrapper+struct.pack('<I',len(core))+core

def const(form,values):
    codes={'DaIdentity':2,'D3Constant32f':4,'D4Constant32f':5}
    d={'CurveDataHeader_'+form:{'Format':codes[form],'Degree':0}}
    d.update(Dimension=values) if form=='DaIdentity' else d.update(Controls=list(values))
    return {'CurveData':d}

def clip_document():
    tracks=[]
    for name in ('root','weapon','hand'):
        tracks.append({'Name':name,'Flags':0,'PositionCurve':const('D3Constant32f',(1.,2.,3.)),
                       'OrientationCurve':const('D4Constant32f',(0.,0.,0.,1.)),
                       'ScaleShearCurve':const('DaIdentity',9)})
    group={'Name':'root','TransformTracks':tracks}
    return {'ArtToolInfo':None,'Animations':[{'Duration':1.,'TimeStep':.1,'Oversampling':1.,'TrackGroups':[group]}]}

def make_clip(doc=None):
    with patch('bc2_weapon_animation_pipeline.Resource') as reader:
        reader.return_value.read_root_fields.return_value=doc or clip_document()
        return Clip(b'synthetic')

class ContainerTests(unittest.TestCase):
    def test_typed_root_reads(self):self.assertEqual(Resource(resource()).read()['Value'],42)
    def test_wrong_wrapper_and_magic(self):
        for pos in (0,31):
            b=bytearray(resource());b[pos]^=255
            with self.assertRaises(ValueError):Resource(b)
    def test_truncated_resource(self):
        b=resource()
        for end in (0,15,24,len(b)-1):
            with self.assertRaises(ValueError):Resource(b[:end])
    def test_unsupported_compression(self):
        b=bytearray(resource());core=b.index(MAGIC);struct.pack_into('<I',b,core+104,1)
        with self.assertRaisesRegex(ValueError,'Compressed'):Resource(b)
    def test_bad_section_reference(self):
        b=bytearray(resource());core=b.index(MAGIC);struct.pack_into('<I',b,core+104+4,len(b))
        with self.assertRaisesRegex(ValueError,'Section bounds'):Resource(b)
    def test_deep_inline_type(self):
        with self.assertRaisesRegex(ValueError,'nesting limit'):Resource(resource(depth=60))
    def test_recursive_object(self):
        with self.assertRaisesRegex(ValueError,'nesting limit'):Resource(resource(kind=2,cycle=True)).read()
    def test_value_budget(self):
        r=Resource(resource());r.values=1000000
        with self.assertRaisesRegex(ValueError,'budget'):r.read()
    def test_alias_scalar_amplification_is_bounded(self):
        # A malicious graph can repeatedly reference the same scalar storage.
        # The global value budget counts each decoded expansion, not just bytes.
        r=Resource(resource());field=r.fields(r.root_type)[0]
        r.values=1000000-4096
        for _ in range(4096):self.assertEqual(r.value(field,r.root_ref,0),42)
        with self.assertRaisesRegex(ValueError,'budget'):r.value(field,r.root_ref,0)
    def test_archive_identity_is_exact_root_relative(self):
        base=Path(__file__).resolve().parent
        row=archive_identity(base/'Dist/win32/a.fbrb',base)
        self.assertEqual(row['archive_relative'],'Dist/win32/a.fbrb')
        self.assertNotIn('archive_relative',archive_identity(base/'a.fbrb'))
        with self.assertRaises(ValueError):archive_identity(base.parent/'a.fbrb',base)
    def test_model_wrapper_explicit(self):
        b=resource();model=b[:4]+b[8:]
        self.assertEqual(Resource(model,'GrannyModel').read()['Value'],42)
        with self.assertRaises(ValueError):Resource(model)

class CurveTests(unittest.TestCase):
    def test_constants_and_identity(self):
        self.assertEqual(decode(const('D3Constant32f',(1.,2.,3.)),3).evaluate(.5),(1.,2.,3.))
        self.assertEqual(decode(const('DaIdentity',4),4).evaluate(2),(0.,0.,0.,1.))
    def test_wrong_dimension_or_format(self):
        with self.assertRaises(ValueError):decode(const('DaIdentity',3),4)
        c=const('D4Constant32f',(0,0,0,1));c['CurveData']['CurveDataHeader_D4Constant32f']['Format']=99
        with self.assertRaises(ValueError):decode(c,4)
    def test_packed_quaternion_sign_swizzle(self):
        for bits,form,code in ((16,'D4nK16uC15u',8),(8,'D4nK8uC7u',9)):
            word=1<<(bits-1)
            # Quantization ranges with offset zero; omitted W, stored XYZ zero.
            for sign in (0,word):
                c={'CurveData':{'CurveDataHeader_'+form:{'Format':code,'Degree':1},'ScaleOffsetTableEntries':0x7777,
                    'OneOverKnotScale':1.,'KnotsControls':[0,sign,word,word]}}
                q=decode(c,4).evaluate(0)
                self.assertEqual(q,(0.,0.,0.,-1. if sign else 1.))
    def test_vector_quantization(self):
        c={'CurveData':{'CurveDataHeader_D3K8uC8u':{'Format':11,'Degree':1},'OneOverKnotScaleTrunc':0x3f80,
                       'ControlScales':[.5,1.,2.],'ControlOffsets':[1.,2.,3.],'KnotsControls':[0,1,2,4]}}
        self.assertEqual(decode(c,3).evaluate(0),(1.5,4.,11.))
    def test_shared_vector_quantization(self):
        c={'CurveData':{'CurveDataHeader_D3I1K8uC8u':{'Format':18,'Degree':1},'OneOverKnotScaleTrunc':0x3f80,
                       'ControlScales':[.5,1.,2.],'ControlOffsets':[1.,2.,3.],'KnotsControls':[0,4]}}
        self.assertEqual(decode(c,3).evaluate(0),(3.,6.,11.))
    def test_linear_spline_known_value(self):
        c=Curve('synthetic',1,3,(0.,1.),((0.,0.,0.),(2.,4.,6.)))
        self.assertEqual(c.evaluate(.25),(.5,1.,1.5))
    def test_repeated_knots_and_constant_curve(self):
        c=Curve('synthetic',2,3,(0.,0.,1.),((2.,3.,4.),)*3)
        for actual,expected in zip(c.evaluate(.3),(2.,3.,4.)):self.assertAlmostEqual(actual,expected)
    def test_nonfinite_rejected(self):
        for value in (float('inf'),float('nan')):
            with self.assertRaises(ValueError):decode(const('D3Constant32f',(value,0.,0.)),3)

class TransformTests(unittest.TestCase):
    def test_row_composition_inverse_and_reflection(self):
        a=matrix((1.,2.,3.),(0.,math.sqrt(.5),0.,math.sqrt(.5)),(1.,0.,0.,0.,1.,0.,0.,0.,1.))
        for x,y in zip(multiply(a,inverse_rigid(a)),identity()):self.assertAlmostEqual(x,y)
        self.assertEqual(canonical(canonical(a)),a)
        self.assertEqual(canonical(a)[12:15],[1.,2.,-3.])
    def test_finite_composition_overflow_and_nan_inverse(self):
        a=identity();a[0]=1e308
        with self.assertRaises(ValueError):multiply(a,a)
        with self.assertRaises(ValueError):inverse_rigid([float('nan')]*16)
    def test_fingerprint_distinguishes_order_bind_and_names(self):
        base=rig_fingerprint(['root'],[-1],[identity()])
        self.assertNotEqual(base,rig_fingerprint(['other'],[-1],[identity()]))
        a=identity();a[12]=1
        self.assertNotEqual(base,rig_fingerprint(['root'],[-1],[a]))
    def test_incompatible_clip_basis(self):
        d=clip_document();d['ArtToolInfo']={'UnitsPerMeter':100.,'Origin':[0.,0.,0.],'RightVector':[1.,0.,0.],'UpVector':[0.,1.,0.],'BackVector':[0.,0.,1.]}
        with self.assertRaisesRegex(ValueError,'clip basis'):make_clip(d)
    def test_static_and_dynamic_are_explicit(self):
        clip=make_clip();s=SimpleNamespace(names=['root','weapon','hand'],parents=[-1,0,0])
        result=clip.evaluate(s,.5,['hand'],'weapon');self.assertEqual(result['evaluation_status'],'static_authored_pose')
        self.assertFalse(result['target_runtime_interpolation_verified'])
        old=clip.tracks['hand'];clip.tracks['hand']=(Curve('synthetic',1,3,(0.,1.),((0.,0.,0.),(1.,0.,0.))),old[1],old[2])
        self.assertEqual(clip.evaluate(s,.5,['hand'],'weapon')['bone_evaluation_status']['hand'],'decoded_spline_candidate')
    def test_missing_ancestor_rejects(self):
        clip=make_clip();del clip.tracks['root'];s=SimpleNamespace(names=['root','weapon','hand'],parents=[-1,0,0])
        with self.assertRaisesRegex(ValueError,'Required authored'):clip.evaluate(s,.5,['hand'],'weapon')
    def test_no_active_asset_binding_from_archive_names(self):
        summary=make_clip().summary()
        self.assertIn('not active native mesh proof',summary['clip_skeleton_pairing'])
        self.assertIn('not_stored_in_clip',summary['clip_basis_provenance'])

if __name__=='__main__':unittest.main()
