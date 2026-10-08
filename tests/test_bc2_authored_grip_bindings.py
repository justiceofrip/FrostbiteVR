from pathlib import Path
import copy,math,sys,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import bc2_authored_grip_bindings as p
from bc2_weapon_config_pipeline import Document,Node,Value

W='00000000-0000-0000-0000-000000000001';T=W[:-1]+'2';C=W[:-1]+'3';S=W[:-1]+'4';M=W[:-1]+'5';O=W[:-1]+'6'
def node(tag='field',name=None,text='',children=(),**attrs):
    if name is not None:attrs['name']=name
    return Node(tag,attrs,Value(2,0,1,text),tuple(children),0)
def document(path,identifier,kind,children,hashchar):
    i=node('instance',guid=identifier,type=kind,children=children)
    return Document(path,hashchar*64,node('partition',children=(i,)),{identifier:i})
def fixture():
    wp='Objects/Rifle';tree='Trees/UnrelatedDirectory';clip='Animations/Different/HandsIkPose';skeleton='Characters/ActualSkeleton';mesh='Render/ExactMesh'
    ref=lambda path,g:path.lower()+'/'+g
    state=node('complex',children=[node('field','AnimTree1p',ref=ref(tree,T)),node('array','Meshes1p',children=[node('item',ref=ref(mesh,M))])])
    docs=[document(wp+'.dbx',W,'GameSharedResources.SoldierWeaponData',[node(name='Name',text='Rifle_sp'),node(name='WeaponClass',text='wcAssault'),node('array','WeaponStates',children=[state])],'a'),
          document(tree+'.dbx',T,'Animation.SpecificAnimTreeData',[node('array','ReplacmentAnimations',children=[node('item',ref=ref(clip,C))])],'b'),
          document(clip+'.dbx',C,'Animation.AnimationAsset',[node(name='Name',text=clip),node(name='Skeleton',ref=ref(skeleton,S))],'c'),
          document(skeleton+'.dbx',S,'Animation.SkeletonAsset',[node(name='Name',text=skeleton)],'d'),
          document(mesh+'.dbx',M,'Render.SkinnedMeshAsset',[node(name='Name',text=mesh)],'e')]
    weapon={'native_name':'Rifle_sp','weapon_class':'wcAssault','resource':wp+'.dbx','resource_sha256':'a'*64,'instance_guid':W}
    config={'schema':'fvr.bc2.authored_weapon_configuration','schema_version':1,'resolved_weapons':[weapon]}
    item={'reference':ref(mesh,M),'status':'authored_mesh_resolved','mesh_document':mesh+'.dbx','mesh_document_sha256':'e'*64,
          'mesh_instance_guid':M,'mesh_resource':mesh+'.res','geometry_variants':[{'mesh_sha256':'f'*64,'lods':[]}]}
    meshes={'schema':'fvr.bc2.authored_weapon_mesh_bindings','schema_version':1,'weapons':[{**weapon,'states':[{'index':0,'meshes':[item]}]}]}
    identity=[float(n//4==n%4) for n in range(16)];left=list(identity);left[12]=.2
    hands={'evaluation_status':'static_authored_pose','all_required_controls_constant':True,
           'coordinate_convention':'canonical reflected-Z row-vector metres; named bone relative to jntWpn_1',
           'bone_evaluation_status':{'RightHand':'static_authored_pose','LeftHand':'static_authored_pose'},
           'weapon_relative':{'RightHand':identity,'LeftHand':left}}
    meta={'rig_fingerprint':'fnv1a64:0123456789abcdef','bones':[{'name':'RightHand','parent_index':-1},{'name':'LeftHand','parent_index':-1}]}
    batch={'schema':'fvr.bc2.authored_hand_pose_batch.v1','skeleton':{'resource':skeleton+'.res','resource_sha256':'1'*64,**meta},
           'clips':[{'resource':clip+'.res','sha256':'2'*64,'status':'authored_hands_decoded','hands':copy.deepcopy(hands),
                     'sources':[{'archive':'Dist/explicit.fbrb','index_sha256':'3'*64}]}]}
    resources={skeleton.lower()+'.res':{'kind':'GrannyModel','sha256':'1'*64,'metadata':meta,'source':{'archive':'Dist/config.fbrb'}},
               clip.lower()+'.res':{'kind':'GrannyAnimation','sha256':'2'*64,'skeleton_sha256':'1'*64,'hands':hands,'source':{'archive':'Dist/explicit.fbrb'}}}
    return config,docs,meshes,batch,resources

def result(args):return p.build(*args)
def first(args):return result(args)['profiles'][0]
class GripBindings(unittest.TestCase):
    def reject(self,args,why=None):
        out=result(args);self.assertFalse(out['profiles']);self.assertTrue(out['gaps'])
        if why:self.assertIn(why,out['gaps'][0]['reason'])
    def test_exact_chain_produces_consumable_matrices(self):
        row=first(fixture());self.assertEqual(row['left_hand_in_weapon'][12],.2)
        self.assertEqual(row['configured_mesh_path'],'Render/ExactMesh');self.assertFalse(row['runtime_accepted'])
        self.assertFalse(row['active_native_mesh_binding']);self.assertEqual(row['animation_chain']['skeleton_asset']['instance_guid'],S)
        self.assertEqual(row['binding_digest'],p.digest({k:v for k,v in row.items() if k!='binding_digest'}))
    def test_replacement_order_is_not_hardcoded(self):
        a=list(fixture());old=a[1][1];items=list(p.named(old.instances[T],'ReplacmentAnimations').children)
        other=document('Animations/Other.dbx',O,'Animation.AnimationAsset',[node(name='Name',text='Animations/Other')],'4')
        a[1].append(other)
        a[1][1]=document(old.resource,T,'Animation.SpecificAnimTreeData',[node('array','ReplacmentAnimations',children=[node('item',ref='animations/other/'+O)]+items)],'b')
        self.assertEqual(first(a)['native_asset_name'],'Rifle_sp')
    def test_other_nonabsolute_animation_does_not_change_pose_role(self):
        a=list(fixture());old=a[1][1];items=list(p.named(old.instances[T],'ReplacmentAnimations').children)
        a[1].append(document('Animations/Recoil.dbx',O,'Animation.DeltaAnimationAsset',[node(name='Name',text='Animations/Recoil')],'4'))
        a[1][1]=document(old.resource,T,'Animation.SpecificAnimTreeData',[node('array','ReplacmentAnimations',children=items+[node('item',ref='animations/recoil/'+O)])],'b')
        self.assertEqual(len(result(a)['profiles']),1)
    def test_wrong_weapon_hash_name_or_guid(self):
        for field,value in (('resource_sha256','0'*64),('native_name','Other'),('instance_guid',O)):
            a=fixture();a[0]['resolved_weapons'][0][field]=value;self.reject(a)
    def test_wrong_tree_type(self):
        a=fixture();a[1][1].instances[T].attributes['type']='Animation.GeneralTree';self.reject(a,'type mismatch')
    def test_wrong_animation_guid(self):
        a=fixture();p.named(a[1][1].instances[T],'ReplacmentAnimations').children[0].attributes['ref']='animations/different/handsikpose/'+O;self.reject(a,'GUID')
    def test_delta_hands_are_not_absolute_pose(self):
        a=fixture();a[1][2].instances[C].attributes['type']='Animation.DeltaAnimationAsset';self.reject(a,'absolute')
    def test_missing_or_ambiguous_role(self):
        a=fixture();p.named(a[1][2].instances[C],'Name').value.data # Frozen value needs replacement document.
        a[1][2]=document(a[1][2].resource,C,'Animation.AnimationAsset',[node(name='Name',text='Animations/Idle')],'c');self.reject(a,'HandsIkPose')
        a=fixture();a[1].append(document('Animations/Second.dbx',O,'Animation.AnimationAsset',[node(name='Name',text='Animations/Second/HandsIkPose')],'4'))
        old=a[1][1];items=list(p.named(old.instances[T],'ReplacmentAnimations').children)
        a[1][1]=document(old.resource,T,'Animation.SpecificAnimTreeData',[node('array','ReplacmentAnimations',children=items+[node('item',ref='animations/second/'+O)])],'b');self.reject(a,'ambiguous')
    def test_skeleton_guid_type_name_and_hash_are_bound(self):
        a=fixture();a[1][3].instances[S].attributes['type']='Animation.AnimationAsset';self.reject(a,'type mismatch')
        a=fixture();p.named(a[1][2].instances[C],'Skeleton').attributes['ref']='characters/actualskeleton/'+O;self.reject(a,'GUID')
        a=fixture();a[3]['skeleton']['resource_sha256']='f'*64;self.reject(a,'skeleton identity')
    def test_skeleton_topology_and_fingerprint_must_match(self):
        for key,value in (('rig_fingerprint','fnv1a64:ffffffffffffffff'),('bones',[])):
            a=fixture();a[3]['skeleton'][key]=value;self.reject(a,'skeleton identity')
    def test_clip_resource_content_and_actual_type_are_bound(self):
        a=fixture();a[3]['clips'][0]['sha256']='f'*64;self.reject(a,'absent')
        a=fixture();a[4]['animations/different/handsikpose.res']['kind']='DeltaAnimation';self.reject(a,'type mismatch')
        a=fixture();a[4]['animations/different/handsikpose.res']['skeleton_sha256']='f'*64;self.reject(a,'different skeleton')
    def test_saved_pose_cannot_override_evaluation(self):
        a=fixture();a[3]['clips'][0]['hands']['weapon_relative']['RightHand'][12]=.4;self.reject(a,'differs')
    def test_varying_controls_do_not_become_static_grips(self):
        a=fixture();a[4]['animations/different/handsikpose.res']['hands']['evaluation_status']='decoded_spline_candidate';self.reject(a,'not static')
    def test_nonfinite_nonrigid_or_nonaffine_matrices_reject(self):
        for index,value in ((0,math.nan),(5,3.),(3,.2)):
            a=fixture();a[4]['animations/different/handsikpose.res']['hands']['weapon_relative']['RightHand'][index]=value
            a[3]['clips'][0]['hands']=copy.deepcopy(a[4]['animations/different/handsikpose.res']['hands']);self.reject(a)
    def test_mesh_reference_document_type_and_content(self):
        a=fixture();a[1][4].instances[M].attributes['type']='Render.MeshShaderSetAsset';self.reject(a,'type mismatch')
        a=fixture();a[2]['weapons'][0]['states'][0]['meshes'][0]['mesh_document_sha256']='f'*64;self.reject(a,'content mismatch')
        a=fixture();a[2]['weapons'][0]['states'][0]['meshes'][0]['reference']='render/other/'+M;self.reject(a,'reference mismatch')
    def test_ambiguous_mesh_variant_rejects(self):
        a=fixture();a[2]['weapons'][0]['states'][0]['meshes'][0]['geometry_variants'].append({'mesh_sha256':'0'*64});self.reject(a,'Ambiguous')
    def test_mesh_weapon_and_state_identity(self):
        a=fixture();a[2]['weapons'][0]['instance_guid']=O;self.reject(a,'identity mismatch')
        a=fixture();a[2]['weapons'][0]['states'][0]['index']=1;self.reject(a,'state missing')
    def test_duplicate_exact_pose_or_weapon_rejects(self):
        a=fixture();a[3]['clips'].append(copy.deepcopy(a[3]['clips'][0]))
        with self.assertRaisesRegex(ValueError,'Duplicate'):result(a)
        a=fixture();a[0]['resolved_weapons'].append(copy.deepcopy(a[0]['resolved_weapons'][0]))
        with self.assertRaisesRegex(ValueError,'Duplicate'):result(a)
    def test_external_variant_requires_unique_content_not_similar_directory(self):
        a=fixture();row,source=p.unique_pose_source('Animations/Different/HandsIkPose.res',a[3]);self.assertEqual(source['archive'],'Dist/explicit.fbrb')
        extra=copy.deepcopy(row);extra['sha256']='f'*64;a[3]['clips'].append(extra)
        with self.assertRaisesRegex(ValueError,'ambiguous'):p.unique_pose_source(row['resource'],a[3])
    def test_external_missing_hash_or_escaping_path_rejects(self):
        for field,value in (('archive','../elsewhere.fbrb'),('index_sha256','bad')):
            a=fixture();a[3]['clips'][0]['sources'][0][field]=value
            with self.assertRaises(ValueError):p.unique_pose_source(a[3]['clips'][0]['resource'],a[3])
    def test_header_requires_explicit_assets_and_digests(self):
        out=result(fixture())
        with self.assertRaises(ValueError):p.cpp_header(out,set())
        with self.assertRaises(ValueError):p.cpp_header(out,{'Absent'})
        out['profiles'][0]['binding_digest']='0'*64
        with self.assertRaisesRegex(ValueError,'digest'):p.cpp_header(out,{'Rifle_sp'})
    def test_header_deduplicates_equivalent_keys_rejects_conflict(self):
        out=result(fixture());other=copy.deepcopy(out['profiles'][0]);other['weapon']['instance_guid']=O
        other['binding_digest']=p.digest({k:v for k,v in other.items() if k!='binding_digest'});out['profiles'].append(other)
        self.assertEqual(len(p.header_profiles(out,{'Rifle_sp'})),1)
        other['right_hand_in_weapon'][12]=.1;other['binding_digest']=p.digest({k:v for k,v in other.items() if k!='binding_digest'})
        with self.assertRaisesRegex(ValueError,'Conflicting'):p.header_profiles(out,{'Rifle_sp'})
    def test_header_keeps_different_exact_mesh_keys(self):
        out=result(fixture());other=copy.deepcopy(out['profiles'][0]);other['configured_mesh_path']='Render/OtherExactMesh'
        other['binding_digest']=p.digest({k:v for k,v in other.items() if k!='binding_digest'});out['profiles'].append(other)
        self.assertEqual(len(p.header_profiles(out,{'Rifle_sp'})),2)
    def test_header_never_accepts_invented_runtime_status_or_multistate(self):
        for key,value in (('runtime_accepted',True),('active_native_mesh_binding',True),('state_count',2),('right_hand_status','decoded_spline_candidate')):
            out=result(fixture());row=out['profiles'][0];row[key]=value;row['binding_digest']=p.digest({k:v for k,v in row.items() if k!='binding_digest'})
            with self.assertRaises(ValueError):p.cpp_header(out,{'Rifle_sp'})
    def test_header_uses_expected_consumer_contract(self):
        text=p.cpp_header(result(fixture()),{'Rifle_sp'})
        self.assertIn('std::array<AuthoredGripProfile,1>',text);self.assertIn('0x0123456789abcdefULL',text)
        self.assertIn('"Render/ExactMesh"',text);self.assertIn('math::Matrix4',text)
    def test_support_class_requires_exact_typed_source(self):
        for klass,enabled in (('wcAssault',True),('wcSmg',True),('wcLmg',False),('wcUgl',False),('wcShotgun',False),('wcHgr',False),('Other',False)):
            a=fixture();a[0]['resolved_weapons'][0]['weapon_class']=klass
            d=a[1][0];items=list(d.instances[W].children)
            items=[node(name='WeaponClass',text=klass) if n.attributes.get('name')=='WeaponClass' else n for n in items]
            a[1][0]=document(d.resource,W,d.instances[W].attributes['type'],items,'a')
            row=first(a);self.assertEqual(row['authored_rifle_support'],enabled)
            header=p.cpp_header(result(a),{'Rifle_sp'});self.assertIn(','+str(enabled).lower()+'},',header)
        a=fixture();a[0]['resolved_weapons'][0]['weapon_class']='wcSmg';self.reject(a,'class mismatch')
    def test_support_flag_cannot_enable_other_classes(self):
        out=result(fixture());row=out['profiles'][0];row['weapon_class']='wcUgl'
        row['binding_digest']=p.digest({k:v for k,v in row.items() if k!='binding_digest'})
        with self.assertRaisesRegex(ValueError,'classification mismatch'):p.cpp_header(out,{'Rifle_sp'})
        row['authored_rifle_support']=1;row['weapon_class']='wcAssault'
        row['binding_digest']=p.digest({k:v for k,v in row.items() if k!='binding_digest'})
        with self.assertRaisesRegex(ValueError,'classification mismatch'):p.cpp_header(out,{'Rifle_sp'})
    def test_schema_and_count_bound(self):
        a=fixture();a[0]['schema_version']=2
        with self.assertRaises(ValueError):result(a)
        a=fixture();a[3]['clips']*=1025
        with self.assertRaisesRegex(ValueError,'count bound'):result(a)

class ModelAxisTests(unittest.TestCase):
    def fixture(self):
        out=result(fixture());row=out['profiles'][0]
        axis={k:row[k] for k in ('native_asset_name','configured_mesh_path','rig_fingerprint','binding_digest','animation_sha256','skeleton_sha256')}
        axis.update(mesh_sha256=row['meshes'][0]['geometry']['mesh_sha256'],model_forward=[0,0,-1],model_up=[0,1,0],review={
            'status':'authored_geometry_reviewed','native_verified':False,'geometry_image_sha256':'1'*64,
            'firing_fields_sha256':'2'*64,'static_flash_sha256':'3'*64,'description':'Synthetic exact-key frame review'})
        return out,{'schema':'fvr.bc2.authored_model_axes','schema_version':1,'profiles':[axis]}
    def test_optional_exact_axis_emits_evidence_without_changing_binding(self):
        out,axes=self.fixture();before=copy.deepcopy(out)
        text=p.cpp_header(out,{'Rifle_sp'},axes)
        self.assertIn('math::Vec3{0.f,0.f,-1.f}',text);self.assertIn(p.digest(axes['profiles'][0]),text)
        self.assertEqual(out,before);self.assertNotIn('math::Vec3',p.cpp_header(out,{'Rifle_sp'}))
    def test_wrong_identity_and_duplicate_keys_reject(self):
        for key in ('native_asset_name','configured_mesh_path','rig_fingerprint','binding_digest','animation_sha256','skeleton_sha256','mesh_sha256'):
            out,axes=self.fixture();axes['profiles'][0][key]='0'*64
            with self.subTest(key=key),self.assertRaises(ValueError):p.cpp_header(out,{'Rifle_sp'},axes)
        out,axes=self.fixture();axes['profiles']*=2
        with self.assertRaisesRegex(ValueError,'Duplicate'):p.cpp_header(out,{'Rifle_sp'},axes)
    def test_bad_axis_or_native_promotion_reject(self):
        for value in ([0,0,0],[0,0,2],[0,math.nan,1],[True,0,0],[0,0]):
            out,axes=self.fixture();axes['profiles'][0]['model_forward']=value
            with self.subTest(value=value),self.assertRaises(ValueError):p.cpp_header(out,{'Rifle_sp'},axes)
        out,axes=self.fixture();axes['profiles'][0]['model_up']=[0,0,-1]
        with self.assertRaises(ValueError):p.cpp_header(out,{'Rifle_sp'},axes)
        out,axes=self.fixture();axes['profiles'][0]['review']['native_verified']=True
        with self.assertRaises(ValueError):p.cpp_header(out,{'Rifle_sp'},axes)
        out,axes=self.fixture();del axes['profiles'][0]['review']['static_flash_sha256']
        with self.assertRaises(ValueError):p.cpp_header(out,{'Rifle_sp'},axes)
    def test_schema_bounds_and_no_class_default(self):
        out,axes=self.fixture();axes['profiles']*=129
        with self.assertRaisesRegex(ValueError,'count bound'):p.cpp_header(out,{'Rifle_sp'},axes)
        out,axes=self.fixture();axes['schema_version']=2
        with self.assertRaises(ValueError):p.cpp_header(out,{'Rifle_sp'},axes)
        out,axes=self.fixture();self.assertEqual(p.axis_descriptors(p.header_profiles(out,{'Rifle_sp'}),None),{})

if __name__=='__main__':unittest.main()
