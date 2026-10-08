# Tracking and stereo frame channel

FvrFrameChannel connects an x64 host and a single x86 render producer using a
fresh GUID-named Local\FrostbiteVR.Control.v1 session. A named mutex protects a
512-byte shared mapping; an event wakes the host. Process handles and creation
times detect peer exit and PID reuse. Default Windows object security is used;
this is same-user, same-machine development IPC, not a network service.

FrameRequest is 240 bytes in both architectures. It explicitly encodes reference,
head and eye poses, asymmetric FOV, GPU LUID, dimensions/typed format, tracking
and space generations, predicted OpenXR time, request ID and a separate QPC
completion deadline. Invalid poses, reserved fields, versions and sizes fail.
The OpenXR timestamp is never compared with the QPC clock.

The slot advances through requested, rendering, ready, delivered and complete.
Only a complete matching GPU ticket is delivered. Before delivery, a missed
deadline cancels the request and the producer reclaims or recreates its texture
stream. After delivery, only explicit GPU-consumption feedback or peer exit
permits reclamation. A newer request cannot overwrite an owned slot. An
abandoned CPU mutex poisons the session rather than trusting partially written
state. Resource sharing/key ownership is separately enforced by FvrD3D11.

RemoteFrameProvider implements IFrameProvider for the XR host. Its default
rendezvous budget is 8 ms, configurable 1..50 ms. This is bounded CPU waiting;
it does not promise that BC2 will render within that budget. Game-side
RemoteFrameProducer::Pump never waits for the CPU mutex and must run at a
verified native graphics boundary. Its local provider receives feedback for
cancelled or consumed pairs. The native BC2 provider is still pending.

BC2XrHost --ipc creates a channel and prints frame_channel=<token> to stderr.
It does not launch, inject, or attach the game. Explicit bootstrap coordination
must pass that token to a producer. The existing --test-scene mode remains a
local synthetic renderer. Controller actions are not in protocol version 1.

## Verification

Test-FrameChannel.ps1 runs a real x86 renderer against an x64 receiver with no
headset/runtime. Four GPU pairs passed scene geometry, stereo parallax and head
translation checks; a deliberately unconsumed pair forced a new texture session
and rendering recovered. Tracking/space IDs exceed 32 bits. Separate child
processes tested process exit and abandoned mutex handling. A second producer
and malformed token were rejected. Logs include exact binary hashes.

FrameProtocolTests separately exercises deadline races before/during/after
render, stale publication, invalid data, one-time delivery, and the rule that a
timeout cannot reclaim a pair already owned by the GPU consumer.

Windows contracts: [WaitForSingleObject](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject)
and [CreateFileMapping](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createfilemappinga).


## Staged native callbacks

StagedFrameProducer separates TryBegin (visibility thread) from Submit and
PumpGraphics (graphics thread). TryBegin copies the exact decoded poses/FOVs
into a FrameLease tagged with channel generation, request ID and the adapter's
owner/frame/device key. It calls no renderer or simulation. A taken native
frame cannot be repeated after failure, cancellation or reconnection. A new
world/device generation must be supplied by the adapter when identities change.

Submit transfers a complete ticket only when its native frame and original
tracking/time/space/GPU requirements match. Call it only after restoring native
state. Busy/Invalid retain caller ownership; Cancel abandons an unfinished
lease without inventing a GPU ticket. PumpGraphics publishes nonblockingly and
returns consumed/discarded feedback exactly once on the graphics thread.
Both native callback threads must be quiescent for Connect/CloseGraphics and
destruction. The feedback provider must outlive the staged producer.

Game operations use a local atomic try-lock and zero-time interprocess mutex
polls. The host uses a bounded one-millisecond mutex wait during rendezvous to
avoid starvation, and retries promptly if a response signal arrives while the
producer holds the mutex. Without this, an auto-reset event could be consumed
before the receiver could read the ready pair. Feedback can be deferred under
contention; FlushFeedback lets the host drain it without requesting another
render, which matters when rendering pauses or a test ends.

StagedFrameProducerTests covers separate callback threads, changed owner/device,
wrong frame/tracking tickets, cancellation, duplicate frames, late completion,
reconnection with repeated request IDs, and delivered GPU ownership beyond the
CPU deadline. A deliberate recursive mutex hold reproduces the wakeup race.
After the fixes, 20 repeated runs passed in each architecture.

Test-FrameChannel.ps1 -Staged uses separate visibility/graphics threads in the
x86 GPU fixture and the x64 receiver. Four pairs pass exact tracking identity,
eye parallax, translated head movement and discarded-stream recovery; peer exit
and abandoned mutex checks also pass. Final report is
reports/ipc-staged-cross-architecture.json (2026-09-25 11:53 UTC). The original
single-thread path was rechecked as well. No headset or native BC2 rendering
was used for either hardware fixture.


## September 27 asynchronous host path

BeginRequest and PollRequest use zero-wait mutex attempts. A pending request retains
its original tracking sample and bounded deadline across runtime frames. Suspend
cancels pending production, retrying if the mutex is busy. A Delivered pair is
never revoked by the deadline or cancellation; GPU feedback must complete first.
RemoteFrameProvider returns the original rendered tracking separately from current
tracking. Recenter, incompatible output requirements and time reversal cancel the
pending sample. Protocol v2 and synchronous RequestPair are unchanged.

Native probe `unavailable_attempts` counts unsuccessful polls (including pending
work), not deadline expirations. Historical `timeouts` fields also counted polls.
