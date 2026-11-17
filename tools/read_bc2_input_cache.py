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
MAX_DESCRIPTOR_INDEX, MAX_DESCRIPTOR_CHILD_ROWS = 31, 8
DESCRIPTOR_DISPATCH_TARGET = 0x007d3e60
DESCRIPTOR_VIRTUAL_SLOTS = (0xf4, 0xf8, 0x10c)


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


def descriptor_pointer(address, size, label):
    require(0x10000 <= address <= 0x100000000-size and address % 4 == 0,
            label+' pointer bounds/alignment')
    return address


def descriptor_selection(memory, entry):
    """Read only the exact 007d3ec3 selection; caps are diagnostic limits."""
    def select(obj):
        descriptor_pointer(obj, 0x29, 'Descriptor entry')
        table = memory.u32(obj+0x24)
        selector = memory.read(obj+0x28, 1)[0]
        require(selector <= MAX_DESCRIPTOR_INDEX, 'Descriptor selector exceeds observation cap')
        descriptor_pointer(table, 16*(selector+1)+16, 'Descriptor table')
        return table, selector
    table, selector = select(entry)
    base = table+16*selector
    related = memory.read(base+0x1d, 1)[0]
    relation = dict(entry=entry, table=table, selector=selector, related_index=related,
                    related_entry=0, related_table=None, related_selector=None)
    selected = base+0x10
    if related != 0xff:
        require(related <= MAX_DESCRIPTOR_INDEX, 'Related descriptor selector exceeds observation cap')
        address = descriptor_pointer(table, 16*(related+1)+4, 'Related descriptor table')+16*(related+1)
        related_entry = memory.u32(address)
        relation['related_entry'] = related_entry
        if related_entry:
            other_table, other_selector = select(related_entry)
            relation.update(related_table=other_table, related_selector=other_selector)
            selected = other_table+16*(other_selector+1)
    relation['selected_descriptor'] = descriptor_pointer(selected, 16, 'Selected descriptor')
    return relation


def descriptor_row(memory, image, address, child=False, maximum_descendants=None):
    descriptor_pointer(address, 16, 'Descriptor row')
    receiver, reference, flags = (memory.u32(address+offset) for offset in (0, 4, 8))
    descendants = memory.read(address+0xe, 1)[0]
    if maximum_descendants is not None:
        require(descendants <= maximum_descendants, 'Child descriptor subtree exceeds parent extent')
    eligible = bool(flags & 1) and (not child or reference == 0)
    row = dict(address=address, receiver=receiver, reference=reference, flags=flags,
               flattened_descendants=descendants, dispatch_eligible_at_read=eligible,
               targets_observed=False, vtable=None, targets=[])
    if eligible:
        descriptor_pointer(receiver, 4, 'Descriptor receiver')
        table = memory.u32(receiver)
        descriptor_pointer(table, 0x110, 'Descriptor receiver vtable')
        require(not image.section(table-image.base, 0x110)[4] & 0x20000000,
                'Descriptor receiver vtable outside image data')
        targets = []
        for offset in DESCRIPTOR_VIRTUAL_SLOTS:
            target = struct.unpack('<I', exact(memory, image, table+offset, 4))[0]
            head = exact(memory, image, target, 16, True)
            targets.append(dict(offset=offset, target=target, head_hex=head.hex()))
        row.update(targets_observed=True, vtable=table, targets=targets)
    return row


def descriptor_targets(memory, image, entry):
    # Exact immutable PE and live bytes establish these offsets, not a call.
    exact(memory, image, 0x007d3ec3, 0x7a, True)
    exact(memory, image, 0x007bcdc0, 0x1bf, True)
    selection = descriptor_selection(memory, entry)
    root = descriptor_row(memory, image, selection['selected_descriptor'])
    count, children = root['flattened_descendants'], []
    omission = None
    if not root['dispatch_eligible_at_read']:
        omission = 'parent dispatch gate inactive at read'
    elif count > MAX_DESCRIPTOR_CHILD_ROWS:
        omission = 'flattened child count exceeds observation cap'
    else:
        first = descriptor_pointer(root['address']+16, 16*count or 1, 'Descriptor children')
        cursor, end = first, first+16*count
        while cursor < end:
            row = descriptor_row(memory, image, cursor, child=True,
                                 maximum_descendants=(end-cursor)//16-1)
            step = 16*(row['flattened_descendants']+1)
            require(cursor+step <= end, 'Child descriptor subtree exceeds parent extent')
            children.append(row)
            cursor += step
    # Only immediate sibling roots are visited. Descendant contents and actual
    # invocation flags/virtual return values are deliberately not inferred.
    identity = lambda row: {key: row[key] for key in
        ('address', 'receiver', 'reference', 'flattened_descendants', 'targets_observed', 'vtable', 'targets')}
    return dict(selection=selection, root=root, children=children,
        child_omission=omission, recursive_descendants_observed=False,
        invocation_observed=False, lifetime_retirement_proven=False,
        allocation_extent_proven=False,
        identity=dict(selection=selection, root=identity(root), children=[identity(row) for row in children],
                      child_omission=omission))


def observe(memory, image, p, native_source, with_descriptor_targets=False):
    before = resolve(memory, image, p, native_source)
    controlled = reflected(memory, image, before['owner']['controlled'], 0xfc)
    require(controlled['type'] == 'ClientSoldierEntity', 'Controlled/soldier reflection type')
    entry = reflected(memory, image, before['owner']['entry'], 0x154)
    descriptors = None
    if with_descriptor_targets:
        require(entry['target'] == DESCRIPTOR_DISPATCH_TARGET, 'Unsupported descriptor dispatch target')
        descriptors = descriptor_targets(memory, image, before['owner']['entry'])
    after = resolve(memory, image, p, native_source)
    require(before['owner'] == after['owner'], 'Resolve tuple changed around dispatch observation')
    observation = dict(owner=before['owner'], controlled=controlled, entry=entry,
                       cache_snapshots=[before['cache_snapshot'], after['cache_snapshot']])
    if descriptors is not None:
        observation.update(descriptor_snapshot=descriptors, descriptor_identity=descriptors['identity'])
    return observation


def stable_observation(observation):
    # Preserve the owner and reflection comparisons. Cache rotation is recorded
    # separately and cannot veto (or bypass) owner/reflection stability checks.
    keys = ('owner', 'controlled', 'entry')
    if 'descriptor_identity' in observation:
        keys += ('descriptor_identity',)
    return {key: observation[key] for key in keys}


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
    ap.add_argument('--descriptor-targets', action='store_true',
                    help='also observe bounded selected descriptor targets; no native calls or recursive walk')
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
                  descriptor_targets_requested=args.descriptor_targets, descriptor_snapshots=[],
                  descriptor_bounds=dict(maximum_selector=MAX_DESCRIPTOR_INDEX,
                      maximum_flattened_child_rows=MAX_DESCRIPTOR_CHILD_ROWS,
                      recursive_walk=False, scope='diagnostic caps, not native allocation bounds'),
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
        first = observe(memory, image, p, before, args.descriptor_targets)
        result['cache_snapshots'].extend(first['cache_snapshots'])
        if args.descriptor_targets:
            result['descriptor_snapshots'].append(first['descriptor_snapshot'])
        middle = source(provision, args.session_dir)
        same_source(before, middle)
        provision.verify_process(process, middle['identity'], EXPECTED, args.pid)
        second = observe(memory, image, p, middle, args.descriptor_targets)
        result['cache_snapshots'].extend(second['cache_snapshots'])
        if args.descriptor_targets:
            result['descriptor_snapshots'].append(second['descriptor_snapshot'])
        compare_observations(first, second)
        require(provision.digest(Path(before['identity']['executable']).read_bytes()) == EXPECTED, 'PE changed during reads')
        after = source(provision, args.session_dir)
        same_source(before, after)
        provision.verify_process(process, after['identity'], EXPECTED, args.pid)
        memory.check()
        result.update(diagnostic_complete=True, source_after=after, observation=stable_observation(second),
                      equal_newly_resolved_observations=2, executable_sha256=EXPECTED,
                      equal_observations_scope='owner tuple and controlled/entry reflection' +
                          (' plus selected descriptor structural identity' if args.descriptor_targets else ''))
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
