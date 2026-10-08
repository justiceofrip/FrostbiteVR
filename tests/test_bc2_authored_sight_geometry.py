import copy,math,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import bc2_authored_sight_geometry as sight
from bc2_weapon_config_pipeline import Node,Value,Document,Resolver
import bc2_authored_magazine_geometry as m

def node(tag='field',name=None,value='',ref=None,children=(),kind=None,guid=None):
    attrs={}
    if name:attrs['name']=name
    if ref:attrs['ref']=ref
    if kind:attrs['type']=kind
    if guid:attrs['guid']=guid
    return Node(tag,attrs,Value(2,0,1,value),tuple(children),0)
G=['00000000-0000-0000-0000-'+f'{i:012d}' for i in range(1,5)]
def docs(duplicate=False,asset_type='Animation.AnimationAsset',role='1P_AltDeploy',skeleton_ref=None):
    sk=node('instance',kind='Animation.SkeletonAsset',guid=G[2],children=[node(name='Name',value='skeleton')])
    asset=node('instance',kind=asset_type,guid=G[1],children=[node(name='Name',value='clips/'+role),node(name='Skeleton',ref=skeleton_ref or 'skeleton/'+G[2])])
    refs=[node('item',ref='clips/alt/'+G[1])]
    if duplicate:refs.append(node('item',ref='clips/alt/'+G[1]))
    tree=node('instance',kind='Animation.SpecificAnimTreeData',guid=G[0],children=[node('array','ReplacmentAnimations',children=refs)])
    state=node('complex',children=[node(name='AnimTree1p',ref='tree/'+G[0])])
    documents=[Document(path,'a'*64,node(),{n.attributes['guid']:n}) for path,n in [('tree.dbx',tree),('clips/alt.dbx',asset),('skeleton.dbx',sk)]]
    return documents,state
def hinge(angle,axis=(0,-1,0),pivot=(.03,.04,-.7)):
    x,y,z=axis;c=math.cos(angle);s=math.sin(angle);t=1-c
    # Parent translation stays identical; this is a joint rotating in place.
    return [c+x*x*t,x*y*t+z*s,x*z*t-y*s,0,x*y*t-z*s,c+y*y*t,y*z*t+x*s,0,x*z*t+y*s,y*z*t-x*s,c+z*z*t,0,*pivot,1]
def good():
    closed=hinge(0);opened=hinge(.718);forward=[hinge(.718*n/32) for n in range(33)];reverse=list(reversed(forward))
    return closed,opened,forward,reverse
class ExactRole(unittest.TestCase):
    def test_typed_guid_role(self):
        d,s=docs();r=sight.role(Resolver(d),d[0],s,'1p_altdeploy');self.assertEqual(r['resource'],'clips/1P_AltDeploy.res');self.assertEqual(r['asset']['instance_guid'],G[1])
    def test_missing_wrong_type_duplicate(self):
        for kwargs in ({'duplicate':True},{'asset_type':'Animation.DeltaAnimationAsset'},{'role':'1P_Reload'},{'skeleton_ref':'skeleton/'+G[3]}):
            with self.subTest(kwargs=kwargs),self.assertRaises(ValueError):
                d,s=docs(**kwargs);sight.role(Resolver(d),d[0],s,'1p_altdeploy')
    def test_sibling_tree_not_identity(self):
        d,s=docs();r=Resolver(d);s.children[0].attributes['ref']='tree/'+G[3]
        with self.assertRaises(ValueError):sight.role(r,d[0],s,'1p_altdeploy')
    def test_wrong_skeleton_type(self):
        d,s=docs();d[2].instances[G[2]].attributes['type']='Render.SkinnedMeshAsset'
        with self.assertRaises(ValueError):sight.role(Resolver(d),d[0],s,'1p_altdeploy')
class MeasuredMotion(unittest.TestCase):
    def test_horizontal_reciprocal(self):
        p=sight.hinge_profile(*good());self.assertAlmostEqual(p['settled_travel_rad'],.718);self.assertLess(m.norm(m.sub(p['axis_part'],[0,-1,0])),1e-8)
    def test_parent_basis_invariance(self):
        c,o,f,r=good();rotation=hinge(.43,axis=(1,0,0),pivot=(0,0,0));convert=lambda mat:m.multiply(mat,rotation)
        result=sight.hinge_profile(convert(c),convert(o),list(map(convert,f)),list(map(convert,r)))
        self.assertLess(m.norm(m.sub(result['axis_part'],[0,-1,0])),1e-7)
    def test_animated_pivot_rejected(self):
        c,o,f,r=good();f[16][12]+=.01
        with self.assertRaises(ValueError):sight.hinge_profile(c,o,f,r)
    def test_direction_and_endpoints(self):
        for mutate in ('reverse','endpoint','travel','count'):
            c,o,f,r=good()
            if mutate=='reverse':r=f
            if mutate=='endpoint':r[-1]=hinge(.05)
            if mutate=='travel':o=hinge(2)
            if mutate=='count':f=[f[0]]
            with self.subTest(mutation=mutate),self.assertRaises(ValueError):sight.hinge_profile(c,o,f,r)
    def test_nonfinite_and_reflected_rejected(self):
        for value in (float('nan'),-1):
            c,o,f,r=good();f[12][0]=value
            with self.assertRaises(ValueError):sight.hinge_profile(c,o,f,r)
    def test_header_requires_exact_digest(self):
        with self.assertRaises(ValueError):sight.header({'schema':'fvr.bc2.authored_sight_geometry.v1','runtime_admitted':False,'digest':'0'*64})
if __name__=='__main__':unittest.main()
