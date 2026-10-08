from pathlib import Path
import importlib.util,json,struct,sys,unittest
TOOLS=Path(__file__).resolve().parents[1]/'tools'
sys.path.insert(0,str(TOOLS))
from bc2_body_equipment_assets import SPECS as EQUIPMENT_SPECS
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
        self.assertEqual({r['asset'] for r in rows if not r.get('display_only')},{'XM8_sp_s','SPAS12_sp','AEK971_sp'})
        self.assertEqual(sum(s['part_triangles'] for r in rows if not r.get('display_only') for s in r['sections']),1146)
        equipment=[r for r in rows if r.get('display_only')]
        identities=[(r['asset'],r['archive'],r['mesh'],r['closed_clip']) for r in equipment]
        self.assertEqual(set(identities),set(EQUIPMENT_SPECS))
        self.assertEqual(len(identities),len(set(identities)))
        self.assertTrue(all(r['closed_pose']=='static_authored_pose' for r in equipment))
    def test_archive_escape_rejected_before_access(self):
        with self.assertRaises(ValueError):assets.archive(Path('G:/example/installation'),'../../outside.fbrb')
if __name__=='__main__':unittest.main()
