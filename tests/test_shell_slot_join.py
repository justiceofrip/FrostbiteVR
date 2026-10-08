import copy, importlib.util, pathlib, struct, unittest
path=pathlib.Path(__file__).resolve().parents[1]/'tools/audit_shell_slot_join.py'
spec=importlib.util.spec_from_file_location('audit_shell_slot_join',path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
RAW=struct.pack('<12f',1,0,0,.1,0,1,0,.2,0,0,1,.3).hex()
def draw(eye,section,seed=False,frame=9):
    count,vh,ph=module.GEOMETRY[section]
    return {'world':1,'request':2,'frame':frame,'eye':eye,'view':10+eye,'count':count,'geometry_section':section,
        'vertex_skin_fnv1a64':vh,'position_fnv1a64':ph,'complete':True,'geometry_valid':True,'qpc_ns':200,
        'shell_slot_observation':{'captured':True,'constant_buffer_slot':0,'byte_offset':272,'byte_count':48,'bytes_hex':RAW},
        'buffers':[{}, {}, {'complete':True,'copied_bytes':4096,'packed_shell_matches':int(seed),'packed_shell_offsets':[272] if seed else []}],
        'producer':{'actor':3,'weak':4,'weapon':5,'owner_generation':6,'space':7,'rig_pose':8,'rig_fingerprint':module.RIG,'input_generation':1,
            'exact_request_association':seed,'packed_shell_valid':seed,'request':2,'native_frame':frame,'observed_ns':100,'deadline_ns':300}}
def fixture():return {'reload_draw_evidence':{'draws':[draw(e,s,e==1) for e in (0,1) for s in (1,2)]}}
class JoinTests(unittest.TestCase):
    def test_exact_same_frame_both_sections(self):
        out=module.audit(fixture());self.assertEqual(out['complete_stereo_shell_frames'],1);self.assertEqual(out['matched_left_draws'],2);self.assertFalse(out['render_authority'])
    def test_changed_left_bytes(self):
        f=fixture();f['reload_draw_evidence']['draws'][0]['shell_slot_observation']['bytes_hex']=struct.pack('<12f',*([2]*12)).hex()
        out=module.audit(f);self.assertEqual(out['complete_stereo_shell_frames'],0);self.assertEqual(out['slot_mismatch_draws'],1)
    def test_other_frame_not_nearest_match(self):
        f=fixture();f['reload_draw_evidence']['draws'][0]['frame']=8;out=module.audit(f);self.assertEqual(out['complete_stereo_shell_frames'],0);self.assertEqual(out['frames_without_source'],1)
    def test_owner_or_source_ambiguity(self):
        for mutate in (lambda d:d['producer'].update(actor=999),lambda d:d['shell_slot_observation'].update(bytes_hex=struct.pack('<12f',*([2]*12)).hex())):
            f=fixture();mutate(f['reload_draw_evidence']['draws'][3]);self.assertEqual(module.audit(f)['conflicting_frames'],1)
    def test_missing_legacy_or_wrong_geometry(self):
        for key in ('shell_slot_observation','geometry_valid'):
            f=fixture()
            for d in f['reload_draw_evidence']['draws']:d.pop(key)
            self.assertEqual(module.audit(f)['eligible_shell_draws'],0)
        f=fixture();f['reload_draw_evidence']['draws'][0]['vertex_skin_fnv1a64']='wrong';self.assertEqual(module.audit(f)['eligible_shell_draws'],3)
    def test_expired_hidden_wrong_match_never_seed(self):
        for mutate in (lambda d:d['producer'].update(deadline_ns=200),lambda d:d['producer'].update(shell_hidden=True),lambda d:d['buffers'][2].update(packed_shell_offsets=[320]),lambda d:d['buffers'][2].update(packed_shell_matches=2)):
            f=fixture()
            for d in f['reload_draw_evidence']['draws'][2:]:mutate(d)
            out=module.audit(f);self.assertEqual(out['source_seeds'],0);self.assertEqual(out['matched_left_draws'],0)
    def test_wrong_slot_or_nonfinite_rejected(self):
        for mutate in (lambda d:d['shell_slot_observation'].update(byte_offset=0),lambda d:d['shell_slot_observation'].update(bytes_hex='00'),lambda d:d['shell_slot_observation'].update(bytes_hex=struct.pack('<12f',*([float('nan')]*12)).hex())):
            d=draw(1,1,True);mutate(d);self.assertIsNone(module.read_draw(d))
    def test_cross_world_or_request_rejected(self):
        for key in ('world','request'):
            f=fixture();f['reload_draw_evidence']['draws'][0][key]=999;out=module.audit(f);self.assertEqual(out['frames_without_source'],1);self.assertEqual(out['complete_stereo_shell_frames'],0)
if __name__=='__main__':unittest.main()
