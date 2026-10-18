"""Derive one private shell-insertion cue from the owner's installed BC2 audio.

No audio is shipped. This does not install hooks, suppress stock sounds, grant
ammunition, or establish headset acceptance. The complete first authored reload
variation plays once; its native sound graph's loop is not reproduced.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
from bc2_mechanism_audio_cache import cue, decode
import inspect_bc2_mesh_asset as archives

COMMON = 'Dist/win32/levels/sp_common/level-00.fbrb'
INDEX = '57835d6ace174f83eeb46ec8bbfe3acf8a3e7fde5cb8c8e49f084950e975a842'
RESOURCE = 'Sound/Weapons/Handheld/UL_shg_SPAS12/Spas-12_reload_1_1p_Wave.res'
RESOURCE_SHA = 'a535681077aa7c82a193fadb10353ff8701b294691ba152fd9c5e35d08947e6e'
PCM_SHA = 'a516a10a23ac04ebcf76b3d371500bef8ec9a7ced6de5109a9b95fefb8b79484'
WAV_SHA = '3ec56ca5e0e7fd245d2a7cab3b7140cf45106fc1db4a555b654ffb680f0a7b15'
FRAMES = 29173
RATE = 48000
WAV_BYTES = 58390
FILE = 'spas-shell-insert.wav'
MANIFEST = 'shell-reload-audio.json'
ANIMATION = 'aa490af3-e76d-42de-81bb-2c3dce520f84'
SOUND_TEMPLATE = '4b8fa1d7-5d2f-4caf-9ba9-190410e13b8c'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def validate_output_location(output, source):
    """Arguments must already be resolved; accepting PurePaths aids CI checks."""
    if output.drive.upper() != 'G:' or output == source or source in output.parents:
        raise ValueError('Private audio cache must be outside the source repository on G:')


def encode_checked(rate, pcm):
    if rate != RATE or len(pcm) != FRAMES * 2:
        raise ValueError('Shell audio rate/sample count differs')
    if sha(pcm) != PCM_SHA:
        raise ValueError('Decoded shell audio differs')
    encoded = cue(pcm, rate, 0, FRAMES, gain=.30)
    if len(encoded) != WAV_BYTES or sha(encoded) != WAV_SHA:
        raise ValueError('Derived shell cue differs')
    return encoded


def prepare(game, ffmpeg, output):
    output = Path(output).resolve()
    validate_output_location(output, Path(__file__).resolve().parents[1])
    previous = archives.MAX_PAYLOAD
    try:
        archives.MAX_PAYLOAD = 640 * 1024 * 1024
        ar = archives.Archive(Path(game) / COMMON)
    finally:
        archives.MAX_PAYLOAD = previous
    if ar.index_sha256 != INDEX:
        raise ValueError('Common archive index differs')
    raw = ar.read_selected([RESOURCE])[RESOURCE]
    if sha(raw) != RESOURCE_SHA:
        raise ValueError('Unreviewed shell wave resource')
    rate, pcm = decode(raw, ffmpeg)
    encoded = encode_checked(rate, pcm)
    profile = {'schema': 'fvr.bc2.shell_reload_audio_cache.v1', 'asset': 'SPAS12_sp',
               'archive': COMMON, 'index_sha256': INDEX, 'resource': RESOURCE,
               'resource_sha256': RESOURCE_SHA, 'pcm_sha256': PCM_SHA,
               'animation_guid': ANIMATION, 'sound_template_guid': SOUND_TEMPLATE,
               'cue': {'file': FILE, 'sha256': WAV_SHA, 'bytes': len(encoded),
                       'sample_rate': RATE, 'channels': 1, 'bits_per_sample': 16,
                       'first_sample': 0, 'end_sample_exclusive': FRAMES,
                       'duration_s': FRAMES / RATE, 'gain': .30, 'edge_fade_samples': 96,
                       'loop': False, 'range_basis': 'complete authored variation; codec padding excluded'},
               'native_binding_observed': False, 'suppression_verified': False,
               'headset_accepted': False, 'runtime_enabled': False}
    manifest = (json.dumps(profile, indent=2, allow_nan=False) + '\n').encode('utf-8')
    if len(encoded) + len(manifest) > 128 * 1024:
        raise ValueError('Private shell cache exceeds 128 KiB')
    output.mkdir(parents=True, exist_ok=True)
    (output / FILE).write_bytes(encoded)
    (output / MANIFEST).write_bytes(manifest)
    return profile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('game', 'ffmpeg', 'output'):
        parser.add_argument('--' + option, type=Path, required=True)
    args = parser.parse_args()
    result = prepare(args.game, args.ffmpeg, args.output)
    print(json.dumps({'asset': result['asset'], 'cue': result['cue'], 'headset_accepted': False}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
