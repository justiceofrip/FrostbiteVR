"""Capture private reload configuration for isolated BC2 machine-code emulation.

Read-only; no input, focus changes, hooks or native calls. Output contains game
configuration bytes and MUST stay private: do not commit or distribute it.
Reads validate identity and unchanged configuration, not atomic simulation state.
"""
from __future__ import annotations

import argparse
import ctypes
import datetime
import hashlib
import json
from pathlib import Path

from capture_reload_state import Image, Inspector
from capture_reload_server import capture, discover
from read_bc2 import Process


def creation_filetime(process):
    values = [ctypes.c_ulonglong() for _ in range(4)]
    get_times = ctypes.WinDLL('kernel32', use_last_error=True).GetProcessTimes
    get_times.argtypes = [ctypes.c_void_p] + [ctypes.POINTER(ctypes.c_ulonglong)] * 4
    get_times.restype = ctypes.c_int
    if not get_times(process.h, *(ctypes.byref(v) for v in values)):
        raise ctypes.WinError(ctypes.get_last_error())
    return values[0].value


def collect(pid, expected_creation):
    image = Image()
    process = Process(pid, expected_path=image.path)
    try:
        if creation_filetime(process) != expected_creation:
            raise ValueError('Process creation identity changed')
        inspector = Inspector(process, image)
        owner = inspector.owner()
        weapons = []
        for slot, address in enumerate(owner['items']):
            if not address:
                continue
            try:
                weapon = inspector.weapon(address, slot)
                regions = []
                for name, typename, minimum in (('firing_data', 'WeaponFiringData', 0x48),
                                                ('primary_fire', 'FiringFunctionData', 0x194)):
                    info = inspector.require_type(weapon[name], typename)
                    if not minimum <= info['size'] <= 4096:
                        raise ValueError('Unsupported configuration bounds: ' + typename)
                    regions.append(dict(name=name, address=weapon[name],
                                        bytes=process.read(weapon[name], info['size']).hex()))
                branches = [inspector.state(weapon, offset) for offset in (0x3c, 0x40)]
                for region in regions:
                    original = bytes.fromhex(region['bytes'])
                    if process.read(region['address'], len(original)) != original:
                        raise ValueError('Configuration changed during capture')
                weapons.append(dict(weapon=weapon, regions=regions, branches=branches))
            except (ValueError, OSError) as error:
                weapons.append(dict(slot=slot, unsupported=str(error)))
        selected = capture(inspector, discover(image, process))
        if inspector.owner() != owner or creation_filetime(process) != expected_creation:
            raise ValueError('Owner changed across inventory capture')
        return dict(schema='fvr.bc2.private_emulation_fixture.v1', read_only=True,
                    process_writes=False, native_calls=False, desktop_input=False,
                    atomic_simulation_snapshot=False, distribution_allowed=False,
                    utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                    pid=pid, creation_filetime=expected_creation,
                    exe_sha256=hashlib.sha256(image.data).hexdigest(),
                    owner=owner, weapons=weapons, selected_server=selected)
    finally:
        process.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--creation-filetime', type=int, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Output exists; preserve previous evidence')
    if args.pid <= 0 or args.creation_filetime <= 0:
        parser.error('Positive process ID and creation FILETIME required')
    result = collect(args.pid, args.creation_filetime)
    # Exclusive creation prevents overwriting another capture that won a race.
    with args.output.open('x', encoding='utf-8') as output:
        json.dump(result, output, indent=2)
        output.write('\n')
    print(json.dumps(dict(output=str(args.output), read_only=True,
                          assets=[w.get('weapon', {}).get('asset_name') for w in result['weapons']],
                          distribution_allowed=False)))


if __name__ == '__main__':
    main()
