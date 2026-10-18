import json
import struct
import sys
import unittest
from pathlib import Path, PureWindowsPath
from unittest.mock import patch, Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import bc2_shell_reload_audio_cache as cache


class ShellReloadCacheTests(unittest.TestCase):
    def test_private_resolved_output_location(self):
        source = PureWindowsPath('G:/work/FrostbiteVR')
        cache.validate_output_location(PureWindowsPath('G:/private/shell-audio'), source)
        for output in ('C:/private', 'F:/private', 'G:/work/FrostbiteVR',
                       'G:/work/FrostbiteVR/build/audio', '//server/share/audio'):
            with self.subTest(output=output), self.assertRaises(ValueError):
                cache.validate_output_location(PureWindowsPath(output), source)

    def test_rate_count_and_pcm_identity_refused_before_cue(self):
        with patch.object(cache, 'cue') as cue:
            for rate, pcm in ((44100, bytes(cache.FRAMES*2)),
                              (48000, bytes(cache.FRAMES*2-2)),
                              (48000, bytes(cache.FRAMES*2+2)),
                              (48000, bytes(cache.FRAMES*2))):
                with self.subTest(rate=rate, size=len(pcm)), self.assertRaises(ValueError):
                    cache.encode_checked(rate, pcm)
            cue.assert_not_called()

    def test_encoded_output_identity_refused(self):
        pcm = bytes(cache.FRAMES*2)
        with patch.object(cache, 'PCM_SHA', cache.sha(pcm)), patch.object(cache, 'cue', return_value=bytes(cache.WAV_BYTES)):
            with self.assertRaisesRegex(ValueError, 'Derived shell cue differs'):
                cache.encode_checked(cache.RATE, pcm)

    def test_archive_and_resource_refusal_leave_output_absent(self):
        for index, raw in (('changed', b'wrong'), (cache.INDEX, b'wrong')):
            with self.subTest(index=index):
                output = Path('private-test-output-not-created')
                ar = Mock(index_sha256=index)
                ar.read_selected.return_value = {cache.RESOURCE: raw}
                with patch.object(cache, 'validate_output_location'), patch.object(cache.archives, 'Archive', return_value=ar), patch.object(cache, 'decode') as decode, patch.object(Path, 'mkdir') as mkdir, patch.object(Path, 'write_bytes') as write:
                    with self.assertRaises(ValueError):
                        cache.prepare(Path('installed'), Path('ffmpeg'), output)
                    decode.assert_not_called()
                    mkdir.assert_not_called()
                    write.assert_not_called()

    def test_archive_failure_restores_payload_bound(self):
        previous = cache.archives.MAX_PAYLOAD
        with patch.object(cache, 'validate_output_location'), patch.object(cache.archives, 'Archive', side_effect=ValueError('index')):
            with self.assertRaises(ValueError):
                cache.prepare(Path('installed'), Path('ffmpeg'), Path('not-created'))
        self.assertEqual(cache.archives.MAX_PAYLOAD, previous)

    def test_mocked_decoder_uses_entire_one_pass_cue_and_exact_manifest(self):
        raw = b'local fixture resource'
        pcm = struct.pack('<h', 20000) * cache.FRAMES
        expected = cache.cue(pcm, cache.RATE, 0, cache.FRAMES, gain=.30)
        ar = Mock(index_sha256=cache.INDEX)
        ar.read_selected.return_value = {cache.RESOURCE: raw}
        written = {}
        def save(path, data):
            written[path.name] = data
            return len(data)
        with patch.object(cache, 'validate_output_location'), patch.object(cache.archives, 'Archive', return_value=ar), patch.object(cache, 'RESOURCE_SHA', cache.sha(raw)), patch.object(cache, 'PCM_SHA', cache.sha(pcm)), patch.object(cache, 'WAV_SHA', cache.sha(expected)), patch.object(cache, 'decode', return_value=(cache.RATE, pcm)) as decode, patch.object(Path, 'mkdir') as mkdir, patch.object(Path, 'write_bytes', autospec=True, side_effect=save):
            output = Path('private-test-output-not-created')
            result = cache.prepare(Path('installed'), Path('ffmpeg'), output)
            decode.assert_called_once_with(raw, Path('ffmpeg'))
            ar.read_selected.assert_called_once_with([cache.RESOURCE])
            mkdir.assert_called_once_with(parents=True, exist_ok=True)
            self.assertEqual(written[cache.FILE], expected)
            self.assertEqual(json.loads(written[cache.MANIFEST]), result)
            self.assertEqual(result['cue']['first_sample'], 0)
            self.assertEqual(result['cue']['end_sample_exclusive'], cache.FRAMES)
            self.assertFalse(result['cue']['loop'])
            self.assertFalse(result['runtime_enabled'])
            self.assertFalse(result['headset_accepted'])
            self.assertEqual(sorted(written), sorted((cache.FILE, cache.MANIFEST)))


if __name__ == '__main__':
    unittest.main()
