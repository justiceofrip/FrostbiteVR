Feedback80 - current holster evidence for ordinary LMG draw diagnostics

Live mechanism snapshots previously exposed weapon-action blocking without a
current Empty/ownership receipt. The existing bounded observation request now
publishes an immutable holster sample and its original owner/input/visibility
and suppression leases. Independent gameplay/render input ordering is explicit.
Genuine Empty is distinguished from an unavailable sample or merely blocked
weapon actions; observation failure does not invalidate normal gameplay.

Holster evidence carries its own original validUntilNs. Root kept it independent
of the snapshot's general deadline so this optional observation cannot shorten
unrelated feed, pump or sight evidence. No native operation, diagnostic gameplay
admission, default feature flag or ordinary holster behavior is changed.

Independent review found no issue before the separate-expiry refinement; root
reviewed that refinement. Focused and normal builds are pending. Candidate38
must pass both architectures and package checks before installation;37 remains
installed. No LMG reload or headset acceptance is inferred from observation.
