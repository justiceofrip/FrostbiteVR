import unittest
from bc2_shader_coordinates import trace_coordinates,evaluate_coordinates


def shader(stage,instructions):
    return '''// Input signature:
// Name Index Mask Register SysValue Format Used
// TEXCOORD 5 xyzw 3 NONE float xyzw
// Output signature:
// Name Index Mask Register SysValue Format Used
// TEXCOORD 5 xyzw 6 NONE float xyzw
'''+stage+'_5_0\n'+instructions+'\nret\n'


class Coordinates(unittest.TestCase):
    def test_vertex_masks_follow_component_lanes(self):
        v=trace_coordinates(shader('vs','mov o6.xy, v3.zwzz\nmov o6.zw, v3.xxxy'))
        values=v['outputs'][('TEXCOORD',5)]
        self.assertEqual([x['terms'] for x in values],[{'TEXCOORD5.z':1.},{'TEXCOORD5.w':1.},{'TEXCOORD5.x':1.},{'TEXCOORD5.y':1.}])

    def test_linked_pixel_sample_uses_semantic_not_register_number(self):
        v=trace_coordinates(shader('vs','mov o6.xyzw, v3.xyzw'))
        p=trace_coordinates(shader('ps','dcl_resource_texture2d (float,float,float,float) t2\nmul r0.xy, v3.zwzz, l(4.,8.,0.,0.)\nadd r0.xy, r0.xyxx, l(-.5,.25,0.,0.)\nsample_indexable(texture2d)(float,float,float,float) r1.xyz, r0.xyxx, t2.xyzw, s2'),v['outputs'])
        sample=p['samples'][0]
        self.assertTrue(sample['affine']);self.assertEqual(sample['texture_slot'],2)
        self.assertEqual(sample['coordinates'],[dict(constant=-.5,terms={'TEXCOORD5.z':4.}),dict(constant=.25,terms={'TEXCOORD5.w':8.})])

    def test_unknown_runtime_and_nonlinear_values_stay_unknown(self):
        for op in ('mul r0.xy, v3.xyxx, v3.zwzz','mad r0.xy, v3.xyxx, cb0[0].xyxx, v3.zwzz','rsq r0.xy, v3.xyxx','mov_sat r0.xy, v3.xyxx'):
            p=trace_coordinates(shader('ps','dcl_resource_texture2d (float,float,float,float) t2\n'+op+'\nsample_indexable(texture2d)(float,float,float,float) r1.xyz, r0.xyxx, t2.xyzw, s2'))
            self.assertFalse(p['samples'][0]['affine'])

    def test_multi_destination_invalidates_both_targets(self):
        v=trace_coordinates(shader('vs','mov r0.xyzw, v3.xyzw\nmov r1.xyzw, v3.xyzw\nimul r0.x, r1.y, v3.x, l(3)\nmov o6.xy, r0.xxxx\nmov o6.zw, r1.yyyy'))
        self.assertEqual(v['outputs'][('TEXCOORD',5)],[None]*4)

    def test_branch_and_early_return_reject(self):
        for op in ('if v3.x','if_nz r0.x\nmov o6.xyzw, v3.xyzw\nendif','retc_z v3.x','ret\nmov o6.xyzw, v3.xyzw'):
            with self.subTest(op=op),self.assertRaises(ValueError):trace_coordinates(shader('vs',op))

    def test_sample_results_cannot_become_affine_coordinates(self):
        p=trace_coordinates(shader('ps','dcl_resource_texture2d (float,float,float,float) t2\nsample_indexable(texture2d)(float,float,float,float) r0.xy, v3.xyxx, t2.xyzw, s2\nsample_indexable(texture2d)(float,float,float,float) r1.xyz, r0.xyxx, t2.xyzw, s2'))
        self.assertEqual([x['affine'] for x in p['samples']],[True,False])

    def test_evaluated_coordinates_preserve_repeat_and_offsets(self):
        channels={('TEXCOORD',5):dict(count=2,values=[(.25,.5,-1.,2.),(2.,3.,.5,-.5)])}
        coordinates=[dict(constant=-.5,terms={'TEXCOORD5.z':4.}),dict(constant=.25,terms={'TEXCOORD5.w':8.})]
        self.assertEqual(evaluate_coordinates(coordinates,channels,2),[(-4.5,16.25),(1.5,-3.75)])
        with self.assertRaises(ValueError):evaluate_coordinates(coordinates,{},2)
        with self.assertRaises(ValueError):evaluate_coordinates(coordinates,channels,3)
        with self.assertRaises(ValueError):evaluate_coordinates([None,coordinates[1]],channels,2)

    def test_arrays_and_cube_resources_are_not_flat_2d_samples(self):
        for kind in ('texture2darray','texturecube','texture2dms'):
            p=trace_coordinates(shader('ps','dcl_resource_'+kind+' (float,float,float,float) t2\nsample_indexable('+kind+')(float,float,float,float) r0.xy, v3.xyxx, t2.xyzw, s2'))
            self.assertEqual(p['samples'],[])


if __name__=='__main__':unittest.main()
