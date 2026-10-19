"""Real bounded FBRB fixtures, mocked decoder, no game assets or processes."""
import gzip
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import bc2_mechanism_audio_cache as pump
import bc2_shell_reload_audio_cache as shell


def archive_bytes(entries):
    names = bytearray(b'Resource\0')
    payload = bytearray()
    records = bytearray()
    for name, raw in entries:
        offset = len(names)
        names += name.encode('ascii') + b'\0'
        records += struct.pack('>6I', offset, 65536, len(payload), len(raw), len(raw), 0)
        payload += raw
    index = struct.pack('>2I', 2, len(names)) + names + struct.pack('>I', len(entries)) + records + b'\1' + struct.pack('>I', len(payload))
    compressed = gzip.compress(index)
    return b'FbRB' + struct.pack('>I', len(compressed)) + compressed + gzip.compress(payload)


class MemoryOutput:
    """No filesystem writes; deliberately outside the developer source tree."""
    drive = 'G:'
    parents = ()

    def resolve(self):
        return self

    def mkdir(self, **kwargs):
        pass

    def __truediv__(self, name):
        return self

    def write_bytes(self, data):
        pass

    def write_text(self, data, **kwargs):
        pass


class AudioResourceIdentityTests(unittest.TestCase):
    def prepare(self, module, game, expected_raw):
        name = module.SPAS if module is pump else module.RESOURCE
        frames = 28864 if module is pump else module.FRAMES
        pcm = struct.pack('<h', 1000) * frames
        resource_hash = 'SPAS_SHA' if module is pump else 'RESOURCE_SHA'
        with patch.object(module, resource_hash, hashlib.sha256(expected_raw).hexdigest()), patch.object(module, 'decode', return_value=(48000, pcm)) as decoder:
            if module is pump:
                result = module.prepare(game, Path('unused-ffmpeg'), MemoryOutput())
            else:
                encoded = module.cue(pcm, 48000, 0, frames, gain=.30)
                with patch.object(module, 'validate_output_location'), patch.object(module, 'PCM_SHA', module.sha(pcm)), patch.object(module, 'WAV_SHA', module.sha(encoded)), patch.object(Path, 'mkdir'), patch.object(Path, 'write_bytes'):
                    result = module.prepare(game, Path('unused-ffmpeg'), Path('unused-output'))
            decoder.assert_called_once_with(expected_raw, Path('unused-ffmpeg'))
            self.assertEqual(result['resource'], name)
            return result

    def test_changed_archive_layout_keeps_exact_resource_and_reports_observed_index(self):
        for module in (pump, shell):
            with self.subTest(module=module.__name__), tempfile.TemporaryDirectory() as folder:
                game = Path(folder)
                path = game / module.COMMON
                path.parent.mkdir(parents=True)
                name = module.SPAS if module is pump else module.RESOURCE
                raw = b'exact synthetic reviewed cue'
                path.write_bytes(archive_bytes([(name, raw)]))
                first = self.prepare(module, game, raw)
                path.write_bytes(archive_bytes([('unrelated-localization.res', b'different index and offset'), (name, raw)]))
                second = self.prepare(module, game, raw)
                self.assertNotEqual(first['index_sha256'], second['index_sha256'])
                self.assertEqual(second['index_sha256'], module.archives.Archive(path).index_sha256)
                self.assertEqual({k: v for k, v in first.items() if k != 'index_sha256'}, {k: v for k, v in second.items() if k != 'index_sha256'})

    def test_changed_required_bytes_missing_and_duplicate_resources_still_reject(self):
        for module in (pump, shell):
            name = module.SPAS if module is pump else module.RESOURCE
            for entries in ([(name, b'altered')], [('unrelated.res', b'original')], [(name, b'original'), (name, b'original')]):
                with self.subTest(module=module.__name__, entries=entries), tempfile.TemporaryDirectory() as folder:
                    game = Path(folder)
                    path = game / module.COMMON
                    path.parent.mkdir(parents=True)
                    path.write_bytes(archive_bytes(entries))
                    with patch.object(module, 'decode') as decoder, self.assertRaises(ValueError):
                        if module is pump:
                            module.prepare(game, Path('unused-ffmpeg'), MemoryOutput())
                        else:
                            with patch.object(module, 'validate_output_location'):
                                module.prepare(game, Path('unused-ffmpeg'), Path('unused-output'))
                    decoder.assert_not_called()


if __name__ == '__main__':
    unittest.main()
