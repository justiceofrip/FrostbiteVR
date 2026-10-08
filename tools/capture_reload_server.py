"""Bounded read-only selected BC2 client/server weapon ownership capture.

Uses QUERY_INFORMATION|VM_READ through the existing Process helper. No hooks,
input, native calls, or ammo writes. Object identity coherence is not simulation
atomicity and does not grant manual reload authority.
"""
from __future__ import annotations
import argparse
import datetime as dt
import hashlib
import json
import math
from pathlib import Path
import struct
import time
from capture_reload_state import Image, Inspector
from read_bc2 import Process

PATTERNS = {
 'context': ('B8 01 00 00 00 84 05 ?? ?? ?? ?? 0F 85 86 00 00 00 09 05 ?? ?? ?? ?? 53 33 DB 53 B9 ?? ?? ?? ?? E8', 0x9d),
 'manager': ('51 53 55 8B 6C 24 10 56 57 55 8B F1 E8 ?? ?? ?? ?? 8D 4E 10 E8 ?? ?? ?? ?? C7 46 4C ?? ?? ?? ?? 33 DB 89 5E 50 89 5E 54 8D 7E 4C C7 06 ?? ?? ?? ?? C7 46 10', 0x39),
 'create': ('56 8B F1 8B 4E 5C 2B 4E 58 33 C0 C1 F9 02 3B 4E 04 73 58 57 8D 4E 78 E8', 0x71),
 'player': ('0F 57 C0 53 33 DB 56 8B F1 C7 06 ?? ?? ?? ?? C7 46 10 ?? ?? ?? ?? B8 ?? ?? ?? ?? 89 46 08 89 46 0C 8B 44 24 0C 89 5E 14 89 5E 18 89 5E 1C 88 5E 20', 0x5a),
 'controlled': ('8B 81 3C 0C 00 00 C3 CC CC CC CC CC CC CC CC CC 8B 81 3C 0C 00 00 85 C0 74 06 05 C0 00 00 00 C3', 0x21),
 'item_count': ('8B 81 BC 02 00 00 2B 81 B8 02 00 00 C1 F8 02 C3', 16),
 'item_getter': ('8B 91 BC 02 00 00 2B 91 B8 02 00 00 8B 44 24 04 C1 FA 02 3B C2 73 0C 8B 89 B8 02 00 00 8B 04 81 C2 04 00 33 C0 C2 04 00', 40),
}
# Additional exact selected-item/effects/firing relationships from inspected code;
# hashes cover the complete native selected-item prefix, not just displacements.
PREFIX_RVA = 0x2a5ec0
PREFIX_SIZE = 0x67
PREFIX_SHA256 = '1e4b053656a8298c5addb8997952b8b2e33b54a7efe40087e43384bc22f7afe6'

def require(condition, message):
    if not condition:raise ValueError(message)

def discover(image, process=None):
    found, proof = {}, {}
    def region(name, rva, size):
        code = image.read(rva, size)
        if process is not None:require(process.read(image.base+rva, size) == code, name+' live code mismatch')
        proof[name] = {'rva': rva, 'size': size, 'sha256': hashlib.sha256(code).hexdigest()}
        return code
    for name, (pattern, size) in PATTERNS.items():
        rva = image.find(pattern);found[name] = rva;region(name, rva, size)
    word = lambda name, offset: struct.unpack('<I', image.read(found[name]+offset, 4))[0]
    context, table = word('context', 0x1c), word('manager', 0x2d)
    require(image.read(found['context']+0x97, 1) == b'\xb8' and word('context', 0x98) == context and image.read(found['context']+0x9c, 1) == b'\xc3', 'context getter links')
    require(image.read(found['player']+0x54, 6) == bytes.fromhex('898654010000'), 'player index store')
    require(image.read(found['create']+0x57, 6) == bytes.fromhex('8b566c8904ba') and image.read(found['create']+0x4f, 1) == b'\x57', 'server manager indexed player store')
    def call(rva):
        code = image.read(rva, 5);require(code[0] == 0xe8, 'expected native call')
        return rva+5+struct.unpack_from('<i', code, 1)[0]
    ctor = call(found['create']+0x52)
    region('server_player_constructor', ctor, 0x1f)
    require(image.read(ctor, 15) == bytes.fromhex('8b44240483ec3053555657508bf1e8') and call(ctor+0xe) == found['player'], 'server player constructor linkage')
    prefix = region('server_selected_item_prefix', PREFIX_RVA, PREFIX_SIZE)
    require(hashlib.sha256(prefix).hexdigest() == PREFIX_SHA256, 'server selected-item proof unsupported')
    selected_slot = call(PREFIX_RVA+0x24)
    require(call(PREFIX_RVA+0x45) == selected_slot and region('selected_slot', selected_slot, 7) == bytes.fromhex('8b814c010000c3'), 'selected slot native caller linkage')
    require(context >= image.base and table >= image.base, 'native pointer below module')
    image.section(context-image.base, 0x30);image.read(table-image.base, 12)
    if process is not None:require(process.base == image.base, 'relocated module unsupported')
    return {'context': context, 'manager_table': table, 'controlled_getter': image.base+found['controlled'], 'proof': proof}

def chain(inspect, binding):
    p = inspect.p;client = inspect.owner();cp = client['player'];pid = p.u32(cp+0x154)
    manager = p.u32(binding['context']+8)
    require(p.u32(manager) == binding['manager_table'], 'server manager type mismatch')
    capacity, players = p.u32(manager+4), p.u32(manager+0x6c)
    require(0 < capacity <= 256 and pid < capacity and players >= 0x10000 and players+capacity*4 <= 0xffffffff, 'server player array bounds')
    player = p.u32(players+pid*4)
    require(player >= 0x10000 and p.u32(player+0x154) == pid, 'server player index does not pair with client')
    table = inspect.image_pointer(p.u32(player), 0x34)
    require(p.u32(table+0x30) == binding['controlled_getter'], 'server controlled getter differs')
    soldier = p.u32(player+0xc3c)
    inspect.require_type(soldier, 'ServerSoldierEntity')
    require(p.u32(player+0xc6c) == soldier, 'server controlled actor differs')
    require(p.u32(soldier+0xc) == p.u32(client['actor']+0xc), 'client/server soldier data mismatch')
    require(p.u32(soldier+0x220) == player, 'server soldier player backlink mismatch')
    inventory = p.u32(soldier+0x2b4)
    inspect.require_type(p.u32(inventory+4), 'WeaponSwitchingData')
    begin, end = p.u32(soldier+0x2b8), p.u32(soldier+0x2bc)
    require(begin >= 0x10000 and begin < end <= 0xffffffff and end-begin <= 256 and (end-begin)%4 == 0, 'server item span bounds')
    items = list(struct.unpack('<'+'I'*((end-begin)//4), p.read(begin, end-begin)))
    slot = p.u32(inventory+0x14c)
    require(slot < len(items) and slot == client['selected_slot'], 'client/server selected slot differs')
    item = items[slot]
    require(item >= 0x10000 and items.count(item) == 1, 'server selected item missing or duplicated')
    weapon = inspect.weapon(client['selected_weapon'], client['selected_slot'])
    data, effects, firing = p.u32(item+4), p.u32(item+0xc), p.u32(item+0x10)
    inspect.require_type(data, 'SoldierWeaponData')
    require(data == weapon['data'], 'client/server selected weapon data mismatch')
    inspect.require_type(effects, 'ServerWeaponFiringEffects')
    require(p.u32(effects+0x10) == weapon['firing_data'] and p.u32(effects+0x128) == player, 'server effects data/player mismatch')
    require(p.u32(p.u32(effects+0xd4)+4) == weapon['firing_data'], 'server effects callback data mismatch')
    require(p.u32(firing) == inspect.firing_table and p.u32(firing+8) == weapon['firing_data'] and p.u32(firing+12) == weapon['ammo_address'], 'server firing identity/config mismatch')
    return {'client_owner': client, 'player_id': pid, 'server_manager': manager, 'server_player_array': players,
            'server_player_capacity': capacity, 'server_player': player, 'server_soldier': soldier,
            'server_inventory': inventory, 'server_items_begin': begin, 'server_items_end': end, 'server_items': items,
            'selected_slot': slot, 'server_item': item, 'server_effects': effects, 'server_firing': firing,
            'weapon_data': data, 'firing_data': weapon['firing_data'], 'ammo_address': weapon['ammo_address'],
            'asset_name': weapon['asset_name'], 'asset_path': weapon['asset_path']}

def capture(inspect, binding):
    before = chain(inspect, binding)
    raw = inspect.p.read(before['server_firing'], 0xb0)
    current, previous, following = struct.unpack_from('<III', raw, 0x3c)
    timer = struct.unpack_from('<f', raw, 0x50)[0]
    loaded, reserve = struct.unpack_from('<ii', raw, 0x7c)
    require(max(current, previous, following) <= 15 and math.isfinite(timer) and abs(timer) <= 1000000 and all(-1 <= v <= 1000000 for v in (loaded, reserve)), 'server state malformed')
    after = chain(inspect, binding)
    require(before == after, 'server/client ownership changed during read')
    return {**before, 'state': {'current': current, 'previous': previous, 'next': following, 'timer': timer,
                              'loaded': loaded, 'reserve': reserve, 'raw_hex': raw.hex()},
            'identity_coherent': True, 'atomic_native_snapshot': False, 'authority_proven': False}

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--pid', required=True, type=int)
    ap.add_argument('--output', required=True, type=Path)
    ap.add_argument('--seconds', default=0., type=float)
    ap.add_argument('--interval-ms', default=100., type=float)
    args = ap.parse_args()
    if not math.isfinite(args.seconds) or not 0 <= args.seconds <= 15 or not math.isfinite(args.interval_ms) or not 20 <= args.interval_ms <= 1000:ap.error('Bounded 0..15 seconds and 20..1000 ms interval')
    report = {'schema': 'fvr.bc2.reload_server_state.v1', 'utc': dt.datetime.now(dt.timezone.utc).isoformat(),
              'pid': args.pid, 'read_only': True, 'native_calls': False, 'process_writes': False,
              'input_or_focus_changes': False, 'authority_proven': False, 'samples': [], 'rejected': []}
    process = None
    try:
        image = Image();process = Process(args.pid);inspect = Inspector(process, image)
        report['executable_sha256'] = hashlib.sha256(image.data).hexdigest()
        binding = discover(image, process);report['binding'] = binding
        deadline = time.perf_counter()+args.seconds
        while True:
            try:report['samples'].append({'monotonic_ns': time.perf_counter_ns(), **capture(inspect, binding)})
            except (OSError, ValueError, KeyError) as exc:report['rejected'].append(str(exc))
            if time.perf_counter() >= deadline:break
            time.sleep(min(args.interval_ms/1000, max(0, deadline-time.perf_counter())))
    except (OSError, ValueError, KeyError, RuntimeError) as exc:report['rejected'].append(str(exc))
    finally:
        if process is not None:process.close()
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False)+'\n', encoding='utf-8')
    print(json.dumps({'samples': len(report['samples']), 'rejected': report['rejected'], 'output': str(args.output)}))
    return 0 if report['samples'] else 1

if __name__ == '__main__':raise SystemExit(main())
