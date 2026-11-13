"""Bounded, read-only selected client pickup -> server pickup diagnostic.

The wire ID is connection scoped and recyclable. Two equal observations do not
grant a native lease, prove absence of ABA, or authorize an inventory operation.
Existing guarded native owner/connection/lifetime validation remains authoritative.
No heap scan, process write, native call, request file or input operation exists.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import time

EXPECTED = "3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258"
SCHEMA = "fvr.bc2.readonly_pickup_identity.v1"
MAX_CONNECTIONS, MAX_BUCKETS, MAX_NODES, MAX_DEPTH = 256, 8192, 4096, 64
CODE = (
    (0x647a10, 0x51, '9cccd7b64ced9ffa0118b27eb85caf249397bc1fea69689490a00a7fe6bed12b'),
    (0x847b10, 0x68, '232f0ff1625a4845c7d54d5337a497fc2f17861aa2c1efe0808bf65b2ae6d409'),
    (0x89dac1, 0x18, 'a3eae3a4cfa38a96c6fd4eb4f89c14d5985f72bf4716db173c5770223c5c87f2'),
    (0x6ad666, 0x19, '58093f43b5c4f2ed2f622dbddf1f8477b17034ac92cd56fe3a3b71d0682d654d'),
    (0x537a70, 0x7, '5f14a1e53514fd7c27be3933204785b9b1ce052c1ebeee506b751d9682dbd353'),
    (0x63a770, 0x7, '5f14a1e53514fd7c27be3933204785b9b1ce052c1ebeee506b751d9682dbd353'),
    (0xc1e270, 0x35, '341a3c4308c267c27e840a3bcf20ac64262e867e1430a132db496a868dfe06a3'),
    (0xc1e0e0, 0x5d, '0cba1696f05d93e638815edade9d4345db27d7f6c2a028735d29ce8f1798f293'),
    (0xc17bb0, 0x46, '10a4fae5bd361d5639973b70b423bd79c43b321e2a29f17ba5b52545839cb0ae'),
    (0xc20fd0, 0x51, 'd6c508c9b9e3226aa4fc4e274b984fe58e8fde34bf3e5fa76955f4ce57ae60cc'),
    (0x642a00, 0x37, '13cac61d452ff3f380e5c98b66880725598b7f69be34ce34d6498fac087448bf'),
    (0x5483ff, 0x10, '883ef3bb2c86d17202fb10bba79348b2a5bde8e8d749c4ab092859f8f42362ef'),
    (0x547bf0, 0x9d, '11c744eb09e76e0f3299d9a720b96e6e8f720ca4a47b853e6cc09d676632eb12'),
    (0x55b830, 0x39, '1bfdc0a259f16d4f3ead5ac7d04ec2f70a5ccbbc68100fd1cd774b2cb5a06812'),
    (0x573750, 0x71, 'ffe2d48666f9619efe442c2b1d6452bc94386a95e7ee526492c79df69def594c'),
    (0x7ff0d3, 0x6, 'fd322358a80194c1d04dd42ef49da4fe4a8efc541f1db9fd718658a24db789a5'),
    (0x911e6f, 0xc, '09bf0325814fc616d4e2910ef75a70a9fa108ed213af7556cc85959c7aea12cb'),
    (0x6ad7d0, 0x4, 'ff27560b0c51bd5f0a15e2e1c9e7f414e6be00542b7b0029d68ecbae0498b2a5'),
    (0x68feb0, 0x6, '16d4619f6eea2da7e278d71e5afcb77da157efd096b2cc2f24639fa125817ac9'),
    (0x877d60, 0x6, 'e19b47d5336bb13f90f8fd2804aa1311972ea9b85406c6003cc9360ec1185b47'),
    (0xc1bbc0, 0x5d, '2ac9ef877bf98e5bd879441142c7934b30b4e270aef247b5f31f03c6d5a0065a'),
    (0xc251e0, 0x15f, 'b0a038fa99f3494746f93d3fcddff80b8f76c05016cad17684f43c5f56de203f'),
    (0xc25f20, 0xa0, '6dc3b39a093c3e49139f91fabbef1a51bb7bcb3e95df5f6e322b8c383ae32f38'),
)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(blob):
    return hashlib.sha256(blob).hexdigest()


def decode_source(blob, metadata):
    """Extend the EXISTING read_source/parse_source result, with an exact byte join."""
    require(len(blob) == 1264 and sha(blob) == metadata['source_sha256'], 'Source byte join changed')
    require(struct.unpack_from('<4I', blob) == (0x31504342, 1, 1264, 0), 'Source wire header')
    client = dict(zip(('player', 'soldier', 'weak', 'weapon', 'actorGeneration', 'equipGeneration', 'space'),
                      struct.unpack_from('<4I3Q', blob, 1144)))
    owner = dict(zip(('session', 'actorGeneration', 'space', 'player', 'actor', 'inventory', 'slot', 'item', 'data',
                     'clientFiring0', 'clientFiring1', 'serverFiring'), struct.unpack_from('<3Q9I', blob, 1184)))
    require(all(client.values()) and owner['session'] == metadata['identity']['session'] and
            owner['actorGeneration'] == client['actorGeneration'] and owner['space'] == client['space'] and
            owner['slot'] <= 1 and all(value for key, value in owner.items() if key != 'slot'),
            'Source owner/generation contract')
    require(not any(blob[1244:]), 'Source reserved/target fields')
    return dict(metadata, client=client, owner=owner)


def read_bound_source(directory, source_reader):
    # Metadata and bytes share the existing bounded CreateFile/no-reparse read.
    metadata, blob = source_reader(directory, include_bytes=True)
    return decode_source(blob, metadata)


def same_source(before, after):
    require(before['identity'] == after['identity'] and before['client'] == after['client'] and
            before['owner'] == after['owner'] and after['source_sequence'] >= before['source_sequence'] and
            after['source_observed_ns'] >= before['source_observed_ns'], 'Source owner/session changed')


class Memory:
    """Hard bounds apply to every read, including borrowed reflection readers."""
    def __init__(self, process, seconds=10.0):
        self.process, self.base = process, process.base
        self.deadline = time.monotonic() + seconds
        self.calls = self.bytes = 0

    def read(self, address, size):
        require(type(address) is int and type(size) is int and 0x10000 <= address and
                0 < size <= 65536 and address + size <= 0x100000000, 'Read address/size bound')
        self.calls += 1
        self.bytes += size
        require(self.calls <= 131072 and self.bytes <= 4 * 1024 * 1024 and
                time.monotonic() <= self.deadline, 'Observation read/time budget')
        blob = self.process.read(address, size)
        require(len(blob) == size, 'Short memory read')
        return blob

    def u32(self, address):
        return struct.unpack('<I', self.read(address, 4))[0]

    def u16(self, address):
        return struct.unpack('<H', self.read(address, 2))[0]

    def equal(self, address, expected, label):
        require(self.u32(address) == expected, label)

    def vector(self, begin, end, maximum, label):
        require(begin >= 0x10000 and begin <= end <= 0x100000000 and
                (end - begin) % 4 == 0 and end - begin <= maximum * 4, label + ' vector bound')
        return list(struct.unpack('<' + 'I' * ((end - begin) // 4), self.read(begin, end - begin))) if end > begin else []


def verify_pe32_header(data):
    require(len(data) >= 0x40 and data[:2] == b'MZ', 'PE DOS header')
    nt = struct.unpack_from('<I', data, 0x3c)[0]
    require(nt + 0x60 <= len(data) and data[nt:nt + 4] == b'PE\0\0' and
            struct.unpack_from('<H', data, nt + 4)[0] == 0x14c and
            struct.unpack_from('<H', data, nt + 24)[0] == 0x10b and
            struct.unpack_from('<I', data, nt + 52)[0] == 0x400000, 'PE32 x86 preferred-image ABI')


def verify_image(image, memory):
    require(image.base == memory.base == 0x400000 and sha(image.data) == EXPECTED, 'Exact executable/base identity')
    verify_pe32_header(image.data)
    for address, size, expected in CODE:
        file_bytes = image.read(address - image.base, size)
        require(len(file_bytes) == size and sha(file_bytes) == expected and
                memory.read(address, size) == file_bytes, 'Native code differs at ' + hex(address))


def current_pair(m, source):
    """Re-read the relationships already used by Bc2PlayerPair/ReloadServer.Scope."""
    c, o = source['client'], source['owner']
    require(m.read(c['player'] + 0xccd, 1)[0] & 8, 'Client player is not local')
    for address, expected in ((c['player'] + 0xc54, c['weak']), (c['weak'], c['soldier'] + 4),
                              (c['player'] + 0xc68, c['soldier']), (c['soldier'] + 0x220, c['player']),
                              (o['player'] + 0xc3c, o['actor']), (o['player'] + 0xc6c, o['actor']),
                              (o['actor'] + 0x220, o['player']), (o['actor'] + 0x2b4, o['inventory']),
                              (o['inventory'] + 0x14c, o['slot']), (o['item'] + 4, o['data']),
                              (c['weapon'] + 4, o['data']), (c['weapon'] + 0x3c, o['clientFiring0']),
                              (c['weapon'] + 0x40, o['clientFiring1']), (o['item'] + 0x10, o['serverFiring'])):
        m.equal(address, expected, 'Current source relationship changed')
    manager = m.u32(0x1566b18 + 8)
    m.equal(manager, 0x1417680, 'Server player manager type')
    capacity, players = m.u32(manager + 4), m.u32(manager + 0x6c)
    player_id = m.u32(c['player'] + 0x154)
    require(0 < capacity <= 256 and player_id < capacity, 'Player index/capacity')
    m.equal(players + player_id * 4, o['player'], 'Indexed current server player')
    m.equal(o['player'] + 0x154, player_id, 'Paired player IDs')
    m.equal(o['actor'], 0x1424850, 'Server soldier exact type')
    begin, end = m.u32(o['actor'] + 0x2b8), m.u32(o['actor'] + 0x2bc)
    items = m.vector(begin, end, 64, 'Server inventory')
    require(o['slot'] < len(items) and items[o['slot']] == o['item'] and items.count(o['item']) == 1,
            'Current server inventory membership')
    flags = m.read(c['soldier'] + 0x114, 1)[0]
    inventory = m.u32(c['soldier'] + (0x24c if flags & 1 else 0x248))
    m.equal(inventory + 0x14c, o['slot'], 'Current client selected slot')
    cb, ce = m.u32(c['soldier'] + 0x260), m.u32(c['soldier'] + 0x264)
    client_items = m.vector(cb, ce, 64, 'Client inventory')
    require(o['slot'] < len(client_items) and client_items[o['slot']] == c['weapon'] and
            client_items.count(c['weapon']) == 1, 'Current client inventory membership')
    return dict(manager=manager, capacity=capacity, players=players, player_id=player_id,
                begin=begin, end=end, items=items, client_inventory=inventory, client_begin=cb,
                client_end=ce, client_items=client_items, client_soldier_flags=flags)


def server_connection(m, player):
    # Original 0x5483ff -> context+0x34 -> 0x642a00, compared by PLAYER pointer.
    manager = m.u32(0x1566b18 + 0x34)
    begin, end = m.u32(manager + 0x208), m.u32(manager + 0x20c)
    connections = m.vector(begin, end, MAX_CONNECTIONS, 'Server connections')
    require(connections and len(set(connections)) == len(connections), 'Empty/duplicate server connections')
    owners = [m.u32(connection + 0x1610) for connection in connections]
    matches = [connection for connection, owner in zip(connections, owners) if owner == player]
    require(len(matches) == 1, 'Missing/ambiguous same-player connection')
    return dict(manager=manager, begin=begin, end=end, connections=connections, players=owners, connection=matches[0])


def map_header(m, manager):
    m.equal(manager, 0x147dff4, 'Network manager exact table')
    capacity, bits = m.u32(manager + 0x240), m.u32(manager + 0x244)
    require(1 <= bits <= 16 and 1 < capacity <= 65536 and capacity <= 1 << bits, 'Network ID capacity/bits')
    return dict(manager=manager, capacity=capacity, bits=bits)


def client_network_id(m, manager, interface):
    header = map_header(m, manager)
    buckets, count, nodes = m.u32(manager + 0x1d0), m.u32(manager + 0x1d4), m.u32(manager + 0x1d8)
    require(0 < count <= MAX_BUCKETS and 0 < nodes <= MAX_NODES, 'Client hash count bound')
    bucket_values = m.vector(buckets, buckets + (count + 1) * 4, MAX_BUCKETS + 1, 'Client hash buckets')
    sentinel = bucket_values[-1]
    seen, keys, matches, records = set(), set(), [], []
    for index, node in enumerate(bucket_values[:-1]):
        while node:
            require(node != sentinel and node not in seen and len(seen) < nodes, 'Client hash cycle/count')
            seen.add(node)
            key, record, following = struct.unpack('<3I', m.read(node, 12))
            require(0 < key < header['capacity'] and key % count == index and key not in keys,
                    'Client hash key/bucket/duplicate')
            keys.add(key)
            require(m.u16(record + 4) == key, 'Client record network ID differs')
            target = m.u32(record + 8)
            records.append(dict(node=node, key=key, record=record, interface=target, following=following))
            if target == interface:
                matches.append(records[-1])
            node = following
    require(len(seen) == nodes and len(matches) == 1, 'Missing/ambiguous selected network identity')
    return dict(header, buckets=buckets, bucket_count=count, node_count=nodes, sentinel=sentinel,
                bucket_values=bucket_values, records=records, selected=matches[0])


def server_network_object(m, manager, network_id):
    header = map_header(m, manager)
    require(0 < network_id < header['capacity'], 'Server network ID bound')
    count, node = m.u32(manager + 0x148), m.u32(manager + 0x140)
    sentinel = manager + 0x138
    require(0 < count <= MAX_NODES, 'Server tree count bound')
    lower, upper, seen, path = -1, header['capacity'], set(), []
    while node:
        require(node != sentinel and node not in seen and len(seen) < min(count, MAX_DEPTH),
                'Server tree cycle/depth/count')
        seen.add(node)
        raw = m.read(node, 24)
        right, left, parent, colour, key, record = struct.unpack('<6I', raw)
        require(lower < key < upper, 'Server tree key ordering')
        path.append(dict(node=node, right=right, left=left, parent=parent, colour=colour, key=key, record=record))
        if key == network_id:
            require(m.u16(record + 4) == key, 'Server record network ID differs')
            interface = m.u32(record + 8)
            m.equal(interface, 0x1424330, 'Server pickup network interface type')
            m.equal(0x1424330 + 0x1c, 0x6ad7d0, 'Server pickup conversion slot')
            pickup = interface - 0x50
            m.equal(pickup, 0x1424358, 'ServerPickupEntity primary type')
            m.equal(0x1424358 + 8, 0x68feb0, 'ServerPickupEntity reflected getter')
            m.equal(record + 0xc, m.u32(interface + 0x30), 'Server record reverse identity')
            return dict(header, node_count=count, sentinel=sentinel, path=path, record=record,
                        interface=interface, pickup=pickup, network_id=network_id)
        if key < network_id:
            lower, node = key, right
        else:
            upper, node = key, left
    raise ValueError('Selected network ID absent from server tree')


def server_catalog(m, reflection, pickup):
    identity = reflection.reflected(pickup, 'ServerPickupEntity')
    require(identity['table'] == 0x1424358 and identity['typeInfo'] == 0x1bf2710, 'Server pickup reflected type')
    begin, end = m.u32(pickup + 0x34), m.u32(pickup + 0x38)
    require(begin >= 0x10000 and begin < end and end - begin <= 128 * 32 and (end - begin) % 32 == 0,
            'Server catalog range bound')
    raw = m.read(begin, end - begin)
    rows = []
    for index in range((end - begin) // 32):
        data = struct.unpack_from('<I', raw, index * 32)[0]
        linked = struct.unpack_from('<i', raw, index * 32 + 12)[0]
        require(-1 <= linked < (end - begin) // 32, 'Server catalog linked index bound')
        info = reflection.reflected(data, 'SoldierWeaponData')
        persistent = m.u32(data + 0x64)
        persistence_info = reflection.reflected(persistent, 'PersistentWeapon')
        rows.append(dict(index=index, weaponData=data, linkedSlot=linked,
                         name=reflection.text(m.u32(data + 12), 64), path=reflection.text(m.u32(data + 0x40), 192),
                         persistence=persistent, persistenceId=reflection.text(m.u32(persistent + 0x40), 96),
                         reflected=info, persistenceReflected=persistence_info))
    return dict(pickup=pickup, begin=begin, end=end, count=len(rows), rows=rows, row_bytes_sha256=sha(raw))


def observe(m, reflection, source):
    pair = current_pair(m, source)
    selected = reflection.owner()
    pickup = selected.get('pickup', 0)
    require(pickup >= 0x10000, 'No current selected client pickup')
    m.equal(pickup, 0x1436040, 'ClientPickupEntity primary type')
    m.equal(pickup + 0x50, 0x143601c, 'ClientPickupEntity network interface type')
    client = m.u32(0x1571430)
    connection = server_connection(m, source['owner']['player'])
    sent = client_network_id(m, client + 0x12cc, pickup + 0x50)
    received = server_network_object(m, connection['connection'] + 0x12cc, sent['selected']['key'])
    require(sent['bits'] == received['bits'], 'Peer network-ID bit widths differ')
    catalog = server_catalog(m, reflection, received['pickup'])
    require(reflection.owner() == selected and current_pair(m, source) == pair and
            server_connection(m, source['owner']['player']) == connection and m.u32(0x1571430) == client,
            'Owner/selection/connection changed during observation')
    return dict(pair=pair, selected=selected, client_connection=client, server_connection=connection,
                client_map=sent, server_map=received, server_catalog=catalog)


def collect(process, image, reader_factory, read_source, verify_process, seconds=10.0):
    result = dict(schema=SCHEMA, executable_sha256=EXPECTED, read_only=True, process_access=0x410,
                  native_functions_called=False, native_requests_queued=False, native_inventory_verified=False,
                  selection_admitted=False, native_authority=False, diagnostic_complete=False,
                  external_reads_are_not_atomic=True, unobserved_id_reuse_excluded=False,
                  limits='Two declared bounded observations; connection-scoped recyclable ID; native guarded owner/connection/lifetime revalidation required.')
    first = read_source()
    result['source_before'] = first
    observations = []
    for number in (1, 2):
        current = read_source()
        same_source(first, current)
        verify_process(process, current['identity'], EXPECTED, current['identity']['pid'])
        memory = Memory(process, seconds)
        verify_image(image, memory)
        reflection = reader_factory(memory, image)
        reflection.verify()
        observation = observe(memory, reflection, current)
        verify_image(image, memory)
        reflection.verify()
        after = read_source()
        same_source(current, after)
        verify_process(process, after['identity'], EXPECTED, after['identity']['pid'])
        observations.append(observation)
        result.setdefault('passes', []).append(dict(number=number, read_calls=memory.calls,
                                                   read_bytes=memory.bytes, completed=True))
        result['source_after'] = after
    require(observations[0] == observations[1], 'Declared identity/catalog observations differ')
    result.update(diagnostic_complete=True, observation=observations[1], equal_declared_observations=2)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--source-directory', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=10.0, help='Per-observation hard time budget, 1..30 seconds')
    args = parser.parse_args()
    require(args.pid > 0 and 1 <= args.seconds <= 30, 'PID/time bounds')
    require(not args.output.exists(), 'Output must be new')
    from read_bc2_provision_target import load_dependencies, read_source, verify_process
    manifest, _, modules = load_dependencies(args.manifest)
    source_reader = lambda: read_bound_source(args.source_directory, read_source)
    source = source_reader()
    require(source['identity']['pid'] == args.pid, 'Requested PID differs from native source')
    flynn = modules['read_flynn_catalog']
    image = flynn.Image(Path(source['identity']['executable']))
    require(sha(image.data) == EXPECTED, 'Unsupported executable')
    process = flynn.Process(args.pid, Path(source['identity']['executable']))
    try:
        try:
            result = collect(process, image, flynn.Reader, source_reader, verify_process, args.seconds)
        except (OSError, ValueError) as error:
            result = dict(schema=SCHEMA, executable_sha256=EXPECTED, read_only=True, process_access=0x410,
                          native_authority=False, diagnostic_complete=False, native_functions_called=False,
                          native_requests_queued=False, selection_admitted=False, source_before=source,
                          external_reads_are_not_atomic=True, unobserved_id_reuse_excluded=False,
                          failure=str(error))
    finally:
        process.close()
    result['dependency_manifest_sha256'] = sha(args.manifest.read_bytes())
    result['dependencies'] = manifest['files']
    with args.output.open('x', encoding='utf-8', newline='\n') as stream:
        stream.write(json.dumps(result, indent=2) + '\n')
    print(json.dumps(dict(schema=SCHEMA, diagnostic_complete=result['diagnostic_complete'],
                          failure=result.get('failure'), output=str(args.output), native_authority=False)))
    return 0 if result['diagnostic_complete'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
