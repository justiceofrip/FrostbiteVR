"""Inventory exact installed firearm configurations from existing pinned metadata.

This is scheduling data. Presence in a registry is recorded separately from
physical mechanics, support, rendering, integration and headset acceptance.
"""
import collections
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[4]
evidence = Path('G:/Unity/VRMMO/bc2vr-recovery')
out = Path(__file__).resolve().parent
classes = {'wcAssault', 'wcSmg', 'wcLmg', 'wcShotgun', 'wcSniper'}
sources = {}
rows = {}

def read(path):
    raw = path.read_bytes()
    sources[str(path)] = hashlib.sha256(raw).hexdigest()
    return json.loads(raw)

def family(asset, cls, fields):
    reload_type = fields.get('FireLogic.ReloadType', {}).get('value')
    if asset == 'MP412': return 'revolver_cylinder'
    if asset == 'Garand': return 'clip_or_exact_configuration_review'
    if cls == 'wcSniper':
        return 'bolt_magazine' if fields.get('FireLogic.FireLogicType', {}).get('value') == 'fltSingleFireWithBoltAction' else 'detachable_magazine'
    if cls == 'wcShotgun' and reload_type == 'rtSingleBullet': return 'tube_pump'
    if cls == 'wcLmg' and asset not in ('MG36', 'XM8 LMG'): return 'belt_feed'
    return 'detachable_magazine'

for name, nested in [('mp-common-all-metadata.json', True), ('sp-common-configurations.json', False)]:
    path = evidence / 'manual-reload-catalog-20261003-candidate' / name
    document = read(path)
    if nested: document = document['configuration']
    for index, weapon in enumerate(document['resolved_weapons']):
        if weapon['weapon_class'] not in classes: continue
        path_key = weapon['resource'].removesuffix('.dbx')
        identity = (weapon['native_name'], weapon['resource_sha256'], weapon['instance_guid'].lower())
        if path_key in rows:
            assert tuple(rows[path_key]['identity']) == identity, path_key
            rows[path_key]['origins'].append({'source': str(path), 'index': index})
            continue
        rows[path_key] = dict(asset=weapon['native_name'], configuration=path_key, identity=identity,
            weapon_class=weapon['weapon_class'], mechanism=family(weapon['native_name'], weapon['weapon_class'], weapon['fields']),
            origins=[{'source': str(path), 'index': index}], two_hand='pending_exact_selector_join',
            manual_reload='pending_complete_runtime_and_geometry_join', source_complete=False,
            installed=False, headset_accepted=False)

# Campaign-only configurations already have exact content receipts in the
# source catalog. Their names must not collapse onto another mesh/configuration.
catalog = read(root / 'config/body-ammo-assets.json')
for entry in catalog['profiles']:
    config = entry.get('configuration_path')
    if not config or config in rows: continue
    if entry['asset'] not in ('Nambu100', 'Garand'): continue
    cfg = entry['authored_configuration']
    rows[config] = dict(asset=entry['asset'], configuration=config,
        identity=(entry['asset'], cfg['sha256'], cfg.get('guid', cfg.get('instance_guid', '')).lower()),
        weapon_class='campaign_exact_configuration',
        mechanism='detachable_magazine' if entry['asset']=='Nambu100' else 'clip_or_exact_configuration_review',
        origins=[{'source':str(root/'config/body-ammo-assets.json'),'configuration':config}],
        two_hand='pending_exact_selector_join', manual_reload='pending_complete_runtime_and_geometry_join',
        source_complete=False, installed=False, headset_accepted=False)

ordered = sorted(rows.values(), key=lambda row:(row['mechanism'], row['asset'], row['configuration']))
counts = collections.Counter(row['mechanism'] for row in ordered)
result = dict(schema='fvr.bc2.firearm_completion_roster.v1', source_pins=sources,
    scope='All exact firearms in the existing MP/SP common catalogs, plus enrolled campaign-only firearm configurations.',
    exclusions=['Launchers, grenades and tools are tracked separately from this firearm roster.',
                'CHAT01/03 recovery remains deferred; external CHAT25/26/32 ownership is unchanged.'],
    summary=dict(configurations=len(ordered), asset_names=len({r['asset'] for r in ordered}), mechanisms=dict(counts)),
    rows=ordered,
    completion_rule='A row closes only after its exact two-hand selector, manual mechanism, native adapter, geometry and required render/cache dependencies are connected. Registry existence alone is insufficient.')
(out/'roster.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps(result['summary']))
