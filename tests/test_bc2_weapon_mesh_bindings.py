from pathlib import Path
import sys,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import bc2_weapon_mesh_bindings as p
from bc2_weapon_config_pipeline import Document,Node,Value

G='12345678-1234-1234-1234-123456789abc'
H='22345678-1234-1234-1234-123456789abc'
BASE='Objects/Weapons/Rifle_Mesh'
def document(name=BASE,identifier=G,kind='Render.SkinnedMeshAsset'):
    field=Node('field',{'name':'Name'},Value(2,0,1,name),(),0)
    instance=Node('instance',{'guid':identifier,'type':kind},Value(2,0,1,''),(field,),0)
    root=Node('partition',{},Value(2,0,1,''),(instance,),0)
    return Document(BASE+'.dbx','a'*64,root,{identifier:instance})
def config(refs=None):
    return dict(schema='fvr.bc2.authored_weapon_configuration',schema_version=1,resolved_weapons=[
        dict(native_name='Rifle_sp',resource='Objects/Weapons/Rifle.dbx',resource_sha256='b'*64,instance_guid=H,
            weapon_states=[dict(index=0,mesh_asset_references=refs or [BASE.lower()+'/'+G],missing=[])])])
def inventory(profiles=None):
    return dict(schema='fvr.bc2.installed_weapon_inventory',schema_version=1,asset_profiles=profiles or [])
def profile():
    return dict(resource=BASE+'.res',geometry_variants=[dict(sha256='c'*64,lods=[dict(lod=0,data_resource=BASE+'_lod0_data.res',data_sha256='d'*64)])],sources=[],runtime_enabled=True)
def first(out):return out['weapons'][0]['states'][0]['meshes'][0]

class Bindings(unittest.TestCase):
    def test_exact_guid_chain_joins_geometry_without_sibling_admission(self):
        out=p.join(config(),[document()],inventory([profile()]));m=first(out)
        self.assertEqual(out['summary']['geometry_matches'],1);self.assertEqual(m['mesh_instance_guid'],G)
        self.assertFalse(m['runtime_admitted']);self.assertFalse(out['weapons'][0]['runtime_admitted'])
        self.assertNotIn('runtime_enabled',m)
    def test_missing_geometry_preserves_authored_mesh_without_guess(self):
        m=first(p.join(config(),[document()],inventory()))
        self.assertEqual(m['status'],'authored_mesh_resolved');self.assertEqual(m['geometry_status'],'not_in_inventory')
    def test_wrong_guid_and_wrong_instance_type_do_not_resolve(self):
        for d in (document(identifier=H),document(kind='Render.MeshShaderSetAsset')):
            self.assertEqual(first(p.join(config(),[d],inventory()))['status'],'unresolved')
    def test_authored_name_must_match_resource(self):
        self.assertEqual(first(p.join(config(),[document(name='Objects/Other')],inventory()))['status'],'unresolved')
    def test_multiple_meshes_and_states_stay_separate(self):
        c=config([BASE+'/'+G,'Objects/Optic/'+H]);c['resolved_weapons'][0]['weapon_states'].append(dict(index=1,mesh_asset_references=[BASE+'/'+G],missing=[]))
        out=p.join(c,[document()],inventory([profile()]))
        self.assertEqual(out['summary'],dict(weapon_definitions=1,mesh_references=3,resolved=2,geometry_matches=2,unresolved=1))
        self.assertEqual(len(out['weapons'][0]['states']),2)
    def test_all_geometry_variants_retained_without_choosing_one(self):
        item=profile();item['geometry_variants'].append(dict(sha256='e'*64,lods=[]))
        m=first(p.join(config(),[document()],inventory([item])))
        self.assertEqual(m['variant_count'],2);self.assertFalse(m['runtime_admitted'])
    def test_duplicate_profile_or_resource_is_ambiguous(self):
        with self.assertRaises(ValueError):p.join(config(),[document()],inventory([profile(),profile()]))
        with self.assertRaises(ValueError):p.join(config(),[document(),document()],inventory())
    def test_invalid_path_reference_remains_unresolved(self):
        for ref in (G,'../a/'+G,'/absolute/'+G,'C:/a/'+G,'Objects/../a/'+G,'Objects/a/notguid'):
            m=first(p.join(config([ref]),[document()],inventory()))
            self.assertEqual(m['status'],'unresolved');self.assertIn('reason',m)
    def test_schema_mismatch_rejects(self):
        c=config();c['schema_version']=2
        with self.assertRaises(ValueError):p.join(c,[document()],inventory())

if __name__=='__main__':unittest.main()
