"""Wait for the exact BC2 front end using read-only process handles, no input."""
import argparse
from collections import deque
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import time

sys.dont_write_bytecode = True

# Verified image and explicit Steam candidate. Derived using Bc2Profile::DiscoverProfile's
# Present/vtable/getter relationships; ObserveBc2 reads these same dimensions.
# This is read-only startup evidence, never native hook admission.
SUPPORTED_GAME_SHA = '3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258'
STEAM_GAME_SHA = '22e96001382eb847d3ab0a02b59ec7bb79ba47e04146e74c6c49e38465f38c96'


def validate_executable(raw, expected_sha, experimental_steam=False):
    """Bind the selected file; Steam opt-in retains native layout admission."""
    if hashlib.sha256(raw).hexdigest() != expected_sha:
        raise ValueError('Installed BC2 build changed')
    if expected_sha == SUPPORTED_GAME_SHA:
        return
    if expected_sha != STEAM_GAME_SHA or experimental_steam is not True:
        raise ValueError('This executable requires its supported compatibility option.')
    if len(raw) < 64 or len(raw) > 128 * 1024 * 1024 or raw[:2] != b'MZ':
        raise ValueError('Steam compatibility: invalid executable image')
    nt = struct.unpack_from('<I', raw, 0x3c)[0]
    if nt < 64 or nt + 24 > len(raw) or raw[nt:nt+4] != b'PE\0\0':
        raise ValueError('Steam compatibility: invalid PE header')
    machine, count, stamp = struct.unpack_from('<HHI', raw, nt + 4)
    opt_size = struct.unpack_from('<H', raw, nt + 20)[0]
    opt = nt + 24
    if not 1 <= count <= 96 or opt_size < 224 or opt + opt_size + 40 * count > len(raw):
        raise ValueError('Steam compatibility: truncated section table')
    magic = struct.unpack_from('<H', raw, opt)[0]
    base = struct.unpack_from('<I', raw, opt + 28)[0]
    size = struct.unpack_from('<I', raw, opt + 56)[0]
    if (machine, magic, stamp, base, size) != (0x14c, 0x10b, 1314210909, 0x400000, 0x1913000):
        raise ValueError('Steam compatibility: this image does not match the native BC2 build layout')
    for index in range(count):
        section = opt + opt_size + index * 40
        virtual_size, rva, raw_size, pointer = struct.unpack_from('<IIII', raw, section + 8)
        if pointer + raw_size > len(raw) or rva + max(virtual_size, raw_size) > size:
            raise ValueError('Steam compatibility: invalid section bounds')


RENDERER_VTABLE = 0x14497a0
RENDERER_PRESENT = 0x9eea30
WIDTH_GETTER = 0x9d8180
HEIGHT_GETTER = 0x9d8190
STARTUP_TIMEOUT_SECONDS = 90


def sample_menu_renderer(process, renderer_global, game_global):
    delta = process.base - 0x400000
    manager = process.u32(0x19a4ca8 + delta)
    owner = process.u32(manager + 0x82b24)
    expected_menu = (0x142b4e8 + delta, 0x142b4d4 + delta, 0x142b4c8 + delta)
    actual_menu = tuple(process.u32(owner + offset) for offset in (0, 0x2c, 0x90))
    if actual_menu != expected_menu:
        raise ValueError('Native main-menu owner is not ready')
    renderer = process.u32(renderer_global + delta)
    game = process.u32(game_global + delta)
    if min(renderer, game) < 0x10000:
        raise ValueError('Native renderer is not ready')
    table = RENDERER_VTABLE + delta
    if process.u32(renderer) != table:
        raise ValueError('The active renderer is not the supported Direct3D 11 renderer. '
                         'Close BC2, set DxVersion=11 in Documents/BFBC2/settings.ini, then retry.')
    slots = tuple(process.u32(table + offset) for offset in (20, 40, 44))
    expected_slots = (RENDERER_PRESENT + delta, WIDTH_GETTER + delta, HEIGHT_GETTER + delta)
    if slots != expected_slots or process.read(WIDTH_GETTER + delta, 4) != b'\x8b\x41\x18\xc3' or process.read(HEIGHT_GETTER + delta, 4) != b'\x8b\x41\x1c\xc3':
        raise ValueError('Direct3D 11 renderer code differs from the supported build; no VR module was attached.')
    dimensions = process.read(renderer + 0x18, 8)
    width, height = struct.unpack('<II', dimensions)
    if not (0 < width <= 16384 and 0 < height <= 16384 and width * height <= 32_000_000):
        raise ValueError('BC2 render size is invalid or exceeds the 32-million-pixel startup limit. '
                         'Choose a lower game resolution and retry.')
    # Re-read all ownership and layout values inside this sample. A resize or
    # renderer/menu replacement must restart the consecutive-sample witness.
    if (process.u32(0x19a4ca8 + delta) != manager or process.u32(manager + 0x82b24) != owner
            or tuple(process.u32(owner + offset) for offset in (0, 0x2c, 0x90)) != actual_menu
            or process.u32(renderer_global + delta) != renderer
            or process.u32(game_global + delta) != game or process.u32(renderer) != table
            or tuple(process.u32(table + offset) for offset in (20, 40, 44)) != slots
            or process.read(renderer + 0x18, 8) != dimensions):
        raise ValueError('Main-menu renderer or dimensions changed during observation')
    return dict(manager=manager, owner=owner, renderer=renderer, game_renderer=game,
                graphics_api='D3D11', width=width, height=height)


def main():
    from read_bc2 import Process
    from runtime_process import creation_filetime

    parser = argparse.ArgumentParser()
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--creation-filetime', type=int, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--expected-sha256', required=True)
    parser.add_argument('--experimental-steam', action='store_true')
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError('Preserve existing menu readiness evidence')
    exe = args.exe.resolve()
    raw = exe.read_bytes()
    validate_executable(raw, args.expected_sha256, args.experimental_steam)

    def field(pattern, offset):
        matches = list(re.finditer(pattern, raw, re.S))
        if len(matches) != 1:
            raise ValueError('Renderer signature changed')
        return struct.unpack_from('<I', raw, matches[0].start() + offset)[0]

    renderer_global = field(rb'\x55\x8b\xec\x83\xe4\xf0\x81\xec\x44\x01\x00\x00\x53\x56\x8b\x35....\x8b\x06\x8b\x50\x18\x57\x8b\xf9\x8b\xce\xff\xd2', 16)
    game_global = field(rb'\x56\x8b\x35....\x8b\x8e\xd8\x09\x00\x00\xe8....\x8b\xce\xe8....\x8b\x8e\xd8\x09\x00\x00\x5e\xe9', 3)
    observations = deque(maxlen=20)
    previous = None
    stable = 0
    started = time.monotonic()
    while time.monotonic() - started < STARTUP_TIMEOUT_SECONDS:
        process = None
        row = {'seconds': round(time.monotonic() - started, 3)}
        try:
            process = Process(args.pid, expected_path=exe)
            if creation_filetime(process) != args.creation_filetime:
                raise RuntimeError('BC2 process creation identity changed')
            sample = sample_menu_renderer(process, renderer_global, game_global)
            identity = tuple(sample.values())
            stable = stable + 1 if identity == previous else 1
            previous = identity
            row.update(sample, stable=stable)
        except (ValueError, OSError) as error:
            stable = 0
            previous = None
            row['waiting'] = str(error)
        finally:
            if process is not None:
                process.close()
        observations.append(row)
        if stable >= 3:
            break
        time.sleep(.1)
    passed = stable >= 3
    result = {'schema': 'fvr.main-menu-ready416.v1', 'passed': passed, 'pid': args.pid,
              'creation_filetime': str(args.creation_filetime), 'read_only': True,
              'sent_input': False, 'injected_module': False, 'stable_samples': stable,
              'elapsed_seconds': round(time.monotonic() - started, 3),
              'last_observations': list(observations),
              'graphics_api': row.get('graphics_api') if passed else None,
              'width': row.get('width') if passed else None,
              'height': row.get('height') if passed else None,
              'error': None if passed else row.get('waiting', 'Main menu did not become stable')}
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'passed': passed, 'output': str(args.output)}))
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
