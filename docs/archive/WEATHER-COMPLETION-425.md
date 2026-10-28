# Native weather completion boundary —425

The424 actual-game run safely skipped all240 frames, made zero translation
writes, and reported124 rejected publications. Only two of four selected
observer rows completed. No native restore failure or watched exception was
reported. Its per-frame cache guards prevented an unverified correction.

The cause is the new native environment/world leaf wrappers returning directly
from inside `__try` and then consulting `AbnormalTermination()` in `__finally`.
That return is itself abnormal SEH control flow, even when the native call
succeeded. Consequently the environment producer was marked unsuccessful and
its observer invocation reset before normal completion could publish it.

Both wrappers now use one shared Windows-only leaf `InvokeWithFinally` in
`Bc2WeatherTranslation.h`. It stores the native return value, sets a completion
flag only after the original returns, performs cleanup in `__finally`, and
returns the saved value afterward. Cleanup receives that explicit completion
flag. Normal completion retains callback lifetime for postprocessing; an actual
native exception restores TLS and releases admitted callback scopes while the
exception continues to the original caller. EAX/result values are preserved.

The ProjectionOverride regression invokes this actual production leaf through
real Windows SEH, including nested callbacks. It checks normal completion and
result preservation, TLS restoration, retained lifetime on the normal path,
`RaiseException` propagation to an outer handler, and cleanup/drain of both
callbacks during nested exception unwind. It introduces no new test target.

No frame epoch, packet ownership, publication, translation, overlap or exact
restore guard is relaxed. Frozen424 evidence remains unchanged. Source is
prepared for parent-owned builds and a bounded actual-game run; this document
does not claim425 runtime or headset success.
