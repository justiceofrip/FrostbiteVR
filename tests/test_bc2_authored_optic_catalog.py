import dataclasses,struct,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import bc2_authored_optic_catalog as optic
from bc2_weapon_config_pipeline import Node,Value,Document,Resolver
G=[f'00000000-0000-0000-0000-{i:012d}' for i in range(1,6)]
def field(name,value='',ref=None,children=(),tag='field',kind='string'):
    attributes={'name':name} if name else {}
    if ref is not None:attributes['ref']=ref
    if kind=='float32':v=Value(7,4,1,struct.pack('>f',value))
    elif kind=='bool':v=Value(6,1,1,bytes([value]))
    else:v=Value(2,0,1,value)
    return Node(tag,attributes,v,tuple(children),0)
def instance(identifier,kind,children):return Node('instance',{'guid':identifier,'type':kind},Value(2,0,1,''),tuple(children),0)
def fixture(zoom_mesh=True,filter=False):
    mesh=instance(G[3],'Render.SkinnedMeshAsset',[field('Name','exact/scope')])
    scope_filter=instance(G[4],'Render.ColorTintScopeFilterData',[field('BlurScale',1.,kind='float32')])
    zoom=instance(G[2],'GameSharedResources.ZoomLevelData',[field('FieldOfView',11.5,kind='float32'),field('ForegroundBlurFilter','BfNone')])
    aim=instance(G[1],'GameSharedResources.SoldierAimingSimulationData',[field('ZoomType','ztPressButtonToSwitchZoomLevel'),
        field('ZoomLevels',children=[field(None,ref=G[2],tag='item')],tag='array')])
    state=field(None,tag='complex',children=[field('Meshes1p',tag='array',children=[field(None,ref='exact/scope/'+G[3],tag='item')]),
        field('MeshZoom1p',ref='exact/scope/'+G[3] if zoom_mesh else 'null'),field('ZoomedScopeFilter',ref=G[4] if filter else 'null'),
        field('NonZoomedScopeFilter',ref='null'),field('ZoomMeshTransitionFactor',1.,kind='float32')])
    weapon=instance(G[0],'GameSharedResources.SoldierWeaponData',[field('Name','shared-name'),field('WeaponClass','wcSniper'),
        field('AimingController',ref=G[1]),field('RenderFov',30.,kind='float32'),field('ZoomRenderFov',8.,kind='float32'),
        field('Hud',tag='complex',children=[field('CrosshairTypeId','sni_test'),field('WeaponClass','sni')]),
        field('WeaponStates',children=[state],tag='array')])
    root=field(None,tag='partition');doc=Document('weapon.dbx','a'*64,root,{n.attributes['guid']:n for n in (weapon,aim,zoom,scope_filter)})
    mesh_doc=Document('exact/scope.dbx','b'*64,root,{G[3]:mesh});return doc,mesh_doc,weapon,aim,zoom,state
class OpticCatalog(unittest.TestCase):
    def test_zoom_mesh_with_null_filters_is_not_no_optic(self):
        d,m,w,a,z,s=fixture();r=optic.describe(Resolver([d,m]),d,w)
        self.assertEqual(r['states'][0]['authored_route'],'zoom_mesh_only');self.assertEqual(r['states'][0]['ZoomedScopeFilter']['status'],'explicit_null')
        self.assertFalse(r['runtime_admitted']);self.assertEqual(r['runtime_path'],'unknown')
        self.assertEqual(r['zoom_levels'][0]['fields']['FieldOfView']['value'],11.5)
        self.assertEqual(r['weapon_render_fov']['fields']['ZoomRenderFov']['value'],8.)
        self.assertEqual(r['hud']['fields']['CrosshairTypeId']['value'],'sni_test')
    def test_filter_is_separate(self):
        for has_mesh,expected in ((True,'zoom_mesh_and_filter'),(False,'filter_without_zoom_mesh')):
            d,m,w,*_=fixture(has_mesh,True);r=optic.describe(Resolver([d,m]),d,w)
            self.assertEqual(r['states'][0]['authored_route'],expected)
            self.assertEqual(r['states'][0]['ZoomedScopeFilter']['type'],'Render.ColorTintScopeFilterData')
    def test_no_blanket_class_or_asset_inference(self):
        d,m,w,*_=fixture(False,False);r=optic.describe(Resolver([d,m]),d,w)
        self.assertEqual(r['states'][0]['authored_route'],'no_zoom_mesh_or_filter');self.assertEqual(r['runtime_path'],'unknown')
    def test_same_native_name_different_exact_config(self):
        d,m,w,*_=fixture();r=optic.describe(Resolver([d,m]),d,w)
        other=dataclasses.replace(d,resource='other.dbx',sha256='c'*64);q=optic.describe(Resolver([other,m]),other,w)
        self.assertEqual(r['asset'],q['asset']);self.assertNotEqual(r['digest'],q['digest'])
    def test_bad_guid_or_type_not_null(self):
        for bad in ('wrong-guid','wrong-type','missing-document','name-mismatch'):
            d,m,w,a,z,s=fixture();nodes=list(s.children)
            if bad=='wrong-guid':nodes[1]=field('MeshZoom1p',ref='exact/scope/'+G[4])
            if bad=='missing-document':nodes[1]=field('MeshZoom1p',ref='absent/'+G[3])
            if bad=='wrong-type':m.instances[G[3]].attributes['type']='GameSharedResources.ZoomLevelData'
            if bad=='name-mismatch':m.instances[G[3]]=dataclasses.replace(m.instances[G[3]],children=(field('Name','other/mesh'),))
            replacement=dataclasses.replace(s,children=tuple(nodes));children=list(w.children);children[-1]=field('WeaponStates',tag='array',children=[replacement]);w=dataclasses.replace(w,children=tuple(children))
            with self.subTest(bad=bad),self.assertRaises(ValueError):optic.describe(Resolver([d,m]),d,w)
    def test_malformed_and_duplicate_scalars_fail(self):
        for mutation in ('wrong-kind','nan','duplicate'):
            d,m,w,a,z,s=fixture();fields=list(z.children)
            if mutation=='wrong-kind':fields[0]=field('FieldOfView','11.5')
            if mutation=='nan':fields[0]=field('FieldOfView',float('nan'),kind='float32')
            if mutation=='duplicate':fields.append(fields[0])
            d.instances[G[2]]=dataclasses.replace(z,children=tuple(fields))
            with self.subTest(mutation=mutation),self.assertRaises(ValueError):optic.describe(Resolver([d,m]),d,w)
    def test_missing_known_value_stays_missing(self):
        d,m,w,a,z,s=fixture();r=optic.describe(Resolver([d,m]),d,w)
        self.assertIn('FovTransitionTime',r['zoom_levels'][0]['missing']);self.assertNotIn('FovTransitionTime',r['zoom_levels'][0]['fields'])
    def test_duplicate_and_excessive_levels_fail(self):
        for size in (2,17):
            d,m,w,a,z,s=fixture();levels=field('ZoomLevels',tag='array',children=[field(None,ref=G[2],tag='item')]*size)
            d.instances[G[1]]=dataclasses.replace(a,children=(a.children[0],levels))
            with self.assertRaises(ValueError):optic.describe(Resolver([d,m]),d,w)
    def test_explicit_null_cannot_smuggle_children(self):
        d,m,w,a,z,s=fixture();malformed=field('ZoomedScopeFilter',ref='null',children=[field('extra')])
        with self.assertRaises(ValueError):optic.bound_node(Resolver([d,m]),d,malformed,optic.FILTER_TYPES)
    def test_reference_closure_only_optical_fields(self):
        d,m,w,*_=fixture();refs=optic.required_references(d,w,Resolver([d,m]));self.assertEqual(set(refs),{'exact/scope.dbx'})
if __name__=='__main__':unittest.main()
