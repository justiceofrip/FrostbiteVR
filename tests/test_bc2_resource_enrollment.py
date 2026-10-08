from pathlib import Path
import copy,json,struct,sys,unittest
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
import bc2_resource_enrollment as enrollment
import bc2_configured_body_assets as configured
import bc2_body_ammo_assets as ammo

def inputs():
    paths=dict(jobs=ROOT/'profiles/resource-batch227/zero-bolt-batch/selected-jobs.json',
      reviews=ROOT/'profiles/resource-enrollment233/exact-reviews.json',
      geometry=ROOT/'profiles/resource-batch227/zero-bolt-batch/exact-geometry.json',
      visibility=ROOT/'profiles/resource-enrollment233/visibility.json',catalog=ROOT/'config/body-ammo-assets.json')
    result={name:json.loads(path.read_text()) for name,path in paths.items()}
    requested={j['identity']['assetPath'] for j in result['jobs']['descriptors']}
    # The compiler still requires an exact requested set. This test selects its
    # preserved twelve rows from the expanded installed catalog explicitly.
    result['catalog']['profiles']=[r for r in result['catalog']['profiles'] if not r.get('configuration_path') or r['configuration_path'] in requested]
    return result

class ExactEnrollment(unittest.TestCase):
    def test_reviewed_twelve_join_reproduces_registry(self):
        doc,header=enrollment.join(**inputs());self.assertEqual(len(doc['profiles']),12)
        self.assertEqual(header,(ROOT/'profiles/resource-enrollment233/ReviewedResourceRegistry.h').read_text())
        self.assertEqual({p['identity']['assetName'] for p in doc['profiles']},{'XM8C','M416','MG36','XM8 LMG'})
        self.assertEqual(len({p['native_id'] for p in doc['profiles']}),12)
        self.assertTrue(all(not p['native_per_variant_tested'] and not p['headset_tested'] for p in doc['profiles']))

    def test_incomplete_or_substituted_join_never_enrolls(self):
        original=inputs()
        for fault in range(20):
            x=copy.deepcopy(original);job=x['jobs']['descriptors'][0];key=job['identity']['weapon']['resource']
            v=next(v for v in x['visibility']['rows'] if v['authored_configuration']['resource']==key)
            b=next(b for b in x['catalog']['profiles'] if b.get('configuration_path')==job['identity']['assetPath'])
            g=next(g for g in x['geometry']['profiles'] if g['weapon']['resource']==key)
            if fault==0:x['reviews'].pop()
            if fault==1:x['reviews'][0]['descriptor_digest']='0'*64
            if fault==2:job['configuration']['values']['baseCapacity']+=1
            if fault==3:job['identity']['weapon']['sha256']='0'*64
            if fault==4:x['visibility']['rows'].append(copy.deepcopy(v))
            if fault==5:x['geometry']['profiles'].remove(g)
            if fault==6:v['meshes'].append('Objects/Unknown/Attachment')
            if fault==7:b['configured_meshes'].pop()
            if fault==8:b['authored_configuration']['sha256']='0'*64
            if fault==9:g['weapon']['sha256']='0'*64
            if fault==10:v['rig_fingerprint']='0'*16
            if fault==11:b['sources'][0]['lod_sha256']='0'*64
            if fault==12:b['sources'][0]['mesh_sha256']='0'*64
            if fault==13:v['weighted_section_data_verified']=False
            if fault==14:v['native_admitted']=True
            if fault==15:b['display_only']=False
            if fault==16:b['display_anchor_evaluation']['RightHand']='unsupported'
            if fault==17:b['configuration_path']+='Changed'
            if fault==18:b['mesh']='Objects/Unknown/Base'
            if fault==19:x['catalog']['profiles']=[r for r in x['catalog']['profiles'] if not(r['asset']==b['asset'] and r['part']=='jntWpn_6')]
            with self.subTest(fault=fault),self.assertRaises(ValueError):enrollment.join(**x)

    def test_vertex_compaction_preserves_exact_indexed_bytes(self):
        vertex=struct.pack('<3f8B',.1,.2,.3,0,0,0,0,255,0,0,0)
        source=b'PREFIX00'+vertex*3+b'UNUSED_TRAILER';indices=struct.pack('<3H',0,1,2)
        section=dict(stride=20,position='Float3',vertex_skin_hash=f'{ammo.fnv(vertex*3):016x}',position_hash=f'{ammo.fnv(vertex[:12]*3):016x}')
        row=dict(sections=[section]);header=ammo.MAGIC+struct.pack('<I',1)+ammo.text('gun')+ammo.text('mesh')+ammo.text('part')+struct.pack('<QI',1,1)+ammo.text('section')
        packed=header+struct.pack('<5I',len(source),len(indices),2,8,0)+source+indices
        result=configured.compact_cache_sections(packed,[row]);self.assertLess(len(result),len(packed))
        self.assertEqual(result,header+struct.pack('<5I',60,6,2,0,0)+vertex*3+indices)
        unused=bytearray(packed);unused[-7]^=1
        self.assertEqual(configured.compact_cache_sections(bytes(unused),[row]),result)
        corrupt=bytearray(packed);corrupt[len(header)+20+8]^=1
        with self.assertRaises(ValueError):configured.compact_cache_sections(bytes(corrupt),[row])

if __name__=='__main__':unittest.main()
