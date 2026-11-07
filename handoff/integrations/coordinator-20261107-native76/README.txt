Native76 candidate: shared mechanical handling and diagnostic tools

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
are reused.716's full isolated host-diagnostics suite passes. Combined native
builds and installation are pending. Installed75
remains the current headset candidate. These results do not establish actual
underbarrel reload completion, headset reliability or performance improvement.
