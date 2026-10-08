"""Exact SP data joins; no constructor, native receipt or headset simulation."""
from pathlib import Path
import copy,json,sys,unittest
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
import bc2_resource_enrollment as compiler

def inputs():
    folder=ROOT/'profiles/resource-enrollment235-scar'
    result={k:json.loads((folder/n).read_text()) for k,n in dict(jobs='selected-jobs.json',reviews='exact-reviews.json',geometry='exact-geometry.json',visibility='visibility.json').items()}
    result['catalog']=json.loads((ROOT/'config/body-ammo-assets.json').read_text())
    paths={j['identity']['assetPath'] for j in result['jobs']['descriptors']}
    result['catalog']['profiles']=[r for r in result['catalog']['profiles'] if not r.get('configuration_path') or r['configuration_path'] in paths]
    return result

class ScarEnrollment(unittest.TestCase):
    def test_exact_pair_reproduces_registry_and_complete_mesh_sets(self):
        doc,header=compiler.join(**inputs())
        self.assertEqual(header,(ROOT/'profiles/resource-enrollment235-scar/ScarRegistry.h').read_text())
        rows={r['identity']['assetName']:r for r in doc['profiles']}
        self.assertEqual(set(rows),{'SCAR_sp','SCAR_sp_s'})
        self.assertEqual(len(rows['SCAR_sp']['configured_meshes']),1)
        self.assertEqual(len(rows['SCAR_sp_s']['configured_meshes']),2)
        self.assertTrue(all(not r['native_per_variant_tested'] and not r['headset_tested'] for r in rows.values()))

    def test_wrong_scope_configuration_or_missing_detached_part_rejects(self):
        for variant in ('SCAR_sp','SCAR_sp_s'):
            for fault in range(8):
                x=inputs();job=next(j for j in x['jobs']['descriptors'] if j['identity']['assetName']==variant)
                key=job['identity']['assetPath'];v=next(v for v in x['visibility']['rows'] if v['asset']==variant)
                body=next(r for r in x['catalog']['profiles'] if r.get('configuration_path')==key)
                geometry=next(g for g in x['geometry']['profiles'] if g['native_asset_name']==variant)
                if fault==0:body['configuration_path']+='Changed'
                if fault==1:body['configured_meshes'].pop()
                if fault==2:v['rig_fingerprint']='0'*16
                if fault==3:geometry['weapon']['sha256']='0'*64
                if fault==4:body['sources'][0]['lod_sha256']='0'*64
                if fault==5:v['meshes'].append('Objects/Unknown/Attachment')
                if fault==6:body['display_anchor_evaluation']['RightHand']='unsupported'
                if fault==7:x['catalog']['profiles']=[r for r in x['catalog']['profiles'] if not(r['asset']==variant and not r.get('display_only'))]
                with self.subTest(variant=variant,fault=fault),self.assertRaises(ValueError):compiler.join(**x)

if __name__=='__main__':unittest.main()
