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
from read_bc2 import Process
from runtime_process import creation_filetime


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--creation-filetime', type=int, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--expected-sha256', required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError('Preserve existing menu readiness evidence')
    exe = args.exe.resolve()
    raw = exe.read_bytes()
    if hashlib.sha256(raw).hexdigest() != args.expected_sha256:
        raise ValueError('Installed BC2 build changed')

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
    while time.monotonic() - started < 30:
        process = None
        row = {'seconds': round(time.monotonic() - started, 3)}
        try:
            process = Process(args.pid, expected_path=exe)
            if creation_filetime(process) != args.creation_filetime:
                raise RuntimeError('BC2 process creation identity changed')
            delta = process.base - 0x400000
            manager = process.u32(0x19a4ca8 + delta)
            owner = process.u32(manager + 0x82b24)
            expected = (0x142b4e8 + delta, 0x142b4d4 + delta, 0x142b4c8 + delta)
            actual = tuple(process.u32(owner + offset) for offset in (0, 0x2c, 0x90))
            if actual != expected:
                raise ValueError('Native main-menu owner is not ready')
            renderer = process.u32(renderer_global + delta)
            game = process.u32(game_global + delta)
            if min(renderer, game) < 0x10000:
                raise ValueError('Native renderer is not ready')
            # Double-read the owner within the sample; NativeProbe separately
            # verifies the full live code/profile before its hooks are armed.
            if process.u32(0x19a4ca8 + delta) != manager or process.u32(manager + 0x82b24) != owner:
                raise ValueError('Main-menu owner changed during observation')
            identity = (manager, owner, renderer, game)
            stable = stable + 1 if identity == previous else 1
            previous = identity
            row.update(manager=manager, owner=owner, renderer=renderer, game_renderer=game, stable=stable)
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
              'last_observations': list(observations)}
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'passed': passed, 'output': str(args.output)}))
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
