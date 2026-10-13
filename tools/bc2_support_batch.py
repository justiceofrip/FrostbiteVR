"""Compile static support contacts by exact configuration, independently of reloads.

Consumes the existing authored-reference extraction pipeline. A nonconstant or
ambiguous asset is reported once as a data gap, never replaced with another gun's
pose. Explicit primary-hand roles distinguish pistols from BC2's wcSmg label.
"""
import argparse
import hashlib
import json
from pathlib import Path
from bc2_authored_grip_bindings import digest
from bc2_authored_support_bindings import derive, header


def batch(documents, primary_hand_assets=(), preserved_assets=()):
    primary, preserved = set(primary_hand_assets), set(preserved_assets)
    if primary & preserved:
        raise ValueError('A preserved contact cannot be overridden')
    groups = {}
    for document in documents:
        if document.get('schema') != 'fvr.bc2.authored_reload_reference_bindings' or document.get('runtime_admission') is not False:
            raise ValueError('Exact non-admitted reference extraction required')
        for row in document['profiles']:
            if digest({k:v for k,v in row.items() if k != 'binding_digest'}) != row['binding_digest']:
                raise ValueError('Authored reference digest changed')
            groups.setdefault(row['native_asset_name'], {})[row['binding_digest']] = row
    profiles, coverage = [], []
    for asset in sorted(set(groups) | primary | preserved):
        rows = list(groups.get(asset, {}).values())
        item = dict(asset=asset, source_configurations=len(rows), native_reload_admitted=False,
                    aim_mode='primary_hand' if asset in primary else 'two_point')
        if asset in preserved:
            item.update(status='preserved_existing_contact')
        elif not rows:
            item.update(status='missing_authored_reference')
        else:
            doc = dict(schema='fvr.bc2.authored_reload_reference_bindings', runtime_admission=False, profiles=rows)
            try:
                result = derive(doc, {asset}, enable_contact=True,
                                primary_hand_assets={asset} if asset in primary else set())
            except ValueError as exc:
                item.update(status='deferred', reason=str(exc))
            else:
                profiles.extend(result['profiles'])
                item.update(status='static_contact_compiled', contacts=len(result['profiles']),
                            meshes=sorted({p['configured_mesh_path'] for p in result['profiles']}))
        coverage.append(item)
    return dict(schema='fvr.bc2.authored_support_bindings', schema_version=1,
                runtime_admission=False, profiles=profiles, coverage=coverage,
                scope='Measured support contact only. No gun-hand, native reload, pump, bolt, chamber or firing capability admission.')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bindings', action='append', type=Path, required=True)
    p.add_argument('--primary-hand-asset', action='append', default=[])
    p.add_argument('--preserve-asset', action='append', default=[])
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--header', type=Path, required=True)
    a=p.parse_args()
    if a.output.exists() or a.header.exists(): p.error('Use new outputs; preserve prior evidence')
    blobs=[x.read_bytes() for x in a.bindings]
    result=batch([json.loads(x.decode('utf-8-sig')) for x in blobs], a.primary_hand_asset, a.preserve_asset)
    result['inputs']=[dict(path=str(p),sha256=hashlib.sha256(b).hexdigest()) for p,b in zip(a.bindings,blobs)]
    generated=header(result)
    a.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf8')
    a.header.write_text(generated,encoding='utf8')
    print(json.dumps({'contact_profiles':len(result['profiles']),
                      'assets':len({x['native_asset_name'] for x in result['profiles']}),
                      'deferred':[x for x in result['coverage'] if x['status'] not in ('static_contact_compiled','preserved_existing_contact')]}))


if __name__=='__main__':main()
