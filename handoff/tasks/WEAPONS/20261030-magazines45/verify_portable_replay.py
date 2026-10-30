"""Root-only replay of the 23 new policy records, never the prior 204 cache."""
import argparse
import json
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'tools'))
import bc2_firearm_magazine_batch as batch
import bc2_configured_body_assets as body
from bc2_asset_resource_identity import profiles_match


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--game',type=Path,required=True);p.add_argument('--catalog',type=Path,required=True);p.add_argument('--receipt',type=Path,required=True)
    a=p.parse_args();catalog=json.loads(a.catalog.read_text())
    rows=[r for r in catalog['profiles'] if r.get('mod_authored_magazine') or r.get('mod_authored_display')]
    batch.require(len(rows)==23,'Expected exact final authored policy subset')
    # Do not let verification accidentally consult a preparation/report path.
    original=batch.read_pin
    def prohibited(_):raise ValueError('Portable replay attempted a private provenance read')
    batch.read_pin=prohibited
    try:actual,payload=body.derive_catalog(a.game,rows,resource_content=True)
    finally:batch.read_pin=original
    batch.require(profiles_match(actual,rows,True),'Portable actual catalog changed')
    result=dict(schema='fvr.bc2.magazines45.portable_replay_validation.v1',passed=True,catalog_pin=batch.pin(a.catalog),
                magazines=sum(bool(r.get('mod_authored_magazine')) for r in rows),displays=sum(bool(r.get('mod_authored_display')) for r in rows),
                rows=len(rows),cache_bytes=len(payload),cache_sha256=batch.hashlib.sha256(payload).hexdigest(),
                private_report_reads=False,full204_cache_reextracted=False,installed_content_replayed=True,native_execution=False,headset_acceptance=False)
    batch.write(a.receipt,result);print(json.dumps(result))


if __name__=='__main__':main()
