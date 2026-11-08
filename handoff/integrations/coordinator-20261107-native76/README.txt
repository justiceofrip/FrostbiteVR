Native76 installed: shared mechanical handling and diagnostic tools

Source changes
- Fixed-container/underbarrel mechanical handles use shared12cm acquisition,
  natural wrist orientation and held endpoints. Registered axes/travel and
  ammunition transfers remain exact; insertion placement is unchanged.
- Opt-in held linear mechanisms project ordinary diagonal/wrist motion onto
  their measured rail. Discontinuous positions cannot complete a stroke.
- Partial regrasp preserves the acquired wrist offset and the mechanism pose.
- Session shutdown distinguishes a valid final native report from a process
  exit with missing/incomplete diagnostics, preserving the original outcome.
- analyze_bc2_frame_timing.py joins native and host records by full ticket,
  separates resource/owner/device cohorts and reports missing/censored data.
- Host diagnostics retain exact-ticket copy-query results and uncensored
  wait boundaries in bounded storage using the existing clock reads. They add
  no polls or waits and do not measure GPU execution time.

Evidence and delivery
ROOT706 owns integration.710/714 isolated shared-mechanism checks pass;118 new
recovery cases cover59 mechanical profiles, with a failing710 negative control.
707 applied54checks and281PortableSession checks pass;709's unchanged27checks
are reused.716's full isolated host-diagnostics suite passes. Normal builds passed
351 x86 /348 x64, including17 focused suites per architecture and chest checks.
The x64 compiler caught a host-local name collision; ROOT corrected it without
rebuilding the unaffected x86 target. Actual compilation commits remain c11d600
for x86 and c16ff07 for x64; both validate the final source c16ff07. Original logs
remain in canonical reports706/native76/run02 and x64-repair01. Failed attempts
are preserved. Postbuild-only run03 reuses these results; no tests are relabeled.

Source c16ff07 is pushed to campaign and accepted checkpoint refs. Private0.1.76
is installed and setup-validated, fallback0.1.75/settings/cache preserved.
105 package files,97 runtime files and307 equipment parts verified. Stable
G:/Unity/VRMMO/Play-BC2VR-Test.cmd selects76 through the reviewed720/428 entry;
the installed4GiB runtime guard passes with768MiB session reserve. No game started.
The preceding shared belt-fed continuity and trigger-free stock knife jab remain.

ROOT reviewed719/720 postbuild and headset tools, corrected path normalization
and the postbuild-only reservation check, then verified actual installation and
shortcut. All707-720 agents are retired. The final delivery uses an explicit52GiB
private storage scope/15GiB free floor; compiler caches and evidence were kept.
Source/package build receipt3b1a6e4e822db3d2a064e6f461c2ee8ffbf10eafa9f1d68b028ec258e5848707.
Installationa504c5acbb1ac77941195562e72a6b068b0545503faf1a14d3fe97176303c9d8.

These checks do not establish full underbarrel reload completion, headset grip
reliability, knife damage or performance improvement. User74 feedback remains
open until a new headset result. No public release. Ready tasks/remaining limits
remain in COORDINATION and CURRENT; no repeated desktop-black or angle searches.
