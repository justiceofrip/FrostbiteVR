"""Protect external manifest edits and exact restoration of the test fixture."""
from pathlib import Path
import json,sys,tempfile,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import bc2_test_bundle_mount as mount

class MountTests(unittest.TestCase):
    def original(self,newline=b'\r\n'):
        return newline.join([b'Signature '+b'0'*40,b'Authorative',b'',mount.SOURCE,
            b'BundleTarget package:levels/sp_02/level-00.fbrb',b'BundleTarget game:levels/sp_02/level-00.fbrb',
            b'BundleTarget game:levels/sp_common/level-00.fbrb',b'',b'BundleSource game:other',b'BundleTarget game:other',b''])
    def test_only_named_section_changes(self):
        for newline in (b'\n',b'\r\n'):
            old=self.original(newline);new=mount.candidate(old)
            self.assertEqual(new.replace(newline.join(mount.TARGETS)+newline,b'',1),old)
            with self.assertRaises(ValueError):mount.candidate(new)
            with self.assertRaises(ValueError):mount.candidate(old+old)
    def test_selected_level_does_not_touch_other_campaign_maps(self):
        old=self.original()+self.original().replace(b'sp_02',b'sp_03')
        new=mount.candidate(old,'sp_03')
        self.assertTrue(new.startswith(self.original()))
        with self.assertRaises(ValueError):mount.candidate(old,'sp_04')
    def test_base_only_fixture_excludes_package_archive(self):
        old=self.original();new=mount.candidate(old,include_package_common=False)
        self.assertIn(b'BundleTarget game:levels/mp_common/level-00.fbrb',new)
        self.assertNotIn(b'levels/mp2_common',new)
        self.assertEqual(new.replace(b'BundleTarget game:levels/mp_common/level-00.fbrb\r\n',b'',1),old)
    def test_conflict_preserves_external_change_and_restore_is_exact(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);p=root/'package.mft';old=self.original();new=mount.candidate(old)
            p.write_bytes(old);(root/'original.mft').write_bytes(old);(root/'candidate.mft').write_bytes(new)
            receipt=dict(schema='fvr.bc2.test_bundle_mount.v1',manifest=str(p),original_sha256=mount.sha(old),candidate_sha256=mount.sha(new))
            (root/'receipt.json').write_text(json.dumps(receipt))
            mount.change(root);self.assertEqual(p.read_bytes(),new)
            p.write_bytes(new+b'\nExternal change\n')
            with self.assertRaises(ValueError):mount.change(root,True)
            self.assertTrue(p.read_bytes().endswith(b'External change\n'))
            p.write_bytes(new);mount.change(root,True);self.assertEqual(p.read_bytes(),old)
    def test_bad_backups_rejected_without_writes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);p=root/'package.mft';old=self.original();new=mount.candidate(old)
            p.write_bytes(old);(root/'original.mft').write_bytes(old+b'bad');(root/'candidate.mft').write_bytes(new)
            (root/'receipt.json').write_text(json.dumps(dict(schema='fvr.bc2.test_bundle_mount.v1',manifest=str(p),original_sha256=mount.sha(old),candidate_sha256=mount.sha(new))))
            with self.assertRaises(ValueError):mount.change(root)
            self.assertEqual(p.read_bytes(),old)

if __name__=='__main__':unittest.main()
