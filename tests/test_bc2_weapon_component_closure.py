import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import copy
import unittest
import bc2_weapon_component_closure as c
from bc2_weapon_package_index import digest, identity
M=[1.,0,0,0,0,1.,0,0,0,0,1.,0,0,0,0,1.]
def signed(r,k):r[k]=digest(r);return r
def fixture():
    identity={'configuration_resource':'objects/a.dbx','configuration_sha256':'1'*64,'instance_guid':'A'*32,
       'meshes':[['objects/mesh/'+'b'*32,'2'*64,'3'*64]],'rig':'4'*16}
    v={'schema':'fvr.bc2.visibility-descriptors.v1','rows':[{'asset':'Same','authored_configuration':{'resource':'objects/a.dbx','sha256':'1'*64,'instance_guid':'A'*32,'mesh_references':[{'reference':'objects/mesh/'+'b'*32,'resource':'objects/mesh.dbx','sha256':'2'*64,'instance_guid':'b'*32}]},'meshes':['objects/mesh'],'rig_fingerprint':'4'*16,'resources':[{'mesh':'objects/mesh','archive':'dist/test.fbrb','index_sha256':'7'*64,'metadata_sha256':'5'*64,'lods':[{'lod':0,'resource':'objects/mesh_lod0.res','sha256':'6'*64,'sections':[{'normalized':True}]}]}]}]}
    identity=__import__('bc2_weapon_package_index').identity(v['rows'][0])
    p={'package_id':digest(identity),'asset':'Same','identity':identity,'components':{},'runtime_enabled':False}
    i={'schema':'fvr.bc2.weapon-package-index.prototype.v1','native_admission_granted':False,'rows':[p]}
    g=signed({'native_asset_name':'Same','weapon':{'resource':'Objects/A.dbx','sha256':'1'*64,'instance_guid':'A'*32},
      'rig_fingerprint':'fnv1a64:'+'4'*16,'configured_mesh_path':'Objects/Mesh',
      'meshes':[{'reference':'objects/mesh/'+'b'*32,'mesh_document_sha256':'2'*64,'configured_mesh_path':'Objects/Mesh','geometry':{'mesh_sha256':'5'*64,'lods':[{'lod':0,'data_resource':'objects/mesh_lod0.res','data_sha256':'6'*64}]}}],
      'right_hand_status':'static_authored_pose','left_hand_status':'static_authored_pose','right_hand_in_weapon':M,'left_hand_in_weapon':M},'binding_digest')
    m=signed({'native_asset_name':'Same','rig_fingerprint':'fnv1a64:'+'4'*16,'configured_mesh_path':'Objects/Mesh',
      'grip_binding_digest':g['binding_digest'],'mesh_sha256':'5'*64,'lod_sha256':'6'*64,
      'geometry':{k:M for k in ('attached_item','item_from_insertion','weapon_from_entry','item_from_hand')}},'profile_digest')
    doc=lambda schema,rows:{'schema':schema,'schema_version':1,'runtime_admission':False,'profiles':rows}
    return i,doc('fvr.bc2.authored_grip_bindings',[g]),doc('fvr.bc2.authored_magazine_geometry',[m]),v
class Tests(unittest.TestCase):
    def test_exact_legacy_mag_digest_backlink_and_lookup(self):
        i,g,m,v=fixture();r=c.build(i,[m,g],v);p=r['rows'][0]
        self.assertTrue(p['components']['feed_geometry']['data_ready'])
        self.assertNotIn('ammo_geometry',p['components'])
        self.assertIsNotNone(c.lookup(r,p['package_id'],'detachable_magazine_contacts'))
        self.assertFalse(p['runtime_enabled'])
    def test_same_name_variant_no_borrow_and_switch_reuse(self):
        i,g,m,v=fixture();b=copy.deepcopy(i['rows'][0]);b['identity']['configuration_resource']='objects/b.dbx';b['package_id']=digest(b['identity']);i['rows'].append(b)
        r=c.build(i,[g,m],v);a=i['rows'][0]['package_id']
        self.assertIsNone(c.lookup(r,b['package_id'],'detachable_magazine_contacts'))
        x=c.lookup(r,a,'two_hand_grip');x['data_ready']=False
        self.assertTrue(c.lookup(r,a,'two_hand_grip')['data_ready'])
    def test_no_grip_backlink_no_feed(self):
        i,g,m,v=fixture();r=c.build(i,[m],v);self.assertFalse(r['rows'][0]['components']);self.assertIn('backlink',r['component_excluded'][0]['reason'])
    def test_partial_mesh_hash_rig_or_config_fail_closed(self):
        for kind in ('mesh','rig','hash','guid'):
            i,g,m,v=fixture();r=g['profiles'][0];r.pop('binding_digest')
            if kind=='mesh':r['meshes']=[]
            if kind=='rig':r['rig_fingerprint']='fnv1a64:'+'9'*16
            if kind=='hash':r['weapon']['sha256']='9'*64
            if kind=='guid':r['weapon']['instance_guid']='9'*32
            signed(r,'binding_digest');out=c.build(i,[g,m],v);self.assertFalse(out['rows'][0]['components'])
    def test_tampered_digest_rejected(self):
        i,g,m,v=fixture();g['profiles'][0]['left_hand_status']='varying'
        with self.assertRaises(ValueError):c.build(i,[g,m],v)
    def test_mag_content_and_conflicting_weapon_rejected(self):
        for kind in ('lod','weapon','native'):
            i,g,m,v=fixture();r=m['profiles'][0];r.pop('profile_digest')
            if kind=='lod':r['lod_sha256']='8'*64
            if kind=='weapon':r['weapon']={**g['profiles'][0]['weapon'],'resource':'Objects/B.dbx'}
            if kind=='native':r['runtime_admitted']=True
            signed(r,'profile_digest');out=c.build(i,[g,m],v);self.assertNotIn('feed_geometry',out['rows'][0]['components'])
    def test_ambiguous_role_removes_first_ready(self):
        i,g,m,v=fixture();other=copy.deepcopy(g['profiles'][0]);other.pop('binding_digest');other['left_hand_in_weapon']=M.copy();other['left_hand_in_weapon'][12]=.1;signed(other,'binding_digest');g['profiles'].append(other)
        out=c.build(i,[g],v);self.assertFalse(out['rows'][0]['components']);self.assertFalse(out['rows'][0]['component_evidence'])
    def test_support_only_is_not_complete_grip(self):
        i,g,m,v=fixture();r=g['profiles'][0]
        s=signed({'native_asset_name':'Same','source_weapons':[r['weapon']],'rig_fingerprint':r['rig_fingerprint'],'configured_mesh_path':r['configured_mesh_path'],'left_hand_status':'static_authored_pose','left_hand_in_weapon':M},'binding_digest')
        out=c.build(i,[{'schema':'fvr.bc2.authored_support_bindings','schema_version':1,'runtime_admission':False,'profiles':[s]}],v)
        self.assertTrue(out['rows'][0]['component_evidence'][0]['data_ready']);self.assertFalse(out['rows'][0]['components'])
    def test_index_tampering_and_unknown_lookup_rejected(self):
        i,g,m,v=fixture();i['rows'][0]['identity']['rig']='8'*16
        with self.assertRaises(ValueError):c.build(i,[g,m],v)
        i,g,m,v=fixture()
        with self.assertRaises(ValueError):c.lookup(c.build(i,[g],v),'f'*64,'two_hand_grip')
    def test_enriched_input_never_retains_prior_cache_readiness(self):
        i,g,m,v=fixture();prior=c.build(i,[g,m],v)
        for docs in ([],[g]):
            with self.assertRaises(ValueError):c.build(prior,docs,v)
        fresh=c.build(i,[],v);self.assertFalse(fresh['rows'][0]['components'])
    def test_full_resource_evidence_required_and_third_digest_exact(self):
        i,g,m,v=fixture();out=c.build(i,[g,m]);self.assertFalse(out['rows'][0]['components'])
        i,g,m,v=fixture();v['rows'][0]['resources'][0]['lods'][0]['sections'][0]['vertices']=99
        out=c.build(i,[g,m],v);self.assertFalse(out['rows'][0]['components'])
    def test_package_geometry_and_cache_agreement_independently_required(self):
        i,g,m,v=fixture();v['rows'][0]['resources'][0]['metadata_sha256']='8'*64
        i['rows'][0]['identity']=identity(v['rows'][0]);i['rows'][0]['package_id']=digest(i['rows'][0]['identity'])
        out=c.build(i,[g,m],v);self.assertFalse(out['rows'][0]['components'])
    def test_ancillary_absence_allowed_contradiction_rejected(self):
        for mode in ('missing','correct','contradictory','whole_package_changed','base_missing'):
            i,g,m,v=fixture();row=v['rows'][0]
            row['meshes'].append('objects/scope');row['authored_configuration']['mesh_references'].append({'reference':'objects/scope/'+'c'*32,'resource':'objects/scope.dbx','sha256':'8'*64,'instance_guid':'c'*32})
            res=copy.deepcopy(row['resources'][0]);res.update(mesh='objects/scope',metadata_sha256='9'*64);res['lods'][0].update(resource='objects/scope_lod0.res',sha256='a'*64);row['resources'].append(res)
            i['rows'][0]['identity']=identity(row);i['rows'][0]['package_id']=digest(i['rows'][0]['identity'])
            profile=g['profiles'][0];profile.pop('binding_digest')
            accessory={'reference':'objects/scope/'+'c'*32,'mesh_document_sha256':'8'*64,'configured_mesh_path':'objects/scope','geometry':None}
            if mode in ('correct','contradictory'):accessory['geometry']={'mesh_sha256':'9'*64 if mode=='correct' else 'b'*64,'lods':[{'lod':0,'data_resource':'objects/scope_lod0.res','data_sha256':'a'*64}]}
            profile['meshes'].append(accessory)
            if mode=='base_missing':profile['meshes'][0]['geometry']=None
            signed(profile,'binding_digest');mag=m['profiles'][0];mag.pop('profile_digest');mag['grip_binding_digest']=profile['binding_digest'];signed(mag,'profile_digest')
            if mode=='whole_package_changed':res['lods'][0]['sections'][0]['vertices']=1
            out=c.build(i,[g,m],v)
            self.assertEqual('feed_geometry' in out['rows'][0]['components'],mode in ('missing','correct'),mode)
if __name__=='__main__':unittest.main()
