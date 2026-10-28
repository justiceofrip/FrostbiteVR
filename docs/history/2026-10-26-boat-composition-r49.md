# Continuous boat / reload / holster diagnostic preparation

Prepared a private exact-PBL-driver sequence using ordinary input and persistent
consumers: exit, prove empty hands, draw the current rifle, remove/reinsert its
magazine, discard/refill from chest, reacquire support and fire, release support,
and stow/draw the same item. No resets, provisioning, fixed weapon coordinates
or substitute vehicle are permitted inside the sequence.

Peer review corrected the full native image budget, removed a receiver force-kill,
required support release before stow, and caught the terminal diagnostic observer
continuing to advance until its original deadline after completion. Terminal
observer evidence now freezes; ordinary consumers and active deadlines do not.

Final source preparation SHA256:
ae97ef160d60f336d2aaf06cdbd73fe445649a1e2e7cdd0f4e0303aff7ebf647
Runner preparation SHA256:
e9514a00515208356e47713673602300e06ed3edd38e80da52fe2658d666b037

Root's three Python result-checker tests passed. Two affected C++ regression
targets also passed once after recompiling the private driver objects and tests
against existing x64 policy libraries. This includes a real observer warmup and
Use transition, active-timeout negative control, and late/stale input after
terminal stopping. It does not simulate the native boat or establish acceptance.

Receipt: build/x64/scratch/boat-composition01/validation.json, SHA256
ef777b3d01b2520d9d173cc90402343bff9bc8da2be83343a0cd673ce5c3ce2a.
The initial sandbox storage scan stopped on a restricted old test folder. The
same 40 GiB / 15 GiB guard passed outside the sandbox before any compilation;
64 MiB was reserved and all outputs remained on G:. No native module, game,
headset, input, runtime update, or full native suite was executed for these tests.

Next: reviewed diagnostic module build and one actual fresh PBL scene, with the
matching selected rifle/cache. The SP01 mounted checkpoint used by R48 is not
this fixture. Preserve unchanged normal runtime until actual integration checks.
