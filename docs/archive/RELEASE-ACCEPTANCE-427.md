# BC2 VR release acceptance after the 0.1.7 failures

Build success, controller publication, a visible knife, and a played attack
animation do not prove a playable campaign. The published 0.1.5–0.1.7 gameplay
build did not meet the mandatory opening-interaction requirement. Its public
page is marked campaign blocked. Do not promote a replacement based on unit
checks alone.

## Required evidence for the next gameplay package

| Check | Evidence required | Current result |
| --- | --- | --- |
| Opening knife | Actual native acquisition and attack, matched target damage, observed guard death and campaign progression | r10 completes real physical attacks; target damage and opening progress still unverified |
| Repeated knife use | Actual missed physical jab followed by a separate accepted jab; interruption does not leave later input blocked | r10 `motion42703` completes two physical misses with no pending/unknown action; interruption remains unverified |
| Death/checkpoint restart | Both-eye transition, restored playable controls and rendering, no crash in the tested opening flow | One r9 Last Checkpoint reload recovered actor and both-eye images, with no captured exception; actual death and broader stability unverified |
| Main menu and pause | Controller navigation into campaign and back; loading does not require desktop keyboard | Retest on the final paired build |
| Final package | Exact installed binaries match tested source/build/package receipts | No new gameplay package prepared |

Disappearing `USEKNIFE` text is input evidence: the authored opening graph can
hide that prompt on melee input. Guard-kill and patrol-progression events are
separate. A native health sample that becomes unreadable after entity retirement
is unknown, not proof of a kill or no damage.

## Setup compatibility checks

The matrix in `tests/Test-LauncherUserSetups.ps1` uses the real installer,
updater, rollback, Worker and Entry scripts with local fixtures. Its portable
run passed 26 checks: installation and
update in paths containing spaces, Unicode and ampersands; preserved settings
and rollback; manually selected Steam-style, EA-style and Unicode game paths;
VDXR routing without querying Steam; and visible handling of an intercepted
headset failure. It wrote about 600 KiB and started no game or network request.

Process, network and XR boundaries were mocked. Folder layout is not an EA
binary compatibility test. These checks do not emulate a GPU driver, headset
transport, real account permissions, or SteamVR/VDXR rendering. Published
support claims must distinguish those unverified configurations from actual
tested hardware and executable identities.

## Failure reporting

`probe_exit:20` identifies the native exception handler, not a particular failed
check. The session's `native-trace.json` contains the underlying error when that
report was written. Unpublished launcher fixes display a bounded failure reason
and include its sanitized summary in local feedback. The external EA tester's
specific startup cause is now confirmed from their supplied archive: both native
audio readers restricted the cache drive to G:, rejecting a valid X: install.
Both sessions report the same audio-validation error, the executable matches
the supported build, and the cue hashes/layouts are exact. The unpublished fix
accepts absolute local cache paths on the player's chosen drive while preserving
the content gates. The path regression covers C:, X:, Unicode and malformed
forms without claiming a real hardware launch on those drives.

Release notes must state remaining mandatory-flow blockers before publishing.
Passing setup tests must never be presented as verified campaign completion.
