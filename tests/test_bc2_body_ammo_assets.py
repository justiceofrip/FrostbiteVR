from pathlib import Path
import importlib.util,json,struct,sys,unittest
TOOLS=Path(__file__).resolve().parents[1]/'tools'
sys.path.insert(0,str(TOOLS))
from bc2_body_equipment_assets import SPECS as EQUIPMENT_SPECS
from bc2_firearm_body_aliases import expand_equipment_rows
spec=importlib.util.spec_from_file_location('body_assets',TOOLS/'bc2_body_ammo_assets.py')
assets=importlib.util.module_from_spec(spec);spec.loader.exec_module(assets)
class Format(unittest.TestCase):
    def test_ascii_strings_are_bounded(self):
        self.assertEqual(assets.text('part'),struct.pack('<H',4)+b'part')
        for bad in ('','x'*513,'line\nbreak','embedded\0nul','é'):
            with self.assertRaises((ValueError,UnicodeError)):assets.text(bad)
    def test_reviewed_catalog_reproduces_compiled_header(self):
        root=TOOLS.parent;rows=json.loads((root/'config/body-ammo-assets.json').read_text())['profiles']
        self.assertEqual(assets.catalog_header(rows),(root/'src/games/bc2/Bc2BodyAmmoAssetProfiles.h').read_text())
        self.assertEqual(len(rows),252)
        supplies=[r for r in rows if not r.get('display_only')]
        supply_names={r['asset'] for r in supplies}
        self.assertEqual(len(supplies),69)
        self.assertEqual(len(supply_names),65)
        self.assertTrue({'XM8_sp_s','SPAS12_sp','AEK971_sp','XM8C','M416','MG36','XM8 LMG','SCAR_sp','SCAR_sp_s','F2000_sp','MP443_sp','Nambu100'}<=supply_names)
        self.assertTrue({'MP412','Garand','NS2000','870MCS','M24','SV98','GOL','M95'}<=supply_names)
        self.assertEqual(sum(s['part_triangles'] for r in rows if r['asset'] in {'XM8_sp_s','SPAS12_sp','AEK971_sp'} and not r.get('display_only') for s in r['sections']),1146)
        equipment=[r for r in rows if r.get('display_only')]
        identities=[(r['asset'],r['archive'],r['mesh'],r['closed_clip']) for r in equipment if not r.get('configuration_path')]
        self.assertEqual(set(identities),set(EQUIPMENT_SPECS))
        self.assertEqual(len(identities),len(set(identities)))
        sampled=[r for r in equipment if r['closed_pose']=='sampled_authored_mod_reference']
        self.assertEqual(len(sampled),15)
        self.assertTrue(all(r.get('mod_authored_display') or r.get('mod_authored_firearm_display') for r in sampled))
        self.assertTrue(all(r['closed_pose']=='static_authored_pose' for r in equipment if r not in sampled))
        exact=[r for r in equipment if r.get('configuration_path')]
        self.assertEqual(len(exact),164)
        self.assertEqual(len({r['configuration_path'] for r in exact}),164)
        expanded=expand_equipment_rows(exact)
        self.assertEqual(len(expanded),181)
        self.assertEqual(len({r['configuration_path'] for r in expanded}),181)
        self.assertTrue(all(sorted(s['mesh'] for s in r['sources'])==r['configured_meshes'] for r in expanded))
        nambu=[r for r in rows if r['asset']=='Nambu100']
        self.assertEqual(len(nambu),2)
        magazine=next(r for r in nambu if r.get('rigid_assembly'))
        self.assertEqual(magazine['part'],'jntWpn_12')
        self.assertEqual(sum(s['part_triangles'] for s in magazine['sections']),510)
        self.assertTrue(magazine['excluded_depth_only_sections'])
        self.assertEqual([m['bone'] for m in magazine['assembly_source']['profile']['assembly']['members']],
                         ['jntWpn_13','jntWpnwpnJnt_14','jntWpn_15'])
    def test_archive_escape_rejected_before_access(self):
        with self.assertRaises(ValueError):assets.archive(Path('G:/example/installation'),'../../outside.fbrb')
if __name__=='__main__':unittest.main()
