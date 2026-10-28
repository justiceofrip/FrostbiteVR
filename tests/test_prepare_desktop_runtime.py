import ast
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import prepare_desktop_runtime as package


class PortablePackageTests(unittest.TestCase):
    def test_transitive_local_modules_are_complete(self):
        names=package.local_imports(ROOT/'tools',('bc2_body_ammo_assets','bc2_mechanism_audio_cache','bc2_shell_reload_audio_cache'))
        self.assertIn('bc2_granny_curves',names)
        self.assertIn('bc2_configured_body_assets',names)
        self.assertIn('bc2_magazine_assembly_cache',names)
        for name in names:
            ast.parse((ROOT/'tools'/(name+'.py')).read_text(encoding='utf-8-sig'))

    def test_only_cache_path_guards_change_in_audio_tools(self):
        for name,guard in (('bc2_mechanism_audio_cache',package.AUDIO_GUARD),('bc2_shell_reload_audio_cache',package.SHELL_GUARD)):
            text=(ROOT/'tools'/(name+'.py')).read_text(encoding='utf-8-sig')
            converted=package.public_audio_tool(name,text)
            self.assertNotIn("'G:'",converted)
            before=ast.parse(text);after=ast.parse(converted)
            funcs=lambda tree:{node.name:ast.dump(node) for node in tree.body if isinstance(node,ast.FunctionDef)}
            old,new=funcs(before),funcs(after)
            allowed={'prepare'} if name=='bc2_mechanism_audio_cache' else {'validate_output_location'}
            self.assertEqual({key for key in old if old[key]!=new[key]},allowed)
            with self.assertRaises(ValueError):package.public_audio_tool(name,text.replace(guard,'pass'))

    def test_package_paths_do_not_escape(self):
        for name in ('../escape','a/../escape','/absolute','C:/Windows','a\\b','a//b'):
            with self.subTest(name=name),self.assertRaises(ValueError):package.safe_relative(name)
        self.assertEqual(str(package.safe_relative('python/python.exe')),'python/python.exe')

    def test_new_startup_helpers_have_no_private_absolute_paths(self):
        for path in (ROOT/'launcher/runtime').iterdir():
            text=path.read_text(encoding='utf-8-sig')
            self.assertNotIn('G:\\',text,path.name)
            self.assertNotIn('outca',text,path.name)
            self.assertNotIn('bc2vr-recovery',text,path.name)
            if path.suffix=='.py':ast.parse(text)

    def test_complete_staging_preserves_normal_identity_and_rejects_fixture(self):
        scratch=ROOT/'test-temp';scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='public-stage-',dir=scratch) as directory:
            root=Path(directory).resolve()
            self.assertIn(scratch.resolve(),root.parents)
            binaries={}
            for key,name in package.RECEIPT_BINARIES.items():
                path=root/name;path.write_bytes(('fixture-'+key).encode())
                binaries[key]={'path':str(path),'sha256':package.sha(path)}
            record={'full_normal_builds':True,'diagnostic_only':False,'diagnostic_kind':'normal',
                    'checkpoint':416,'source_sha256':'a'*64,'binaries':binaries,'ordinary_resource_input_probe':False}
            receipt=root/'receipt.json';receipt.write_text(json.dumps(record),encoding='utf-8')
            out=root/'runtime'
            result=package.stage(ROOT,receipt,package.sha(receipt),out,'0.1.4')
            self.assertGreater(result['files'],30)
            runtime=json.loads((out/'runtime.json').read_text())
            self.assertEqual(runtime['source_sha256'],'a'*64)
            self.assertFalse(runtime['diagnostic_only'])
            self.assertFalse(runtime['game_assets_distributed'])
            self.assertFalse(result['bundled_ffmpeg'])
            self.assertTrue((out/'Dependencies.ps1').is_file())
            self.assertTrue((out/'Resolution.ps1').is_file())
            for name in ('BC2NativeResolution.exe','BC2ResolutionBootstrap.dll'):
                self.assertEqual((out/'bin'/name).read_bytes(),(root/name).read_bytes())
            self.assertTrue((out/'config/ffmpeg-download.json').is_file())
            self.assertNotIn('G:\\',(out/'runtime.json').read_text())
            self.assertFalse(any(p.suffix in ('.fvrprop','.wav','.res','.fbrb') for p in out.rglob('*')))
            for entry in runtime['files']:
                self.assertEqual(package.sha(out/entry['path']),entry['sha256'])
            for key in ('resolution','resolution_bootstrap'):
                entry=record['binaries'].pop(key)
                receipt.write_text(json.dumps(record),encoding='utf-8')
                missing=root/('missing-'+key)
                with self.assertRaisesRegex(ValueError,'missing required binary: '+key):
                    package.stage(ROOT,receipt,package.sha(receipt),missing,'0.1.4')
                self.assertFalse(missing.exists())
                record['binaries'][key]=entry
                original=Path(entry['path']).read_bytes()
                Path(entry['path']).write_bytes(b'changed resolution helper')
                receipt.write_text(json.dumps(record),encoding='utf-8')
                with self.assertRaisesRegex(ValueError,'Frozen binary changed: '+key):
                    package.stage(ROOT,receipt,package.sha(receipt),root/('changed-'+key),'0.1.4')
                Path(entry['path']).write_bytes(original)
            receipt.write_text(json.dumps(record),encoding='utf-8')
            dependency=root/'dependencies.json'
            dependency.write_text(json.dumps({'files':[{'path':'ffmpeg/ffmpeg.exe','source':binaries['host']['path'],'sha256':binaries['host']['sha256']}]}),encoding='utf-8')
            with self.assertRaisesRegex(ValueError,'Only Python is bundled'):
                package.stage(ROOT,receipt,package.sha(receipt),root/'bundled-ffmpeg-rejected','0.1.5',dependency)
            record['ordinary_resource_input_probe']=True
            receipt.write_text(json.dumps(record),encoding='utf-8')
            with self.assertRaisesRegex(ValueError,'Diagnostic fixture'):
                package.stage(ROOT,receipt,package.sha(receipt),root/'rejected','0.1.4')
            self.assertFalse((root/'rejected').exists())
            record['ordinary_resource_input_probe']=False
            receipt.write_text(json.dumps(record),encoding='utf-8')
            Path(binaries['probe']['path']).write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError,'Frozen binary changed'):
                package.stage(ROOT,receipt,package.sha(receipt),root/'changed','0.1.4')


if __name__=='__main__':unittest.main()
