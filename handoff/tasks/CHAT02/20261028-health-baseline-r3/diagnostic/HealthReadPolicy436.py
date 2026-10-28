"""Read-only field proof policy; never invokes or follows the damage hook."""
import hashlib
import json

PROOF_SHA256 = 'b7b64b1a796697e824cdad18d0e7e9243093359fd62791d7dea8aada4acb75a9'
EXE_SHA256 = '3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258'
LEGACY_STATIC_PREFIXES = ('settings_', 'server_settings_', 'godmode_', 'vo_',
                          'message_kind_', 'termination_message_')


def load_proof(raw):
    if len(raw) > 256 * 1024 or hashlib.sha256(raw).hexdigest() != PROOF_SHA256:
        raise ValueError('Offline health proof changed')
    proof = json.loads(raw)
    if proof['executable_sha256'] != EXE_SHA256:
        raise ValueError('Offline health executable identity differs')
    return proof


def verify_spans(proof, image, read):
    # Preserve ALL 27 original offline spans, including the damage consumer.
    # Only the field-reading contract must also remain pristine in memory.
    # +0x2c embedded interface + getter's Float32 [ecx+8] proves +0x34.
    # The damage function is not called/read by snapshot() and the ordinary
    # selected-knife adapter legitimately hooks it. Its original disk semantics
    # are evidence, never a claim that its current runtime body is unmodified.
    spans = {**proof['code_spans'], **proof['data_spans']}
    disk_verified, live_verified = [], []
    for name, item in spans.items():
        expected = bytes.fromhex(item['bytes_hex'])
        if image.read(item['address'] - image.base, item['size']) != expected:
            raise ValueError('Offline native proof mismatch: ' + name)
        disk_verified.append(name)
    for name, item in spans.items():
        if name.startswith(LEGACY_STATIC_PREFIXES) or name == 'server_damage':
            continue
        if read(item['address'], item['size']) != bytes.fromhex(item['bytes_hex']):
            raise ValueError('Live health-read proof mismatch: ' + name)
        live_verified.append(name)
    damage = spans['server_damage']
    return dict(
        schema='fvr.private.health-read-code-policy436.v1',
        disk_verified_native_spans=disk_verified,
        verified_native_spans=live_verified,
        static_only_damage_consumer=dict(name='server_damage', address=damage['address'],
            size=damage['size'], sha256=damage['sha256'],
            reason='Not used for field access; original bytes remain disk-proven. Current knife adapter hooks this function.'),
        native_damage_consumer_live_verified=False,
        damage_hook_followed=False,
        health_field_offset=0x34,
        live_read_basis='Exact embedded tables and Float32 getter, constructors, type metadata; unchanged repeated current-owner snapshot checks',
        actual_death=False,
        independent_presentation_required=True)
