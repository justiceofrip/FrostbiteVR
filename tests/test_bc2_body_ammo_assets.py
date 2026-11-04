from pathlib import Path
from collections import Counter
import hashlib,importlib.util,json,struct,sys,unittest
TOOLS=Path(__file__).resolve().parents[1]/'tools'
sys.path.insert(0,str(TOOLS))
from bc2_body_equipment_assets import SPECS as EQUIPMENT_SPECS
from bc2_firearm_body_aliases import expand_equipment_rows
import bc2_underbarrel_body_cache as underbarrel
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
        self.assertEqual(len(rows),307)
        knife,=[r for r in rows if r['asset']=='KNV-1']
        self.assertEqual((knife['mesh'],knife['part']),('Objects/Weapons/Handheld/Knife/Knife_Mesh','jntWpn_1'))
        self.assertEqual([(s['section'],s['part_triangles']) for s in knife['sections']],
                         [('jntWpn_1_Plastic',1118),('jntWpn_1_Metal',950)])
        supplies=[r for r in rows if not r.get('display_only') and r is not knife]
        supply_names={r['asset'] for r in supplies}
        self.assertEqual(len(supplies),123)
        self.assertEqual(len(supply_names),80)
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
    def test_measured_underbarrel_keys_cover_every_exact_configuration(self):
        root=TOOLS.parent;rows=json.loads((root/'config/body-ammo-assets.json').read_text())['profiles']
        prior=json.dumps(rows[:264],sort_keys=True,separators=(',',':'),allow_nan=False).encode()
        self.assertEqual(hashlib.sha256(prior).hexdigest(),'91d00ab05df00306305bfad49be129675ade29d2bfadad42081f282e118237b4')
        retained=json.dumps(rows[:282],sort_keys=True,separators=(',',':'),allow_nan=False).encode()
        self.assertEqual(hashlib.sha256(retained).hexdigest(),'027dc8579ffc715878da0cdb97916ffa002b84b60b6d402ca606210e189cdcdc')
        models=json.loads((root/'profiles/weapons71-underbarrel-reload/geometry.json').read_text())['models']
        self.assertEqual(len(models),7)
        additions={underbarrel.cache_key(r):r for r in rows[264:306]}
        expected=set();configurations=set()
        for model in models:
            for selector in model['selectors']:
                configurations.add(selector['configurationPath'])
                for part in model['supply_parts']:
                    key=(selector['asset'],selector['mesh'],part,selector['rigFingerprint'])
                    expected.add(key);row=additions[key]
                    self.assertEqual(row['mesh_sha256'],selector['meshSha256'])
                    self.assertFalse(row.get('display_only'))
                    self.assertGreater(sum(s['part_triangles'] for s in row['sections']),0)
        self.assertEqual(len(configurations),80)
        self.assertEqual(set(additions),expected)
        self.assertEqual(len(expected),42)
        for asset in ('40mmgl','40mmsg','40mmsmk'):
            self.assertEqual(sum(r['asset']==asset for r in additions.values()),14)
        jobs=[]
        for family, (count, parts) in underbarrel.FAMILIES.items():
            source=json.loads((root/underbarrel.PROFILES/family/'source.json').read_text())
            measured=json.loads((root/underbarrel.PROFILES/family/'measurements.json').read_text())
            self.assertEqual(len(source['configuration_paths']),count)
            self.assertEqual(tuple(measured['supply']['parts']),parts)
            mesh=source['chain']['mesh']['resource'][:-4]
            family_rows=[r for r in additions.values() if r['mesh']==mesh]
            self.assertEqual({(r['asset'],r['part']) for r in family_rows},
                             {(asset,part) for asset in underbarrel.ASSETS for part in parts})
            for row in family_rows:
                profile={k:row[k] for k in ('asset','mesh','part','rig_fingerprint','archive',
                                           'archive_index_sha256','mesh_sha256','lod_sha256')}
                jobs.append(dict(profile=profile,family=family,
                                 triangles=measured['parts'][row['part']]['visible_triangles']))
        pending=underbarrel.pending_jobs(rows[:282],jobs)
        self.assertEqual(len(pending),24)
        self.assertEqual({underbarrel.cache_key(j['profile']) for j in pending},
                         {underbarrel.cache_key(r) for r in rows[282:306]})
        with self.assertRaisesRegex(ValueError,'remaining measured keys'):
            underbarrel.pending_jobs(rows[:282],jobs[:-1])
        corrupt=list(rows[:282]);corrupt[-1]=dict(corrupt[-1],mesh_sha256='0'*64)
        with self.assertRaisesRegex(ValueError,'Retained underbarrel supply'):
            underbarrel.pending_jobs(corrupt,jobs)
    def test_generated_header_preserves_existing_mixed_line_endings(self):
        root=TOOLS.parent;row=json.loads((root/'config/body-ammo-assets.json').read_text())['profiles'][264]
        lines=assets.catalog_header([row]).encode().splitlines()
        original=b''.join(line+(b'\r\n' if i%2 else b'\n') for i,line in enumerate(lines))
        added=dict(row,asset='new_supply')
        result=underbarrel.append_header_bytes(original,[row],[added])
        self.assertEqual(result.decode().splitlines(),assets.catalog_header([row,added]).splitlines())
        retained=Counter(line for line in original.splitlines(keepends=True)
                         if not line.startswith(b'inline constexpr std::array<BodyAmmoAssetProfile,'))
        actual=Counter(result.splitlines(keepends=True))
        self.assertTrue(all(actual[line]>=count for line,count in retained.items()))
        with self.assertRaisesRegex(ValueError,'generated header differs'):
            underbarrel.append_header_bytes(original.replace(b'40mmgl',b'unknown'),[row],[added])
    def test_append_preserves_catalog_bytes_and_cache_records(self):
        original=b'{\r\n  "profiles": [\r\n    {"asset": "old", "value": 1.000}\r\n  ]\r\n}\r\n'
        result=underbarrel.append_catalog_bytes(original,[{'asset':'new'}])
        self.assertTrue(result.startswith(original[:-len(b'\r\n  ]\r\n}\r\n')]))
        self.assertEqual(json.loads(result)['profiles'],[{'asset':'old','value':1.0},{'asset':'new'}])
        vertices=b''.join(struct.pack('<3f8B',float(n),0,0,0,0,0,0,255,0,0,0) for n in range(3))
        positions=b''.join(vertices[n*20:n*20+12] for n in range(3));indices=struct.pack('<3H',0,1,2)
        section=dict(section='round',count=3,stride=20,palette_size=1,part_palette_index=0,
                     position='Float3',vertex_skin_hash=f'{assets.fnv(vertices):016x}',
                     position_hash=f'{assets.fnv(positions):016x}',part_triangles=1)
        row=dict(asset='old',mesh='mesh',part='part',rig_fingerprint=1,sections=[section])
        def record(r):
            return (b''.join(assets.text(r[k]) for k in ('asset','mesh','part'))+struct.pack('<QI',1,1)+
                    assets.text('round')+struct.pack('<5I',len(vertices),len(indices),2,0,0)+vertices+indices)
        baseline=assets.MAGIC+struct.pack('<I',1)+record(row)
        new=dict(row,asset='40mmsmk')
        rows,merged=underbarrel.append_cache([row],baseline,[new],[record(new)])
        self.assertEqual(rows,[row,new]);self.assertEqual(merged[12:len(baseline)],baseline[12:])
        with self.assertRaisesRegex(ValueError,'Duplicate'):
            underbarrel.append_cache([row],baseline,[row],[record(row)])
        corrupt=dict(new,asset='40mmgl')
        with self.assertRaisesRegex(ValueError,'key differs'):
            underbarrel.append_cache([row],baseline,[corrupt],[record(new)])
        with self.assertRaisesRegex(ValueError,'addition bound'):
            underbarrel.append_cache([row],baseline,[new]*25,[record(new)]*25)
if __name__=='__main__':unittest.main()
