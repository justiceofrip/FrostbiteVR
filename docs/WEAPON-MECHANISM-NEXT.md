# Next weapon mechanisms and ownership

The user requested the remaining sniper, shotgun-pump, launcher/attachment and AEK
horizontal-sight work, then explicitly requested handgun magazine eject, insertion
and chambering. These are development requirements; none is added to the prepared
148/148-tested empty-XM8/shoulder candidate by this planning update.

## Mechanism groups

| Group | Reuse and requested behavior | Current gap |
| --- | --- | --- |
| Magazine-fed pistols | Existing detachable-magazine/supply/retained-round/rail policy; physical slide rack or slide-release where supported after empty reload; tactical reload retains chambered round | Exact handgun family admission, magazine/slide geometry and native chamber/ready semantics unbound |
| Revolvers and other sidearms | Exact cylinder/round or other authored mechanism | Do not force a detachable-magazine/slide flow on the whole sidearm class |
| Pump shotguns | Accepted SPAS bottom loading plus shared rear/forward cycle recognizer | Ordinary manual pump gate, state-labelled part/contact and native completion unfinished |
| Self-loading shotguns | Exact authored magazine or shell-loading family | Do not add a mandatory pump from shotgun category alone |
| Bolt-action snipers | Shared ordered unlock/back/forward/lock policy plus their actual loading method | Exact bolt geometry and native family cycle authority; magnified optic is separate work |
| Self-loading snipers | Shared magazine family where exact configuration supports it | Ordinary grip/magazine and native descriptor admission; scope integration separate |
| Grenade/rocket launchers | Shared ammo reservation, guided insertion and request/acknowledgement; profiled opening/closure where the game asset supports it | Native one-round completion, actual mechanism/round geometry, independent ammo pools |
| Underbarrel buckshot/smoke | Existing authored XM320 variants have capacity1 and launcher-style reload; investigate shared loading geometry with exact ammunition-kind profile | Do not reuse SPAS tube loading or grenade ammo pool from the attachment name |
| AEK/GP30 sight | Shared sight contact/grasp/mode/handoff with measured sideways motion | Closed side subtree known; actual moving clip must distinguish hinge/slide and supply its own hand pose |

Pistol chambering is now explicitly requested. The prior deferral of extra rifle
charging-handle work remains. Native loaded+0x7c has not established a separate
chamber count; conserve real ammunition and distinguish physical ready gating from
an unsupported capacity-plus-one or chamber-ammo write. Rack/release is required
when needed, not automatically after a tactical reload with a retained live round.

Manual underbarrel reloads remain deferred under the user's earlier instruction;
stock native reload remains the current preview behavior. This does not defer the
AEK horizontal sight interaction. A shared firing asset does not prove identical
hinge/slide geometry or which rifle owns the selected attachment.

## Underbarrel animation sharing checked in saved assets

The existing typed catalog contains 124 underbarrel configuration definitions
(including SP/MP and optics variants), grouped into seven exact first-person
animation-tree references: AN94, F2000, M16A2, M203, GP30, HK416 and XM320.
Every group contains the grenade (`40mmgl`), buckshot (`40mmsg`) and smoke
(`40mmsmk`) variants. This supports sharing animation-family work across ammo
variants. It does not establish one universal reload clip across all rifles.

Each group also has three parent mesh-reference variants. Resolve the tree's
actual clip selection/parameters and content identity, then deduplicate identical
mechanism motion; retain per-mesh placement and independent ammunition pools.
The GP30 sight remains separate from the XM8 sight. Exact reference/source-hash
evidence is saved privately in
`<local-recovery>/underbarrel-animation-groups-120602.json`.
This inspection changes no native capability or deferred launcher priority.

## Work queue

Three agents are currently active: reticle ownership/aperture diagnostics, common
magazine runtime registry, and strict LMG drum geometry. Their queued next tasks are:

- **body_prop_render:** AEK horizontal sight profile after the reticle candidate;
  sniper/launcher magnification remains a separate later task.
- **reload_takeover_failure:** ordinary pump/bolt native adapter after the registry;
  later handgun slide/chamber binding reuses the conserved ammo/ready contract.
- **shell_hand_failure:** batch handgun/sniper/shotgun config and mechanism extraction
  after drum candidates, prioritizing magazine-fed pistol magazine/slide evidence.
- **root:** preserve the prepared test build, audit pump prerequisites and review
  composed candidates before native tests. Pump source audit is saved privately in
  `<local-recovery>/pump-cycle-next/evidence.json`.

Queued is not active or completed. All background tasks use isolated recovery
candidates without taking game input or replacing the prepared test binaries.
No new native pump, chamber, launcher reload, sniper scope or AEK sight acceptance
is claimed. Exact active assignments remain in [AGENT-STATUS.md](AGENT-STATUS.md).
