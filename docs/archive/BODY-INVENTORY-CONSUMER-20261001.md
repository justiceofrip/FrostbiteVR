# Chest ammunition and shoulder selection candidate — October 1, 2026

The opt-in body inventory mode connects chest ammunition to the existing physical
SPAS reload consumer and right-controller shoulder grabs to BC2's original weapon
selector. Ordinary sessions remain unchanged. This is a shoulder-selection/chest
candidate, not completed holstering: `empty_hands_enabled` stays false, no gun is
hidden or relocated, and no holstered gun mesh is rendered yet.

## Connected path

`Start-BC2VRSession.ps1 -BodyInventory` selects the new option and its physical
reload, hands, aiming, body-follow, and shared-grip prerequisites. The existing
`-SightFlip` option can be combined. `Start-NativeTrace.ps1 -BodyInventory` and
`BC2NativeTrace --body-inventory` expose the same explicit flag, `0x10000000`.
Only real tracked streams are accepted: synthetic reload/action/optic fixtures
are rejected before native launch. The unchanged `Test-NativeStream` fixture
continues using its original waist geometry; no new synthetic gesture is hidden
inside that test. Source delivery does not enable or launch a session.

`gameplay::EnableBodyInventory(config)` runs after Install and before Start.
It requires the verified native selector, controller hands, body-follow, the
physical reload consumer, and valid configurable anchors. It replaces the unused
pre-Start ammo policy with the same native API and a chest anchor. An active item,
reload cycle or unknown reservation is never replaced by configuration.

`BodyAnchorConfig` contains metre-space comfort defaults: chest
(-0.16,-0.25,+0.10), left shoulder (-0.20,-0.12,-0.18), and right shoulder
(+0.20,-0.12,-0.18). Canonical +Z is forward; these are comfort zones, not measured
anatomical sockets. The frame uses current head position and the upright yaw of
the established/recentered `referenceHead`. Looking over a shoulder does not move
the contact away. Snap turning rotates the same world/reference mapping for body
and controller. Recenter moves the zones into the new shared reference.

This initial fixed-heading comfort mode does **not** estimate physical torso
rotation: after a 90-degree physical body turn, the zones still use the prior
reference forward until recenter. It is suitable for the initial seated/snap-turn
test, not a claim of full torso-follow behavior. The user can recenter with the
existing both-stick hold. Persisted settings and a measured/adaptive torso frame
remain later work; no head-yaw-driven shoulder chasing is introduced.

The right hand must first release squeeze, then press it inside the assigned
shoulder zone. A shotgun prefers the right shoulder and other long-gun categories
prefer the left; surviving items retain compatible assignments. Both preferences
allow the other shoulder, so pickups and a two-rifle loadout still work. Category
values are verified from reflected `WeaponClassEnum`; no asset names are compiled
into the new inventory resolver. Unassigned gadgets and underbarrel modes are not
given a spare gun slot. The existing separately verified launcher-family resolver
can map its selected launcher to the corresponding held rifle; shared persistence
alone never authorizes that relationship.

## Native identity and dispatch

`ReadBodyInventory` repeats the player/weak/soldier/controlled links, flag-selected
inventory, complete bounded pointer array, selected item, item data/category/
persistence, reflected class metadata and complete switch map. It rejects changed
cohorts, duplicate pointers, bad layouts and malformed bounds. Up to 32 inventory
slots and 128 switch entries are permitted. Read failure is a lifetime gap; a
returning pointer receives a new generation. Observed removal/replacement, changed
data/category and actor/space changes also invalidate contact identity. A pointer
removed and reused entirely between two coherent observations is not observable;
no native creation/destruction callback proof is claimed.

Only existing non-firing selector actions 7, 33 and 36 are eligible. The first
populated target must be the intended gun; pseudo-slots and ambiguous duplicate
routes reject. The original selector still owns eligibility and equipment
animation. No native equipped pointer or weapon data is written. Gameplay repeats
owner/route immediately before staging the original cache action. Thumbstick
cycling remains available and explicitly cancels a pending body request.

The actual shared `HandInteraction` arbiter reserves the right-hand body contact
and returns it to the existing native GunHold for the transition. It never steals
a support/sight/ammo claim. Any physical reload item, pending reservation, active
cycle or cancellation retirement blocks equipment changes. This uses a narrow
`BlocksEquipment()` getter; reload policies, counts and receipts are unchanged.

BodyInventory commits a draw only after exact actual selection and a fresh
matching existing owned rig/shot publication from the ordinary render path. That
publication keeps its original input generation/deadline, full native owner,
physical equip owner and actor/space checks. It is a baseline ordinary-presentation
acknowledgement, not evidence for a future hide/show override. Missing pose evidence
leaves the draw pending until timeout; wrong selection/replacement cancels it.
Pending requests clear Fire/Zoom/Reload through the existing native input cache
override and clear the native grenade bit for that tick. No global input is sent.

## Transition to actual holstering

Current selected-item shoulder presses do not holster it and never report empty
hands. The parallel exact mesh/skin visibility work owns a separate renderer
request/receipt. That request must preserve native owner, actual original input,
physical hand ownership, selected mesh lease and transaction identity. Native
acceptance must establish reversible gun/attachment hiding while both hands stay
visible, plus suppression of native firing sources. Only then can BodyInventory's
Holster/EmptyHands transaction and free right-hand poses be connected. A private
palette receipt alone is not silently promoted to complete native visibility.

## Validation and next root check

22 focused adapter groups pass in x86 and x64 with `/W4 /WX`: actual bounded reader,
layout/category mutation, native map resolution, shared claim contention, one-shot
dispatch, replacement generations, ordinary-pose freshness/equip rejection,
selection acknowledgement, tracking/reload cancellation, anchor signs/head-only
yaw/recenter, and real InputOverride fire/grenade clearing plus restoration.
16 PowerShell validation cases and 13 compiled CLI parser cases pass. Parser
tests use a nonexistent DLL and stop before process discovery/attachment.
The composed x86 Gameplay, NativeProbe and CLI compile; root owns full builds.

No game process, hooks, input, assets, configuration or headset session was changed
by this candidate. The next bounded native check should measure reader cost and
confirm current SPAS/rifle round-trip selection, ordinary pose acknowledgement and
no action while holding ammunition. A subsequent headset check must validate
actual reach comfort and chest acquisition; neither is established by unit tests.
