# Immutable reload draw producer candidate

This isolated candidate adds observed native request/frame/eye association to the
existing read-only draw collector. It changes no visibility, materials, drawing,
input or ammunition. Frozen staged/original sources were not edited. Hook copies
are review artifacts; apply integration.patch by context and add the two new
Bc2ReloadProducerBinding files rather than blindly replacing newer root files.

## What the candidate measures

NativeProbe brackets its existing verified visibility/prepare original calls with
a thread-local dynamic scope keyed by world, request, view, native frame and eye.
All native relationships are reread after the original call. A nested rejected
scope masks its parent; asynchronous workers have no inherited proof. Scope
unwinding or a changed native owner cannot publish a packet.

RigPublication attaches actor/weak, selected weapon, original owner/space/input
identity, rig pose/fingerprint, named shell/optic bone roles and the original
expiry to its immutable PublishedPose. It samples actual palette publication time
for observedNs and converts the existing QPC expiry to the SAME nanosecond clock
as the collector. It does not extend the original deadline.

After each original packer call, the candidate compares every xyz source scalar
with its exact 48-byte column-packed destination. It uses the input actually
passed to the native packer, including preview fallback when selected. Both
current/previous slots must have the same owner/source/packed bytes and distinct
native destinations under one GetA pair serial. The two pack slots are NOT eyes.
Only one unambiguous pair per request/frame/view/eye is accepted.

A bounded32-slot recorder retains immutable derived metadata, two native pack
destinations, a full packed-palette hash and the exact shell/optic48-byte patterns.
Draw lookup requires the identical world/request/view/frame/eye and fresh current
actor/weak/weapon/owner/space. No pointer into the native arena or input palette
escapes the capture. Nonblocking mutex contention skips evidence; it never stalls
rendering. The collector receives a copied producer record; duplicate eyes or
newer input cannot replace another request's pattern.

NativeProbe reports reload_palette_producer counters plus bounded packet metadata.
Native hook teardown disables recorder admission; no new callbacks or live game
experiments were run by this task. Add Bc2ReloadProducerBinding.cpp to BC2NativeProbe;
the focused test can compile it directly without D3D or BC2Camera dependencies.

## Deliberate remaining proof boundaries

Whether the native character packer runs synchronously under the observed
visibility/prepare calls is NOT established by offline source inspection. The
first diagnostic decides this: published/pairs show success; outside_scope with
no publication identifies a native worker/job boundary that needs its actual
request payload inspected. Do not substitute the latest global request or a
nearby timestamp when that happens.

The current RigPublication path has no verified Meshes1p native resolver or
palette-to-specific-mesh-instance join. selectedMeshes1p therefore stays zero
and selectedMeshIdentityVerified stays false in these wiring copies. An exact
item display name is used only to choose which already-verified named bone pattern
to capture; it is not promoted into a mesh ownership claim. The collector can
compare those patterns to the same request's GPU VS buffers now, while
producer_association_verified must remain false until selected-mesh proof arrives.

The latest exact ACOG geometry matches demonstrate the archive fingerprint can
match the native GPU stream. A matching packed pattern narrows palette ownership,
but multiple matches remain ambiguous. Neither request association nor an isolated
matrix match establishes the specific GPU remap by itself. Before any visual
suppression, require exact selected mesh/section ownership and an unambiguous
relevant GPU palette binding. The present collector's broad association flag is
not authorization for a renderer change.

This code intentionally captures only the already-owned modified palette path
(scope.changed and successful original pack verification). Native fallback-only
frames remain unbound. Extending those frames needs an equally exact immutable
source/owner snapshot, not reuse of the preceding modified pose.

## Validation

Six deterministic cases pass on both x86 and x64: immutable actual packing;
wrong eye/request/frame/view; incomplete/changed/ambiguous pairs; no asynchronous
or invalid-nested inheritance; lease/owner expiry; monotonic pack/end times;
default-off behavior and bounded reporting. The separate candidate NativeProbe
and RigPublication x86 objects compile successfully against frozen staged headers.
Full DLL link/build, native request-scope observation and selected Meshes1p proof
remain root integration work. No headset or visibility result is claimed.
