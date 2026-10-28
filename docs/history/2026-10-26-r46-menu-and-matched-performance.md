# R46 asynchronous menu copy and matched comparison

The previous game88/onfoot49 run failed with14menu errors. Its first failure was
the blocking GPU copy's20ms wait, not the stereo pipeline: the observed call
lasted36.72ms while the original surface lease still had37.84ms remaining.
Replaced that menu copy with existing SubmitCopy/PollCopy into owned scratch.
The source ticket, original deadlines, cancellation, feedback and XR reference
space remain enforced; there is no timeout extension or positive ACK while busy.

Three affected host suites passed, then normal x64 and x86 each passed305/305.
Frozen runtime `unreleased-checkpoint436-r46-menu-copy01`, receipt SHA
`edec49147b8b91a5bf92597384380b130af5a27fe3d3d8e271ddf0c3fed60a86`.
Native BC2 dependency closure and native binaries are unchanged from R45.

Games90/91 completed fresh matched runs with identical binaries, quality,
profiles, adapter and bounded native viewpoint:2784x3024 per eye, actual1x
samples, queue1 and the existing post-Present service. Zero menu errors or
stereo timeouts; exact cleanup and all three profile restorations passed.
Two-source/no-overlap/no-refill:404pairs/9.0635697s=44.574049pairs/s.
Three-source/overlap/refill:806pairs/9.0461958s=89.098226pairs/s.
All nine intervals retained, no interior discards. Combined-policy descriptive
gain99.8881%; not physical-headset FPS or an improvement over public0.1.12.

Comparison SHA `13332ce438b514627f19a2ec2e6dc23be74539c4c24719faa4e44e35c897a254`,
at `G:/Unity/VRMMO/bc2vr-recovery/performance436/r46-matched-analysis-prep/comparison.json`.
Final desktop captures show a normal matching scene; exact paired pixels within
the CPU interval were not captured. No claim of full stereo visual/campaign or
weapon-mechanic acceptance. New launcher staging and remaining campaign work
continue separately; installed/public0.1.12 unchanged.

Before building, exact-manifest lossless compression reclaimed1,209,068,001bytes.
All content hashes remained unchanged; no source, evidence or recordings deleted.
All new output/temp files remained on G: under the existing storage limits.
