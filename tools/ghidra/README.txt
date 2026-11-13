BC2 offline Ghidra toolkit (wrapper/member search 1.1; function exporter 1.0)

Prerequisites: PowerShell 7.2+, an official unpacked Ghidra distribution and its
supported 64-bit JDK. The initial integration targets Ghidra 12.2 and JDK 25;
the coordinating agent records actual smoke-test versions/results separately.
This tool does not install/download software, attach to processes, launch the
game, patch executables, or establish native/headset behavior.

Set paths explicitly, or create this local, ignored config:
  build/toolkit/ghidra.local.json
  { "GhidraHome": "G:\\tools\\ghidra_12.2_PUBLIC", "JdkHome": "G:\\tools\\jdk-25" }
Precedence: explicit parameter, local config key, GHIDRA_HOME/JAVA_HOME.
No Windows/user-wide environment changes are made.

Run from the repository root (use real hexadecimal preferred-image VAs):
  & ./tools/Invoke-GhidraAnalysis.ps1 -Executable 'G:\Games\BC2\BFBC2Game.exe' -Addresses @('0x401000','0x402000') -OutputPath "$PWD/build/ghidra/exports/request01.json"
Addresses are examples, not verified BC2 functions. RVAs and rebased live-process
addresses must first be converted to preferred-image VAs. Paths may have spaces.
Only 1..16 distinct addresses are accepted. OutputPath must be a NEW .json file
under this checkout's build/ghidra; existing evidence is never overwritten.

For candidate discovery when only a member displacement is known, use the
separate MemberOffsets parameter set against an existing completed cache:
  & ./tools/Invoke-GhidraAnalysis.ps1 -Executable 'G:\Games\BC2\BFBC2Game.exe' -MemberOffsets @('0x1ac','0x38c') -SearchStartAddress '0xc00000' -SearchEndAddress '0xc0ffff' -OutputPath "$PWD/build/ghidra/exports/member01.json"
The inclusive range above is an example around a known queue insertion, not a
claim about the consumer address. Choose the intended range from the exact PE.
This mode never imports/analyzes or changes the program; it scans only existing
instructions with -noanalysis -readOnly. It matches unsigned Scalar values in
operands, including immediate additions and memory displacements. Each hit
records instruction address/text, matching operand/index/type flags, scalar
width/signed value, operand registers, and containing function when known.
These are numeric candidates: unrelated constants/receivers can match, and
computed offsets or unanalyzed instructions can be missed. No receiver type,
read/write ownership, queue consumer, or thread safety is inferred.
Search limits: 1..4 distinct 32-bit offsets, MaxInstructions1..5000000 (default
5000000), MaxMatches1..1024 (256), MaxFunctions1..256 (128), SearchSeconds1..120
(60). Limits count emitted instruction matches/unique containing functions;
function-less hits remain included and consume the match budget. The result
reports search_complete or search_truncated, stop_reason and next_scan_address.
Resume a truncated search with that start, the same inclusive end and a NEW
output; a function-limit stop reexamines the un-emitted matching instruction.
Displayed text is capped at512 characters with explicit flags. There is no
decompilation, recursive caller traversal, or synthesized disassembly in this
mode. Export a selected candidate afterward using the original Addresses mode.

The first call imports and analyzes one executable, storing a persistent project
at build/ghidra/projects/<executable-SHA256>/bc2.gpr. It reserves at least 1 GiB
(or 64 x executable size) for the database/cache through StorageBudget before
creating/launching work; the Java heap is a separate memory allocation.
Later calls reserve 512 MiB and use that exact project with -process -noanalysis
-readOnly. Analysis is bounded to 1800 seconds, two CPUs and a 2 GiB Java heap.
An OS-exclusive token serializes calls per executable. The input is held open
read-only with writes/deletion denied throughout hashing, analysis and export.
A version/hash mismatch or unfinished project refuses reuse; the wrapper never
silently rebuilds, migrates, deletes or replaces it. Inspect preserved logs and
project before explicitly recovering an interrupted initial analysis.
Both modes retain StorageBudget accounting across the existing generated roots.
MaxGeneratedBytes defaults to40GiB; a larger explicit ceiling requires a
GeneratedBudgetReason, which is retained in the admission receipt. This option
does not change the512MiB cached reservation, free-space floor, or root set.

The direct Java launch follows Ghidra's official support/launch.bat entrypoint:
  java.exe <installed VMARGS and VMARGS_WINDOWS> <storage overrides> -cp <GhidraHome>/Ghidra/Framework/Utility/lib/Utility.jar ghidra.Ghidra ghidra.app.util.headless.AnalyzeHeadless <project-directory> bc2 <mode arguments> -max-cpu 2 -scriptPath <repository>/tools/ghidra -log <run>/ghidra.log -scriptlog <run>/scripts.log -postScript ExportBc2Functions.java <run>/invocation.json <OutputPath>
First-call mode arguments:
  -import <Executable> -analysisTimeoutPerFile 1800
Cached-call mode arguments:
  -process <saved-program-name> -noanalysis -readOnly
ProcessStartInfo.ArgumentList passes each argument separately, without cmd.exe.
Each run preserves the exact Java executable/arguments, tool source hashes,
executable hash, installed/runtime versions, budget receipt and logs beneath
build/ghidra/runs/<unique-id>. JVM user.home, java.io.tmpdir, application.tempdir,
application.settingsdir and application.cachedir point into the project's
build/ghidra tree; child TEMP/TMP/TMPDIR do likewise. Inherited Java injection
variables are removed for this child. The caller's temporary-storage environment
is restored on exit. No installation launch.properties are edited.

JSON contains one record per requested address: containing-function/entry match,
name, signature, status and decompiled C when available. It reports missing
functions without creating them. Each function exports at most 256 incoming
references to its entry; callers are deduplicated call references from that same
bounded set. Truncation is explicit. This is not an exhaustive cross-reference
graph: interior references, computed/virtual calls and missed analyzer edges are
outside that scope. Decompilation is limited to 60 seconds and 1,000,000 C
characters per requested function. No callee traversal or whole-executable C
export occurs. Initial Ghidra analysis does analyze the executable to populate
the reusable database. Analysis timeouts are retained as incomplete results and
do not qualify the project for automatic reuse.

Keep all databases, executable material, decompilation and local config in the
ignored build tree. Commit only this wrapper, exporter and instructions. Static
decompilation is investigation evidence, not proof of live ABI/ownership or a
permission to enable a native feature.

Primary references:
https://github.com/NationalSecurityAgency/ghidra/blob/master/Ghidra/RuntimeScripts/support/analyzeHeadlessREADME.md
https://github.com/NationalSecurityAgency/ghidra/blob/master/Ghidra/RuntimeScripts/support/launch.bat
https://github.com/NationalSecurityAgency/ghidra/blob/master/Ghidra/RuntimeScripts/support/launch.properties
https://ghidra.re/ghidra_docs/api/ghidra/app/decompiler/DecompInterface.html
https://ghidra.re/ghidra_docs/api/ghidra/app/util/headless/HeadlessScript.html
https://ghidra.re/ghidra_docs/api/ghidra/program/model/listing/Instruction.html
https://ghidra.re/ghidra_docs/api/ghidra/program/model/listing/Listing.html
https://ghidra.re/ghidra_docs/api/ghidra/program/model/scalar/Scalar.html
