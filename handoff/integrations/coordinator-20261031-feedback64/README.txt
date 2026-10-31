Feedback64: constrained feed hand targets and local XR simulation tools

The0.1.27 headset run showed slight LMG cover movement, no magnetic hand, and no
working reload. Retained telemetry rejects an acquisition as OffPath despite
passing timing gates; its exact geometric predicate is not recorded. The native
worker also exited0xc0000005 without a final report; no causal link is established.

This source batch publishes the constrained anatomical contact to the hand,
retains the real pickup offset for carried feed objects, and separates current
ammo-object hand evidence from delayed renderer input without extending leases.
The entry geometry rejection is still open; this does not declare the reload fixed.

tools/Invoke-Bc2XrSimulation.ps1 drives the actual XR host with pinned xr-sim and
xr-tape, process-local runtime selection, bounded capture and recorded commands.
It does not launch BC2 or replace the system runtime. Installation and host-test
results are recorded separately from source and native/headset acceptance.

Root alone integrates/builds/installs; completed agents are retired.
Detailed local evidence: reports/coordinator-20261031-feedback64.
