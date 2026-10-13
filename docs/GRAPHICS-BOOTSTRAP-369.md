# Graphics setup on the native rendering thread — local369

Private364 observed creation flags1 on BC2's D3D11 device. The old normal setup
queried its immediate context and swap-chain device on the injector worker,
then released those temporary COM references there. Microsoft's
[single-threaded device contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_create_device_flag)
does not permit calling those interfaces from multiple threads. This is a
concrete integration defect; it does not establish the cause of the intermittent
null graphics-worker crash saved in earlier runs.

Setup now enables only the verified native Present hook first. That callback
checks the native renderer, swap chain and device identity; obtains the context
and five required entry addresses; and releases its temporary references before
publishing plain borrowed metadata. The worker validates the same owner again
and installs the remaining hooks using those addresses. There are no bootstrap
device/context calls or reference releases on the worker. The
[context API increments a reference](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-getimmediatecontext),
so releasing on the callback thread is part of this change.

The bootstrap callback delegates directly to native Present during setup. It
does not run mod rendering, change sync settings or capture desktop/menu images
until initialization completes. A five-second wait bounds setup. Failure or
cancellation disables the early hook using existing retained-module cleanup;
a late callback cannot publish success after cancellation. No extra native
simulation or eye draw is introduced.

The reusable publication regression exercises incomplete publication, failure,
one-shot ownership, concurrent duplicate entry, cancellation before entry and
during capture, and visibility of the completed value. Both architectures pass
its four groups. Both normal builds pass all246 suites (x86:156.76s;
x64:112.30s).

The normal369 body inspection completed240 stereo pairs with239 paired depth
copies and237 compositions. All30 stable stowed and200 stable held samples
contained matching both-eye ammunition metadata. The median chest location
differs by0.0087mm;20 saved eye images visibly show the textured preview prop.
One prop expiry and two uncomposed depth pairs remain explicitly recorded.

The separate370 menu run used the same normal369 binaries and the corrected366
external controller. It completed restart, observed a replacement actor, reopened
and closed the replacement menu, then supplied197 additional stereo pairs.
Three pointer presses were released, with no remaining pointer ownership and
zero observed exceptions. One fading capture was retried without a dependent
click. Native view lists restored, pending references reached zero and hooks
detached.242 submitted pairs equal240 consumed plus2 discarded; the report
does not classify those two causes. Do not infer sustained performance from this.

Both native reports place bootstrap on the same thread as all sampled world
records, separate from the setup worker. The report's worker_com_calls=0 is the
bootstrap code's declared contract, not an API-wide dynamic tracer. The new local
player candidate is369;361 and357 are preserved. These checks neither identify
the old crash cause nor constitute headset acceptance.

## Depth observation retained separately

Normal367's first stationary run had ammunition metadata in92/92 stable pairs
but zero completed depth captures. Private368 added read-only clear/end counters
without changing admission. A fresh process then completed95 depth/composition
pairs,93/93 stable both-eye ammunition samples and20 visibly changed saved eye
images. The observed full-resolution depth pass uses1920x1080 D24S8 with8 samples
and the expected two clears; the shadow and half-resolution passes are ignored.

This did not reproduce or explain367's failure. Private368 source was restored
exactly before369 implementation. Its passing capture is not a normal-build
acceptance or a fix for all missing-depth cases. Player361 and357 remain saved;
no new headset acceptance, whole-roster coverage or public upload is claimed.
