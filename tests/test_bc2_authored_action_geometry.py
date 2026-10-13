from pathlib import Path
import copy,sys,unittest
from types import SimpleNamespace
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import bc2_authored_action_geometry as action
import bc2_authored_magazine_geometry as mag
from bc2_authored_grip_bindings import digest,named
from test_bc2_authored_grip_bindings import fixture,first,document,node,T,S,C,O


class ActionGraph(unittest.TestCase):
    def setUp(self):
        f=fixture();self.profile=first(f);self.docs={d.resource.lower():d for d in f[1]}
        self.profile['skeleton_source']={'archive':'Dist/common.fbrb','archive_index_sha256':'9'*64}
        self.archive=SimpleNamespace(index_sha256='9'*64,entries=[])
        self.add('Animations/Unrelated/1P_BoltAction',O)

    def add(self,path,identifier,kind='Animation.AnimationAsset',name=None):
        d=document(path+'.dbx',identifier,kind,[node(name='Name',text=name or path),node(name='Skeleton',ref='characters/actualskeleton/'+S)],'e')
        self.docs[d.resource.lower()]=d
        old=self.docs[self.profile['animation_chain']['tree']['resource'].lower()]
        items=list(named(old.instances[T],'ReplacmentAnimations').children)+[node('item',ref=path.lower()+'/'+identifier)]
        self.docs[old.resource.lower()]=document(old.resource,T,'Animation.SpecificAnimTreeData',[node('array','ReplacmentAnimations',children=items)],'b')

    def resolve(self,roles=('1p_boltaction',)):
        self.archive.entries=[SimpleNamespace(name=d.resource,flags=65536,kind='<non-resource>') for d in self.docs.values()]
        self.reads=[]
        def read(names):
            self.reads.append(list(names));return {name:name.lower().encode() for name in names}
        self.archive.read_selected=read
        with patch.object(mag,'parse',side_effect=lambda path,data:self.docs[data.decode()]):
            result=mag.bound_action_assets(self.profile,self.archive,roles)
        self.assertLessEqual(len(self.reads),2)
        return result

    def test_exact_named_role_keeps_document_case_and_skeleton_proof(self):
        row=self.resolve()['1p_boltaction']
        self.assertEqual(row['resource'],'Animations/Unrelated/1P_BoltAction.res')
        self.assertEqual(row['asset_document'],'Animations/Unrelated/1P_BoltAction.dbx')
        self.assertEqual(row['asset_guid'],O);self.assertEqual(row['skeleton_reference'],'characters/actualskeleton/'+S)
        self.assertNotIn('native_admission',row)

    def test_missing_role_is_explicitly_absent(self):
        self.assertEqual(self.resolve(('1p_reload',)),{})

    def test_duplicate_role_and_delta_asset_reject(self):
        self.add('Other/1P_BoltAction',O[:-1]+'7')
        with self.assertRaisesRegex(ValueError,'Ambiguous'):self.resolve()
        self.setUp();self.docs['animations/unrelated/1p_boltaction.dbx'].instances[O].attributes['type']='Animation.DeltaAnimationAsset'
        with self.assertRaisesRegex(ValueError,'absolute'):self.resolve()

    def test_wrong_guid_tree_type_skeleton_and_source_reject(self):
        for mutate in (
            lambda:setattr(self.archive,'index_sha256','8'*64),
            lambda:self.profile['animation_chain']['tree'].update(sha256='a'*64),
            lambda:self.profile['animation_chain']['tree'].update(instance_guid=O),
            lambda:self.docs[self.profile['animation_chain']['tree']['resource'].lower()].instances[T].attributes.update(type='Animation.Other'),
            lambda:named(self.docs['animations/unrelated/1p_boltaction.dbx'].instances[O],'Skeleton').attributes.update(ref='characters/actualskeleton/'+C),
            lambda:self.profile['animation_chain'].update(skeleton_resource='Other.res'),
        ):
            self.setUp();mutate()
            with self.assertRaises((ValueError,KeyError)):self.resolve()

    def test_local_guid_reference_and_two_assets_in_one_document(self):
        key=self.profile['animation_chain']['tree']['resource'].lower();tree=self.docs[key]
        bolt=self.docs['animations/unrelated/1p_boltaction.dbx'].instances[O]
        tree.instances[O]=bolt
        named(tree.instances[T],'ReplacmentAnimations').children[-1].attributes['ref']=O
        row=self.resolve()['1p_boltaction'];self.assertEqual(row['asset_document'],tree.resource)
        self.assertEqual(row['asset_guid'],O)

    def test_duplicate_array_reference_and_bad_roles_reject(self):
        for roles in ((),['1P_BoltAction'],['a','a'],['x']*33,['../escape'],[False]):
            with self.assertRaises(ValueError):self.resolve(roles)
        tree=self.docs[self.profile['animation_chain']['tree']['resource'].lower()]
        items=named(tree.instances[T],'ReplacmentAnimations').children
        items[-1].attributes['ref']=items[0].attributes['ref']
        with self.assertRaisesRegex(ValueError,'Duplicate'):self.resolve()


class ActionBatch(unittest.TestCase):
    def profile(self,asset='A'):
        return {'native_asset_name':asset,'weapon':{'resource':asset+'.dbx','instance_guid':O},
                'state_index':0,'configured_mesh_path':'mesh','binding_digest':asset,
                'animation_chain':{'tree':{'resource':asset+'.dbx'}},
                'skeleton_source':{'archive':'common.fbrb','archive_index_sha256':'a'*64}}

    def test_numeric_cache_keeps_each_configuration_and_action_identity(self):
        ctx={'skeleton':SimpleNamespace(sha256='s'),'reference':SimpleNamespace(sha256='r'),
             'receipt':{'mesh_sha256':'m','lod_sha256':'l'}}
        raw=b'exact';key=('m','l',mag.sha(raw),'r','s')
        cached={'parts':[{'bone':'arbitrary'}],'linear_candidates':[], 'runtime_admitted':False,'digest':'old'}
        cache={key:cached}
        first=action.inspect_action(self.profile(),ctx,{'role':'1p_reload'},raw,{'archive':'first'},cache)
        other=action.inspect_action(self.profile('B'),ctx,{'role':'1p_boltaction'},raw,{'archive':'second'},cache)
        self.assertEqual(first['asset'],'A');self.assertEqual(other['asset'],'B')
        self.assertEqual(first['authored_role'],'1p_reload');self.assertEqual(other['authored_role'],'1p_boltaction')
        self.assertEqual(other['source']['archive'],'second');self.assertEqual(len(cache),1)
        self.assertNotEqual(first['digest'],other['digest']);self.assertEqual(cached['digest'],'old')
        self.assertFalse(other['live_state_selection_verified']);self.assertFalse(other['runtime_admitted'])

    def test_partial_batch_retains_success_missing_roles_and_failure(self):
        profiles=[self.profile(),self.profile('B')]
        bindings={'schema':'fvr.bc2.authored_grip_bindings','schema_version':1,'profiles':profiles}
        meshes={'schema':'fvr.bc2.authored_weapon_mesh_bindings','schema_version':1}
        with patch.object(action,'ArchivePool'),patch.object(action,'context',side_effect=[{'graph_archive':'g','sources':[]},ValueError('different rig')]),\
             patch.object(mag,'bound_action_assets',return_value={'1p_reload':{'resource':'a','role':'1p_reload'}}),\
             patch.object(mag,'selected_animation_resource',return_value=(b'raw',{})),\
             patch.object(action,'inspect_action',return_value={'asset':'A'}):
            result=action.run(Path('.'),bindings,meshes,('1p_reload','1p_boltaction'))
        self.assertEqual(result['records'],[{'asset':'A'}]);self.assertEqual(len(result['missing_roles']),1)
        self.assertEqual(result['gaps'][0]['asset'],'B');self.assertFalse(result['runtime_admission'])
        with self.assertRaisesRegex(ValueError,'Duplicate'):action.run(Path('.'),{**bindings,'profiles':[profiles[0]]*2},meshes,('1p_reload',))
        with self.assertRaisesRegex(ValueError,'absent'):action.run(Path('.'),bindings,meshes,('1p_reload',),{'unknown'})

    def test_stale_profile_rejected_before_archive_access(self):
        p=self.profile();p['binding_digest']='0'*64
        with self.assertRaisesRegex(ValueError,'digest'):action.context(Path('.'),p,{},None)


if __name__=='__main__':unittest.main()
