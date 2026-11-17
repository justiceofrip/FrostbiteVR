"""ROOT-only single read-only on-foot entry identity and input-cache observation.

Run only inside a freshly StorageBudget-admitted ROOT session. No launch, input,
native calls, process writes, pointer arguments, retry loop or authority grant.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import struct
import time

EXPECTED = '3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258'
MAX_BYTES, MAX_SECONDS = 65536, 5
MAX_SCALAR_READS, MAX_SCALAR_BYTES = 512, 16
ENTRY_CACHE_GETTER = 0x007ed760
ENTRY_CACHE_GETTER_BYTES = bytes.fromhex('8b81700c0000c3')
EXPECTED_INPUT_CACHE_VTABLE = 0x01419350


class ScalarReadJournal:
    """Record only already-returned scalar bytes, before caller validation."""

    def __init__(self, process, journal):
        self.process, self.base, self.journal = process, process.base, journal

    def read(self, address, size):
        blob = self.process.read(address, size)
        if 0 < size <= MAX_SCALAR_BYTES:
            if len(self.journal['entries']) < MAX_SCALAR_READS:
                # Pinned Process.read returns the requested length or raises.
                # Bound diagnostic bytes even for a malformed test reader.
                observed = blob[:MAX_SCALAR_BYTES]
                self.journal['entries'].append(dict(address=address,
                    length=len(observed), hex=observed.hex()))
            else:
                self.journal['truncated'] = True
        return blob


def require(ok, message):
    if not ok:
        raise ValueError(message)


def source(provision, directory):
    meta, blob = provision.read_source(directory, include_bytes=True)
    require(provision.digest(blob) == meta['source_sha256'], 'Source bytes changed')
    names = ('player', 'soldier', 'weak', 'weapon', 'actorGeneration', 'equipGeneration', 'space')
    meta['client'] = dict(zip(names, struct.unpack_from('<4I3Q', blob, 1144)))
    require(all(meta['client'].values()), 'Source owner incomplete')
    age = time.perf_counter_ns() - meta['source_observed_ns']
    require(0 <= age <= 200000000, 'Source older than 200ms or future dated')
    return meta


def same_source(a, b):
    require(a['identity'] == b['identity'] and a['client'] == b['client'] and
            b['source_sequence'] >= a['source_sequence'] and
            b['source_observed_ns'] >= a['source_observed_ns'], 'Source owner/session changed')


def profile(image):
    # These signatures/operands are from current Bc2Profile.cpp and the existing
    # capture_reload_state Inspector, on the exact SHA256 above.
    specs = {
        'context': ('B8 01 00 00 00 84 05 ?? ?? ?? ?? 75 66 09 05 ?? ?? ?? ?? 50 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 33 C0 68 ?? ?? ?? ?? C7 05', 0x79),
        'manager': ('51 53 55 8B 6C 24 10 56 57 55 8B F1 E8 ?? ?? ?? ?? 8D 4E 10 E8 ?? ?? ?? ?? 8D 7E 4C 8B CF E8 ?? ?? ?? ?? 8D 4F 14 33 DB', 0x74),
        'controlled': ('8B 81 68 0C 00 00 C3 CC CC CC CC CC CC CC CC CC 8B C1 C7 00 ?? ?? ?? ?? C3', 7),
        'gather': ('83 EC 14 53 55 56 8B 74 24 24 57 8B D9 33 ED 90 8B 4B 18 33 D2 8B C5 F7 F1 8B 7B 14 8B 04 97 85 C0 8D 14 97', 0x232),
        'cache_ctor': ('56 8B F1 68 BC 00 00 00 8D 46 08 6A 00 50 C7 06 ?? ?? ?? ?? E8 ?? ?? ?? ?? F3 0F 10 05 ?? ?? ?? ?? 83 C4 0C F3 0F 11 86 B0 00 00 00 C7 86 C4 00 00 00 FF FF FF FF C6 86 C8 00 00 00 01 8B C6 5E C3', 0x41),
    }
    found = {name: (image.find(pattern), size) for name, (pattern, size) in specs.items()}
    # Exact PE body MOV EAX,[ECX+0xc70]; RET.
    # This is a byte identity check; no function is called and no ABI is guessed.
    getter_rva = ENTRY_CACHE_GETTER-image.base
    require(image.read(getter_rva, 7) == ENTRY_CACHE_GETTER_BYTES, 'Entry cache getter PE bytes')
    found['entry_cache_getter'] = (getter_rva, 7)
    word = lambda name, offset: struct.unpack('<I', image.read(found[name][0] + offset, 4))[0]
    gather = image.base + found['gather'][0]
    tables = []
    for rva, _, raw, rawsize, flags in image.sections:
        if flags & 0xa0000000:
            continue
        for pos in range(12, rawsize - 7, 4):
            if struct.unpack_from('<I', image.data, raw + pos)[0] != gather:
                continue
            words = struct.unpack_from('<5I', image.data, raw + pos - 12)
            try:
                valid = all(image.section(value-image.base)[4] & 0x20000000 for value in words)
            except ValueError:
                valid = False
            if valid:
                tables.append(image.base + rva + pos - 12)
    require(len(tables) == 1, 'Unique router vtable required')
    require(word('cache_ctor', 16) == EXPECTED_INPUT_CACHE_VTABLE, 'Exact input-cache vtable')
    return dict(context=word('context', 21), manager_table=word('manager', 0x6f),
                controlled_getter=image.base+found['controlled'][0],
                router_table=tables[0], cache_table=word('cache_ctor', 16), signatures=found)


def exact(memory, image, address, size, executable=False):
    section = image.section(address-image.base, size)
    require(not executable or section[4] & 0x20000000, 'Target outside executable section')
    blob = memory.read(address, size)
    require(blob == image.read(address-image.base, size), 'Live PE bytes differ at ' + hex(address))
    return blob


def reflected(memory, image, obj, dispatch_offset):
    vt = memory.u32(obj)
    getter = struct.unpack('<I', exact(memory, image, vt+8, 4))[0]
    code = exact(memory, image, getter, 6, True)
    require(code[0] == 0xb8 and code[5] == 0xc3, 'Unsupported reflection getter')
    info = struct.unpack_from('<I', code, 1)[0]
    # TypeInfo links are initialized runtime data, not immutable PE words.
    # Match Bc2Gameplay.cpp Type: bound each live link to the image; never call it.
    image.section(info-image.base, 8)
    metadata = memory.u32(info+4)
    image.section(metadata-image.base, 4)
    name_at = memory.u32(metadata)
    name = bytearray()
    for index in range(64):
        char = exact(memory, image, name_at+index, 1)[0]
        if not char:
            break
        require(32 <= char < 127, 'Invalid reflected name')
        name.append(char)
    else:
        raise ValueError('Reflected name exceeds 63 bytes')
    target = struct.unpack('<I', exact(memory, image, vt+dispatch_offset, 4))[0]
    head = exact(memory, image, target, 16, True)
    return dict(vtable=vt, getter=getter, type_info=info, metadata=metadata,
                type=name.decode('ascii'), dispatch_offset=dispatch_offset,
                target=target, target_head_hex=head.hex())


def resolve(memory, image, p, native_source):
    # Start anew at context on every invocation; no retained object is an input.
    manager = memory.u32(p['context']+8)
    require(memory.u32(manager) == p['manager_table'], 'Manager vtable')
    player = memory.u32(manager+0xb4)
    player_vt = memory.u32(player)
    require(struct.unpack('<I', exact(memory, image, player_vt+0x38, 4))[0] == p['controlled_getter'], 'Player getter')
    require(memory.read(player+0xccd, 1)[0] & 8, 'Not local player')
    weak = memory.u32(player+0xc54)
    target = memory.u32(weak)
    require(target >= 4, 'Weak target')
    soldier = target-4
    controlled = memory.u32(player+0xc68)
    attached = memory.u32(player+0xc60)
    require(controlled == soldier and not attached, 'Not plain on-foot Resolve route')
    require(memory.u32(soldier+0x220) == player, 'Soldier owner')
    require((player, soldier, weak) == tuple(native_source['client'][k] for k in ('player', 'soldier', 'weak')), 'Fresh source/Resolve owner differs')
    slot = memory.u32(player+0xc6c)
    begin, end = memory.u32(controlled+0x7c), memory.u32(controlled+0x80)
    require(begin and 0 <= end-begin <= 256 and not (end-begin)%4 and slot < (end-begin)//4, 'Entry bounds')
    entry = memory.u32(begin+4*slot)
    router, cache = memory.u32(entry+0x198), memory.u32(player+0xc74)
    router_vtable, cache_vtable = memory.u32(router), memory.u32(cache)
    require(router_vtable == p['router_table'] and cache_vtable == p['cache_table'], 'Router/cache vtables')
    entry_player = memory.u32(entry+0x1c4)
    require(0x10000 <= entry_player <= 0x100000000-0xc70-4, 'Entry player pointer bounds')
    require(entry_player == player, 'Entry player differs from current owner')
    dispatch_cache_pair = memory.u32(entry_player+0xc70)
    require(0x10000 <= dispatch_cache_pair <= 0x100000000-0x1a4-4, 'Dispatch cache pair pointer bounds')
    dispatch_vtable = memory.u32(dispatch_cache_pair)
    require(dispatch_vtable == p['cache_table'],
            'Dispatch cache pair first-cache vtable: pointer=0x%08x actual=0x%08x expected_cache=0x%08x' %
            (dispatch_cache_pair, dispatch_vtable, p['cache_table']))
    # 00623410 constructs this pair with inline InputCaches at
    # +0/+d0, then binds +1a0/+1a4; 006234b0 advances that structural pair.
    # The first InputCache vtable alone does not prove a pair or its lifetime.
    # Keep entry+198 as the unrelated Gather router and original owner identity.
    # Each scalar is a bounded, typed read; these external reads are not atomic.
    current, previous = memory.u32(dispatch_cache_pair+0x1a0), memory.u32(dispatch_cache_pair+0x1a4)
    for label, pointer in (('current', current), ('previous', previous)):
        require(0x10000 <= pointer <= 0x100000000-4, 'Pair %s cache pointer bounds' % label)
        require(memory.u32(pointer) == p['cache_table'], 'Pair %s cache vtable' % label)
    require(memory.u32(manager+0xb4) == player and memory.u32(weak) == target, 'Owner changed within Resolve')
    # Keep the full owner tuple, including player+0xc74, for stability.
    owner = dict(manager=manager, player=player, weak=weak, soldier=soldier, controlled=controlled,
                 attached=attached, slot=slot, begin=begin, end=end, entry=entry, router=router, cache=cache)
    snapshot = dict(entry_player=entry_player, entry_router=router, entry_router_vtable=router_vtable,
                    dispatch_cache_pair=dispatch_cache_pair, dispatch_cache_pair_vtable=dispatch_vtable,
                    player_cache=cache, player_cache_vtable=cache_vtable,
                    pair_current_cache=current, pair_previous_cache=previous,
                    dispatch_cache_pair_equals_player_cache=dispatch_cache_pair == cache,
                    dispatch_cache_pair_equals_pair_current_cache=dispatch_cache_pair == current,
                    dispatch_cache_pair_equals_pair_previous_cache=dispatch_cache_pair == previous,
                    player_cache_equals_pair_current_cache=cache == current,
                    player_cache_equals_pair_previous_cache=cache == previous)
    return dict(owner=owner, cache_snapshot=snapshot)


def observe(memory, image, p, native_source):
    before = resolve(memory, image, p, native_source)
    controlled = reflected(memory, image, before['owner']['controlled'], 0xfc)
    require(controlled['type'] == 'ClientSoldierEntity', 'Controlled/soldier reflection type')
    entry = reflected(memory, image, before['owner']['entry'], 0x154)
    after = resolve(memory, image, p, native_source)
    require(before['owner'] == after['owner'], 'Resolve tuple changed around dispatch observation')
    return dict(owner=before['owner'], controlled=controlled, entry=entry,
                cache_snapshots=[before['cache_snapshot'], after['cache_snapshot']])


def stable_observation(observation):
    # Preserve the owner and reflection comparisons. Cache rotation is recorded
    # separately and cannot veto (or bypass) owner/reflection stability checks.
    return {key: observation[key] for key in ('owner', 'controlled', 'entry')}


def compare_observations(first, second):
    require(stable_observation(first) == stable_observation(second), 'Two newly resolved observations differ')


def cache_variation(snapshots):
    return sorted(key for key in snapshots[0]
                  if any(snapshot[key] != snapshots[0][key] for snapshot in snapshots[1:]))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--pid', type=int, required=True)
    ap.add_argument('--session-dir', type=Path, required=True)
    ap.add_argument('--pins', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args(argv)
    require(0 < args.pid <= 0xffffffff, 'PID bounds')
    path = Path(__file__).resolve().parent / 'read_bc2_provision_target.py'
    spec = importlib.util.spec_from_file_location('input_cache_provision', path)
    provision = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(provision)
    provision.no_link_ancestors(args.output)
    require(not args.output.exists() and not args.output.with_name(args.output.name+'.pending').exists(), 'Output must be new')
    manifest, _, modules = provision.load_dependencies(args.pins)
    result = dict(schema='fvr.bc2.input-cache-observation.v1', read_only=True, process_access=0x410,
                  diagnostic_complete=False, native_authority=False, native_functions_called=False,
                  external_reads_are_not_atomic=True, unobserved_reuse_excluded=False,
                  cache_alias_scope='individual non-atomic read-time scalar comparisons only; no lifetime, ownership or cache-generation proof',
                  dispatch_cache_pair_layout_scope='player+c70 receiver; first inline InputCache shares pair address; vtable alone proves neither pair layout nor lifetime; producer/advance receiver equality and issued-copy last use remain unproved',
                  entry_cache_getter=dict(address=ENTRY_CACHE_GETTER, bytes_hex=ENTRY_CACHE_GETTER_BYTES.hex()),
                  expected_input_cache_vtable=EXPECTED_INPUT_CACHE_VTABLE,
                  cache_snapshots=[],
                  scalar_read_journal=dict(entries=[], truncated=False,
                      max_entries=MAX_SCALAR_READS, max_entry_bytes=MAX_SCALAR_BYTES,
                      scope='returned reads requested at 1..16 bytes, in read order; larger reads omitted; no lifetime or authority proof'),
                  bounds=dict(process_read_bytes=MAX_BYTES, process_read_seconds=MAX_SECONDS,
                              passes=2, output_bytes=131072),
                  dependency_manifest_sha256=provision.digest(args.pins.read_bytes()),
                  helper_sha256=provision.digest(Path(__file__).read_bytes()),
                  provision_api_sha256=provision.digest(path.read_bytes()))
    process = memory = None
    try:
        initial = provision.read_source(args.session_dir)
        require(initial['identity']['pid'] == args.pid and initial['identity']['executable_sha256'] == EXPECTED, 'Initial PID/hash')
        image = modules['capture_reload_state'].Image(initial['identity']['executable'])
        require(provision.digest(image.data) == EXPECTED and image.base == 0x400000, 'Exact PE SHA256/base required')
        p = profile(image)
        before = source(provision, args.session_dir)
        require(before['identity'] == initial['identity'], 'Source identity changed while reading PE')
        result['source_before'] = before
        process = modules['read_bc2'].Process(args.pid, before['identity']['executable'])
        provision.verify_process(process, before['identity'], EXPECTED, args.pid)
        require(process.base == image.base, 'Relocated image refused')
        memory = provision.BudgetedProcess(
            ScalarReadJournal(process, result['scalar_read_journal']), None, MAX_BYTES, MAX_SECONDS)
        # The existing input-gather hook can change this unrelated function's
        # entry bytes. It is used ONLY for static router-table discovery here;
        # no gather invocation or gather-hook ownership claim is made.
        result['gather_code_verification'] = 'exact_PE_static_only; live code not inspected; existing hook ownership not asserted'
        for name, (rva, size) in p['signatures'].items():
            if name == 'gather':
                continue
            exact(memory, image, image.base+rva, size, True)
        first = observe(memory, image, p, before)
        result['cache_snapshots'].extend(first['cache_snapshots'])
        middle = source(provision, args.session_dir)
        same_source(before, middle)
        provision.verify_process(process, middle['identity'], EXPECTED, args.pid)
        second = observe(memory, image, p, middle)
        result['cache_snapshots'].extend(second['cache_snapshots'])
        compare_observations(first, second)
        require(provision.digest(Path(before['identity']['executable']).read_bytes()) == EXPECTED, 'PE changed during reads')
        after = source(provision, args.session_dir)
        same_source(before, after)
        provision.verify_process(process, after['identity'], EXPECTED, args.pid)
        memory.check()
        result.update(diagnostic_complete=True, source_after=after, observation=stable_observation(second),
                      equal_newly_resolved_observations=2, executable_sha256=EXPECTED,
                      equal_observations_scope='owner tuple and controlled/entry reflection')
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        result['refusal'] = str(error)[:512]
    finally:
        varied = cache_variation(result['cache_snapshots']) if result['cache_snapshots'] else []
        result['cache_snapshot_variation'] = bool(varied)
        result['varying_cache_snapshot_fields'] = varied
        if process is not None:
            process.close()
        if memory is not None:
            result['reads'] = dict(bytes=memory.bytes_read, calls=memory.read_calls,
                                   elapsed_seconds=time.monotonic()-memory.started,
                                   failure=memory.read_failure)
    provision.publish(args.output, result)
    print(json.dumps(dict(diagnostic_complete=result['diagnostic_complete'],
                         refusal=result.get('refusal'), output=str(args.output))))
    return 0 if result['diagnostic_complete'] else 3


if __name__ == '__main__':
    raise SystemExit(main())
