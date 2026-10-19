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
          new swing instead of ending one (the rule in use)

Columns: overshoot past the target (step/ramp) or worst |error| (steps/sweep), the worst
|error| in the last 120 frames, guard firings, final estimate / true gain.
2026-10-19 result: "reset" runs away on moving targets (ramp60 d=2 r=3: 9 rad); "cause"
matches "none" everywhere on overshoot and removes most false firings (sweep at r=1: 5 -> 0).
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
    raise ValueError(kind)


def run(rule, delay, ratio, kind, jump=0.69, frames=480, gain=0.0004):
    true_gain, est = gain * ratio, gain
    yaw, last_yaw = 0.0, 0.0
    last_target = target_at(kind, -1, jump) if kind in ("steps", "sweep") else 0.0
    queue, flight = [0] * delay, [0, 0]
    guard = dict(sign=0, peak=0.0, prev=-1.0)
    win_angle = win_counts = 0.0
    win_n = guards = 0
    worst = late = 0.0
    for n in range(frames):
        yaw += true_gain * queue.pop(0)
        target = target_at(kind, n, jump)
        d_target, d_game = target - last_target, yaw - last_yaw
        err = target - yaw
        worst = max(worst, yaw - jump if kind == "step" or kind.startswith("ramp") else abs(err))
        if n >= frames - 120:
            late = max(late, abs(err))
        win_angle += d_game
        win_n += 1
        if rule == "reset" and abs(d_target) > 0.01:
            guard = dict(sign=0, peak=0.0, prev=-1.0)
        if abs(err) >= 0.01:
            now = 1 if err > 0 else -1
            if guard["sign"] and now != guard["sign"]:
                if rule == "cause" and abs(d_target) > abs(d_game):
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
        flight = [counts, flight[0]]
        queue.append(counts)
        win_counts += counts
        if win_n >= 20:
            if abs(win_counts) >= 200 and abs(win_angle) >= 0.01:
                measured = win_angle / win_counts
                if 1e-6 < abs(measured) < 0.05:
                    est += min(1.0, abs(win_angle) / 0.2) * (measured - est)
            win_angle = win_counts = 0.0
            win_n = 0
    return worst, late, guards, est / true_gain


if __name__ == "__main__":
    for kind in ("step", "ramp30", "ramp60", "steps", "sweep"):
        for delay in (2, 3):
            for ratio in (1.0, 3.0, 5.0):
                cells = []
                for rule in ("none", "reset", "cause"):
                    w, l, g, e = run(rule, delay, ratio, kind)
                    cells.append(f"{rule}: {w:6.3f} late {l:.3f} g{g:<2d} est/true {e:4.2f}")
                print(f"{kind:7s} d={delay} r={ratio}: " + " | ".join(cells))
