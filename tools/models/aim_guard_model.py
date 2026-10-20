"""Model of the BF4 controller-aim yaw loop (src/games/bf4/Bf4MotionAim.h) for comparing
overshoot-guard rules. Run: python -I tools/models/aim_guard_model.py

The loop: counts = 0.5 * (error - in-flight turn) / estimate, the last two frames' counts
counted as in flight, +-600 counts per frame, the game applying counts `delay` frames
late at `ratio` times the estimated gain, 20-frame learning windows (>= 200 counts,
weighted by the angle turned) and the guard: a finished swing larger than 30 % of the one
before it grows the estimate by 1 + peak/previous (at most 4x).

Guard rules:
  none  - every sign change of the error ends a swing (the original rule)
  reset - the swing record is cleared whenever the target moves > 0.01 rad in a frame
  cause - a sign change the target made (it moved more than the game that frame) starts a
          new swing instead of ending one
  push  - cause, and the game must have moved the way recent counts (the last four frames)
          pushed it, by no more than 5x what they explain (the rule in use since session 15)

Columns: overshoot past the target (step/ramp) or worst |error| (steps/sweep), the worst
|error| in the last 120 frames, guard firings, final estimate / true gain.
2026-10-19 result: "reset" runs away on moving targets (ramp60 d=2 r=3: 9 rad); "cause"
matches "none" everywhere on overshoot and removes most false firings (sweep at r=1: 5 -> 0).
Session 15 result (with the probe): "push" matches "cause" in every scenario; deathcam at the
true gain keeps est/true 1.00 with no guard firing (without the probe the estimate flipped sign
and the guard fired 8-10 times).
The learning includes the probe of Bf4MotionAim.h (a window in which the game turned
against one-signed counts: quiet for 16 frames; turning on means the game moves by itself,
nothing is learned and aiming stays quiet 60 frames; holding still flips the sign), and
learns only windows of the estimate's sign. The "deathcam" target holds while the game
turns 0.01 rad per frame by itself for frames 150-270, ignoring the mouse; it is judged on
the frames after 300.
"""
import math

K_STEP, K_OVER, K_CAP = 0.5, 0.3, 4.0


def target_at(kind, n, jump):
    if kind == "step":
        return jump
    if kind.startswith("ramp"):
        return jump * min(1.0, (n + 1) / int(kind[4:]))
    if kind == "steps":  # teleports back and forth every 45 frames
        return jump if (n // 45) % 2 == 0 else -jump
    if kind == "sweep":  # continuous back and forth, 0.5 rad amplitude
        return 0.5 * math.sin(n / 14.0)
    if kind == "deathcam":  # the target holds; the game turns by itself in frames 150-270
        return jump
    raise ValueError(kind)


def run(rule, delay, ratio, kind, jump=0.69, frames=480, gain=0.0004, probe=True):
    true_gain, est = gain * ratio, gain
    yaw, last_yaw = 0.0, 0.0
    last_target = target_at(kind, -1, jump) if kind in ("steps", "sweep") else 0.0
    queue, flight, recent = [0] * delay, [0, 0], [0, 0, 0, 0]
    guard = dict(sign=0, peak=0.0, prev=-1.0)
    win_angle = win_counts = 0.0
    win_n = guards = 0
    win_abs, lag, muted, probing, probe_turn = 0.0, [0, 0], 0, 0, 0.0
    worst = late = 0.0
    for n in range(frames):
        applied = queue.pop(0)
        if kind == "deathcam" and 150 <= n < 270:
            yaw += 0.01  # the death camera: the mouse is ignored, the view turns on its own
        else:
            yaw += true_gain * applied
        target = target_at(kind, n, jump)
        d_target, d_game = target - last_target, yaw - last_yaw
        err = target - yaw
        worst = max(worst, yaw - jump if kind == "step" or kind.startswith("ramp") else abs(err))
        if kind == "deathcam" and 150 <= n < 300:
            worst = 0.0  # judged on what follows the death
        if n >= frames - 120:
            late = max(late, abs(err))
        win_angle += d_game
        win_n += 1
        if probing:  # quiet, watching: 4 frames for counts in flight, then 12 measured
            probing -= 1
            if probing < 12:
                probe_turn += d_game
            if probing == 0:
                if abs(probe_turn) >= 0.01:
                    muted = 60  # the game turns by itself: stay quiet, learn nothing
                else:
                    est = -est  # still without counts: the counts did it (inverted look)
                win_angle = win_counts = win_abs = 0.0
                win_n = 0
        if rule == "reset" and abs(d_target) > 0.01:
            guard = dict(sign=0, peak=0.0, prev=-1.0)
        if abs(err) >= 0.01:
            now = 1 if err > 0 else -1
            if guard["sign"] and now != guard["sign"]:
                same = sum(est * c for c in recent if est * c * d_game > 0)
                ours = d_game * same > 0 and abs(d_game) <= (K_CAP + 1) * abs(same) + 0.01
                if rule in ("cause", "push") and abs(d_target) > abs(d_game) or rule == "push" and not ours:
                    guard = dict(sign=0, peak=0.0, prev=-1.0)
                elif guard["prev"] > 0 and guard["peak"] > K_OVER * guard["prev"]:
                    est *= min(K_CAP, 1 + guard["peak"] / guard["prev"])
                    guard["prev"] = -1.0
                    guards += 1
                    guard["peak"] = 0.0
                else:
                    guard["prev"], guard["peak"] = guard["peak"], 0.0
            guard["sign"] = now
            guard["peak"] = max(guard["peak"], abs(err))
        last_target, last_yaw = target, yaw
        counts = int(round(max(-600, min(600, K_STEP * (err - est * (flight[0] + flight[1])) / est))))
        if probing or muted:
            counts = 0
            muted = max(0, muted - 1)
        flight = [counts, flight[0]]
        recent = [counts] + recent[:3]
        queue.append(counts)
        applied_lag = lag[0]
        lag = [lag[1], counts]
        win_counts += applied_lag
        win_abs += abs(applied_lag)
        if win_n >= 20 and not probing:
            expected = est * win_counts
            if probe and abs(expected) >= 0.05 and abs(win_angle) >= 0.05 and win_angle * expected < 0 \
                    and abs(win_counts) >= 0.8 * win_abs:
                probing, probe_turn = 16, 0.0  # the game turned against one-signed counts
            elif abs(win_counts) >= 200 and abs(win_angle) >= 0.01 and win_angle * est > 0:
                measured = win_angle / win_counts
                if 1e-6 < abs(measured) < 0.05:
                    est += min(1.0, abs(win_angle) / 0.2) * (measured - est)
            if not probing:
                win_angle = win_counts = win_abs = 0.0
                win_n = 0
    return worst, late, guards, est / true_gain


if __name__ == "__main__":
    for kind in ("step", "ramp30", "ramp60", "steps", "sweep", "deathcam"):
        for delay in (2, 3):
            for ratio in (1.0, 3.0, 5.0):
                cells = []
                for rule in ("none", "reset", "cause", "push"):
                    w, l, g, e = run(rule, delay, ratio, kind)
                    cells.append(f"{rule}: {w:6.3f} late {l:.3f} g{g:<2d} est/true {e:4.2f}")
                print(f"{kind:7s} d={delay} r={ratio}: " + " | ".join(cells))
