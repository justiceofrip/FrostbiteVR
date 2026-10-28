# R37 controlled-position performance investigation

R37 attempt02 passed all 304 suites on both architectures. It adds an explicit
private three-source pipeline and successful-delivery request refill while
retaining two undelivered immutable requests, one host copy, native restoration
and actual GPU/source retirement. Public/installed 0.1.12 was not changed.

R37 game68/onfoot40 was rejected: the user confirmed moving its window off the
accepted position, and the run had 16 host timeouts. Its raw 84.4351 fresh stereo
pairs/s is not accepted performance evidence. A retained 266.36 ms gap spans
native admission to left capture; that interval is not a GPU duration.

R37b reused exactly the same binaries and settings with an early read-only
placement gate. All 1,500 window samples matched. Clean teardown and normal close
passed, with no settings write. Two host timeouts still failed the unchanged
whole-session gate. First timeout was again ordinal 11 near the first local
player ownership transition, before measurement; the second timestamp was not
retained. The cause is not established, and window movement is not proved to
have caused the previous additional stalls.

All nine enclosed counter intervals were retained: 797/9.0467423 = 88.0980 fresh
pairs/s, zero interval discards. Two of 760 retained interior native rows lack
depth (requests1298 and1686). This is UNACCEPTED diagnostic throughput, neither
physical headset FPS nor an accepted before/after gain. R33c remains the accepted
baseline at 59.7831 fresh pairs/s in its separate emulated scene.

Next change: fix the shared Busy-at-depth-admission path, and retain bounded
timeout state under the existing IPC lock to classify startup losses. Do not
extend deadlines, relabel old poses, weaken depth proof or repeat unchanged
benchmarks. Existing deterministic/GPU cases and full builds validate the change
before another bounded actual-game observation.

Evidence under `bc2vr-recovery/performance436`:

- R37 runtime receipt: `fb0243756a495d7b6696205bdf170a8478b287d8a1e3ff936ca8dba22fb9a990`.
- R37b completion: `48dc90453a496862406a763f817d150413de8ed2d6159933cbe0167c90bc9dbe`.
- R37b raw diagnostic: `f77f37afd312a64b5cf962dfd506e5a49e19090aa576b6a475e91c2fdc527ac3`.

Storage remains bounded on G:. Current and baseline binaries/symbols and all
user recordings remain. R37b raw frames were losslessly compressed, saving
248,213,504 bytes with all hashes preserved.
