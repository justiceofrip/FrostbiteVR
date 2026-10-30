"""Reproduce an exact magazine-role preview using the existing bounded mesh renderer."""
import copy
import hashlib
import importlib.util
import inspect
import json
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[3]
script = root / 'handoff/tasks/CHAT16/20261030-batch37/prepare_pistols.py'
spec = importlib.util.spec_from_file_location('pistol_preview_source', script)
module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
plan = json.loads((root / 'handoff/tasks/CHAT14-16/20261030-familybatch36/input-plan.json').read_text())
pin = plan['sources']['actions']; raw = (Path(plan['evidence_root']) / pin['path']).read_bytes()
assert hashlib.sha256(raw).hexdigest() == pin['sha256']
data = json.loads(raw)
config = next(c for c in plan['configurations'] if c['asset'] == 'M1911')
row = data
for key in config['authored_anchor']['pointer'].strip('/').split('/'):
    row = row[int(key)] if isinstance(row, list) else row[key]
bindings = json.loads((root / 'reports/coordinator-20261030-feedbatch38/reference-bindings.json').read_text())
binding = next(p for p in bindings['profiles'] if p['native_asset_name'] == 'M1911')
part = next(p for p in row['parts'] if p['bone'] == 'jntWpn_12')
rail = copy.deepcopy(next(p for p in row['linear_candidates'] if p['part'] == 'jntWpn_3'))
rail.update(part=part['bone'], root=part['parent'], part_bounds_local=part['bounds_m'])
rail['decoded_travel']['maximum_time_seconds'] = part['peak_time_s']
game = Path('G:/New folder (2)/Battlefield Bad Company 2')
configured = json.loads((root / 'reports/coordinator-20261030-feedbatch38/configured-catalog.json').read_text())
pool = module.ArchivePool(game, configured['archive'], configured['index_sha256'])
# This is a diagnostic view, not an edit to the measured slide sampler. Only
# framing and labels differ from its already reviewed rendering function.
source = inspect.getsource(module.preview)
source = source.replace("if owner != 'jntWpn_12'", "if True")
source = source.replace("CYAN JNTWPN_3 INDEX30 - PARENT JNTWPN_1 INDEX10", "CYAN JNTWPN_12 INDEX38 - MAGAZINE")
source = source.replace('MAXIMUM RETRACTION', 'MAGAZINE REMOVED')
source = source.replace('Cyan candidate slide, grey', 'Cyan magazine root, grey')
scope = dict(module.__dict__)
scope['PREVIEWS'] = root / 'reports/coordinator-20261030-pipeline34/magazine-role-review'
exec(compile(source, str(__file__) + ':scoped-preview', 'exec'), scope)
render = scope['preview']
mesh = next(m for m in binding['meshes'] if m['configured_mesh_path'] == row['configured_mesh'])
lod = next(l for l in mesh['geometry']['lods'] if l['lod'] == 0)
def load(src, name, expected):
    archive = pool(game / src['archive']); assert archive.index_sha256 == src['archive_index_sha256']
    value = archive.read_selected([name])[name]
    assert hashlib.sha256(value).hexdigest() == expected
    return value
mesh_raw = load(row['source']['mesh_source'], mesh['mesh_resource'], row['source']['mesh_sha256'])
lod_raw = load(row['source']['mesh_source'], lod['data_resource'], row['source']['lod_sha256'])
skeleton = module.Skeleton(load(binding['skeleton_source'], binding['animation_chain']['skeleton_resource'], row['skeleton_sha256']))
clip = module.Clip(load(row['source'], row['source']['resource'], rail['clip_sha256']))
reference = module.Clip(load(binding['animation_source'], binding['animation_chain']['animation_resource'], rail['reference_clip_sha256']))
output = scope['PREVIEWS'] / 'preview-receipt.json'
if '--review-only' in sys.argv:
    receipt = json.loads(output.read_text())
    assert hashlib.sha256((Path(plan['evidence_root']) / receipt['path']).read_bytes()).hexdigest() == receipt['sha256']
else:
    receipt = render(lod_raw, module.metadata(mesh_raw)[0], skeleton, clip, reference, row, rail)
    receipt.update(direct_child_leaf=False, assembly_root=True, zero_triangle_descendants=3,
                   source_renderer_sha256=hashlib.sha256(inspect.getsource(module.preview).encode()).hexdigest(),
                   scoped_renderer_sha256=hashlib.sha256(source.encode()).hexdigest())
    output.write_text(json.dumps(receipt, indent=2) + '\n')
from bc2_magazine_assembly import derive_assembly
import bc2_reviewed_magazine_role as role_review
names = [n for n in skeleton.names if n in reference.tracks and n.startswith(('jntWpn', 'LeftHand'))]
closed = reference.evaluate(skeleton, 0., names)
parts = module.mag.rigid_parts(lod_raw, module.metadata(mesh_raw)[0], skeleton, strict_assembly_skin=True)
merged, assembly = derive_assembly(parts, skeleton, closed, clip, 'jntWpn_12')
reviews = json.loads((root / 'handoff/tasks/CHAT16/20261030-modanchor40/review-template.json').read_text())
review = reviews[0]
assert review['assembly_digest'] == assembly['assembly_digest']
assert review['visual_review_sha256'] == receipt['sha256']
review['part_digest'] = role_review.part_digest(merged)
review['review_digest'] = role_review.digest({k: v for k, v in review.items() if k != 'review_digest'})
role_review.verify_context(review, binding, clip, skeleton, row['source']['resource'],
                           row['source']['mesh_sha256'], row['source']['lod_sha256'], rail['reference_clip_sha256'])
review_path = scope['PREVIEWS'] / 'm1911-role-review.json'
review_path.write_text(json.dumps(reviews, indent=2) + '\n')
print(review_path)
