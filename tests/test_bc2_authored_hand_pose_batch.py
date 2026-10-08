import copy
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import bc2_authored_hand_pose_batch as p

H='a'*64
NAME='Animations/Exact/HandsIkPose.res'
def entry(name,kind):return SimpleNamespace(name=name,kind=kind,flags=65536)

class Skeleton:
    names=['RightHand','LeftHand','jntWpn_1']
    def __init__(self,data):pass
    def metadata(self):return {'resource_sha256':p.digest(b'skeleton'),'rig_fingerprint':'fnv1a64:0000000000000001','bones':[]}

class Clip:
    tracks={'RightHand':None,'LeftHand':None,'jntWpn_1':None}
    def __init__(self,data):
        self.data=data
        if data==b'unsupported':raise ValueError('unsupported encoding')
    def summary(self):return {'duration_seconds':1.}
    def evaluate(self,skeleton,time,names):
        if 'jntWpn_1' in names and self.data==b'partial':raise ValueError('optional part unsupported')
        return {'evaluation_status':'varying_controls_not_static' if self.data==b'varying' else 'static_authored_pose'}

class BatchTests(unittest.TestCase):
    def setUp(self):
        self.root=Path(__file__).resolve().parent
        self.inventory={'schema':'fvr.bc2.installed_weapon_inventory','schema_version':1,
            'archives':[{'archive':n,'index_sha256':H,'animations':[{'name':NAME,'kind':'GrannyAnimation'}]} for n in ('one','two')]}
        self.data={'skeleton':b'skeleton','one':b'constant','two':b'constant'}
        self.indices={};self.omit=set()
    def archive(self,path):
        name=path.name
        entries=[entry('Characters/Exact.res','GrannyModel')] if name=='skeleton' else [entry(NAME,'GrannyAnimation')]
        if name in self.omit:entries=[]
        return SimpleNamespace(index_sha256=self.indices.get(name,H),entries=entries,
            read_selected=lambda names:{n:self.data[name] for n in names})
    def run_batch(self):
        return p.run(self.root,self.inventory,'skeleton','Characters/Exact.res',archive_type=self.archive,clip_type=Clip,skeleton_type=Skeleton)
    def test_content_versions_remain_distinct_and_identical_sources_merge(self):
        out=self.run_batch();self.assertEqual(out['resource_occurrences'],2);self.assertEqual(len(out['clips']),1)
        self.assertEqual(len(out['clips'][0]['sources']),2)
        self.data['two']=b'varying';out=self.run_batch()
        self.assertEqual(len(out['clips']),2);self.assertEqual(out['static_hand_pose_resources'],1)
        self.assertFalse(out['runtime_admission']);self.assertFalse(out['active_native_mesh_binding'])
    def test_changed_index_and_missing_resources_cannot_look_complete(self):
        self.indices['two']='b'*64
        out=self.run_batch();self.assertEqual(len(out['gaps']),1);self.assertEqual(out['resource_occurrences'],1)
        self.indices.clear();self.omit.add('two');out=self.run_batch();self.assertEqual(len(out['gaps']),1)
    def test_optional_parts_do_not_erase_valid_hand_result(self):
        self.data['one']=b'partial';self.data['two']=b'unsupported';out=self.run_batch()
        self.assertEqual(out['statuses'],{'unsupported':1,'authored_hands_decoded':1})
        row=next(r for r in out['clips'] if r['status']=='authored_hands_decoded')
        self.assertIn('parts_error',row);self.assertEqual(out['static_hand_pose_resources'],1)
    def test_duplicate_archives_and_bad_paths_are_rejected(self):
        self.inventory['archives'].append(copy.deepcopy(self.inventory['archives'][0]))
        with self.assertRaises(ValueError):self.run_batch()
        for name in ('../escape','/absolute','G:/game','a\\b','a//b'):
            with self.assertRaises(ValueError):p.resource(name)
    def test_skeleton_role_must_be_exact(self):
        self.omit.add('skeleton')
        with self.assertRaises(ValueError):self.run_batch()
    def test_input_inventory_is_not_mutated(self):
        before=copy.deepcopy(self.inventory);self.run_batch();self.assertEqual(before,self.inventory)

if __name__=='__main__':unittest.main()
