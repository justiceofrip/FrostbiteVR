"""Plain-language summary of a BF4 developer session's adapter log.

  python tools/bf4probe/summarize_session.py <path to bf4-adapter.log>

Reads the read-only probe lines the adapter writes in developer sessions (camera probe,
session probe, firing census with its ownership walk, bindings, view override) and prints
verdicts: is the camera candidate a real moving camera, did the session signals read the
front end and the campaign, does every gun have client and server copies, which client
class is active, and what most likely holds the client weapon entity (the next link of the
player -> weapon chain). Text in, text out; nothing about the game is assumed beyond the
log formats in src/games/bf4/Bf4Module.cpp.
"""
from __future__ import annotations

import re
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path

HEX = r"[0-9A-Fa-fx]+"
FLOAT = r"-?[0-9.]+(?:e[-+]?\d+)?|-?nan(?:\(ind\))?|-?inf"

CAMERA = re.compile(
    rf"camera probe: object=(?P<object>{HEX}) slot19=\+0x(?P<slot19>{HEX}) field=\+0x(?P<field>{HEX}) view=(?P<view>{HEX}) \| "
    rf"\+00 rigid=(?P<r00>\d) t=\((?P<t00>[^)]*)\) fwd=\((?P<f00>[^)]*)\) \| \+40 rigid=(?P<r40>\d) t=\([^)]*\) \| "
    rf"\+420 rigid=(?P<r420>\d) \| fov=(?P<fov>{FLOAT}) near=(?P<near>{FLOAT}) far=(?P<far>{FLOAT}) "
    rf"aspect=(?P<aspect>{FLOAT}) plausible=(?P<plausible>\d)")
CAMERA_UNREADABLE = re.compile(rf"camera probe: object=(?P<object>{HEX}) vtable=\d slot19=\+0x{HEX} field=\+0x(?P<field>{HEX}) view=.*\(unreadable\)")
SESSION = re.compile(r'session probe: level="(?P<level>[^"]*)" mode="(?P<mode>[^"]*)" isMultiplayer=(?P<mp>-?\d+)')
CENSUS = re.compile(r"firing census \((?P<why>[^)]*)\): (?P<objects>\d+) objects \((?P<limited>\d+) without unlimited ammo\)")
FIRING = re.compile(
    rf"(?:^|\s)firing (?P<addr>{HEX}) vt=\+(?P<vt>{HEX}) data=(?P<data>{HEX}) state=(?P<state>-?\d+)->-?\d+ loaded=(?P<loaded>-?\d+) "
    rf"reserve=(?P<reserve>-?\d+) cap=(?P<cap>-?\d+) mags=-?\d+ reloadType=(?P<rt>-?\d+) fireLogic=(?P<fl>-?\d+).*?(?P<unlimited> unlimited)?$")
PAIR = re.compile(rf"pair candidate: (?P<a>{HEX}) .* and (?P<b>{HEX}) ")
HELD = re.compile(rf"held: firing (?P<firing>{HEX}) -> (?P<kind>client weapon A|client weapon B|server weapon) (?P<owner>{HEX})")
ENTITY = re.compile(rf"client weapon entity (?P<entity>{HEX}) holds (?P<kind>client weapon [AB]) (?P<owner>{HEX}); active \(\+0x98\) = "
                    rf"(?P<active>{HEX}) \(A (?P<a>{HEX}), B (?P<b>{HEX})\)")
ENTITY_HELD = re.compile(rf"weapon entity (?P<entity>{HEX}) held at (?P<at>{HEX}): nearest vtable (?P<obj>{HEX}) \(vt \+(?P<vt>{HEX})\) at -(?P<off>{HEX})")
BINDING = re.compile(r"binding (?P<name>[\w.]+): (?P<status>[\w-]+)")
VIEW_OVERRIDE = re.compile(r"view override (?P<what>install|removed|refused)[: ](?P<detail>.*)")


def _floats(text: str) -> tuple[float, ...]:
    out = []
    for part in text.split():
        try:
            out.append(float(part))
        except ValueError:
            out.append(float("nan"))
    return tuple(out)


def _num(text: str) -> float:
    try:
        return float(text)
    except ValueError:
        return float("nan")


def _plausible(fov: str, near: str, far: str, aspect: str) -> bool:
    f, n, fa, a = _num(fov), _num(near), _num(far), _num(aspect)
    return 0.05 < f < 3.1 and 0 < n < 10 and fa > n and fa < 1e7 and 0.5 < a < 5


@dataclass
class Summary:
    camera: dict = field(default_factory=lambda: defaultdict(list))   # field -> samples
    camera_unreadable: Counter = field(default_factory=Counter)
    slot19: set = field(default_factory=set)
    sessions: list = field(default_factory=list)                        # (level, mode, mp), in order, deduplicated
    censuses: list = field(default_factory=list)                        # (why, objects, limited)
    firing: dict = field(default_factory=lambda: defaultdict(list))     # address -> [(loaded, reserve, state)]
    firing_info: dict = field(default_factory=dict)                     # address -> (reloadType, fireLogic, cap, unlimited, data)
    pairs: set = field(default_factory=set)
    holders: Counter = field(default_factory=Counter)
    entities: dict = field(default_factory=dict)                        # entity -> active class name
    entity_holders: Counter = field(default_factory=Counter)            # (vt, offset) -> count
    bindings: dict = field(default_factory=dict)
    view_override: list = field(default_factory=list)


def parse(lines) -> Summary:
    s = Summary()
    for line in lines:
        line = line.rstrip("\r\n")
        if m := CAMERA.search(line):
            s.slot19.add(m["slot19"])
            s.camera[m["field"]].append({
                "rigid00": m["r00"] == "1", "rigid40": m["r40"] == "1", "t00": _floats(m["t00"]),
                # Recomputed from the values (FOV in radians), so older logs that judged it in
                # degrees still read right.
                "plausible": _plausible(m["fov"], m["near"], m["far"], m["aspect"]),
                "fov": _num(m["fov"]),
            })
        elif m := CAMERA_UNREADABLE.search(line):
            s.camera_unreadable[m["field"]] += 1
        elif m := SESSION.search(line):
            entry = (m["level"], m["mode"], int(m["mp"]))
            if not s.sessions or s.sessions[-1] != entry:
                s.sessions.append(entry)
        elif m := CENSUS.search(line):
            s.censuses.append((m["why"], int(m["objects"]), int(m["limited"])))
        elif m := FIRING.search(line):
            addr = m["addr"].lower()
            s.firing[addr].append((int(m["loaded"]), int(m["reserve"]), int(m["state"])))
            s.firing_info[addr] = (int(m["rt"]), int(m["fl"]), int(m["cap"]), bool(m["unlimited"]), m["data"].lower())
        elif m := PAIR.search(line):
            s.pairs.add(tuple(sorted((m["a"].lower(), m["b"].lower()))))
        elif m := HELD.search(line):
            s.holders[m["kind"]] += 1
        elif m := ENTITY.search(line):
            active = m["active"].lower()
            s.entities[m["entity"].lower()] = ("A" if active == m["a"].lower() else "B" if active == m["b"].lower()
                                               else "none" if int(active, 16) == 0 else "other")
        elif m := ENTITY_HELD.search(line):
            s.entity_holders[(m["vt"].lower(), m["off"].lower())] += 1
        elif m := BINDING.search(line):
            s.bindings[m["name"]] = m["status"]
        elif m := VIEW_OVERRIDE.search(line):
            s.view_override.append(f"{m['what']}: {m['detail'].strip()}")
    return s


RELOAD_TYPES = {0: "one round at a time", 1: "magazine", 2: "magazine (keeps a chambered round)"}
FIRE_LOGIC = {0: "single", 1: "bolt/pump", 2: "automatic", 3: "burst", 4: "hold-and-release", 5: "detonated"}


def camera_verdict(samples: list[dict]) -> str:
    n = len(samples)
    if n == 0:
        return "no samples"
    rigid = sum(x["rigid00"] for x in samples) / n
    plausible = sum(x["plausible"] for x in samples) / n
    positions = {tuple(round(v, 1) for v in x["t00"]) for x in samples if x["rigid00"]}
    moving = len(positions) > 1
    if rigid >= 0.8 and plausible >= 0.8 and moving:
        verdict = "CONFIRMED: a rigid camera transform that moves, with a plausible projection"
    elif rigid >= 0.8 and plausible >= 0.8:
        verdict = "rigid and plausible but never moved (sampled only in a menu or standing still?)"
    elif rigid >= 0.8:
        verdict = "rigid transform, but FOV/near/far/aspect are not plausible at the expected offsets"
    elif rigid < 0.2:
        verdict = "NOT a camera at +0x00 (the layout guess is wrong for this field)"
    else:
        verdict = "mixed: rigid in some samples only"
    return f"{verdict} ({n} samples, {rigid:.0%} rigid, {plausible:.0%} plausible, {len(positions)} distinct positions)"


def render(s: Summary) -> str:
    out = ["BF4 developer session summary", "=" * 29, ""]

    out.append("Camera (head tracking, RE task R3)")
    if not s.camera and not s.camera_unreadable:
        out.append("  no camera probe lines (not a developer session on the analysed build?)")
    for fld in sorted(set(s.camera) | set(s.camera_unreadable)):
        out.append(f"  [renderer+0x{fld}]: {camera_verdict(s.camera.get(fld, []))}"
                   + (f"; unreadable {s.camera_unreadable[fld]}x" if s.camera_unreadable[fld] else ""))
    if s.slot19:
        out.append(f"  GameRenderer slot 19 getter offset(s): {', '.join('+0x' + x for x in sorted(s.slot19))}")
    out.append("")

    out.append("Session signals (single-player check, RE task R5)")
    if not s.sessions:
        out.append("  no session probe lines")
    for level, mode, mp in s.sessions:
        out.append(f"  level={level or '(empty)'} mode={mode or '(empty)'} isMultiplayer={mp}")
    levels = [lvl for lvl, _, _ in s.sessions]
    frontend = any(lvl.lower().startswith("levels/frontend/") for lvl in levels)
    campaign = [(lvl, mp) for lvl, _, mp in s.sessions if lvl.lower().startswith(("levels/sp/", "levels/sp_"))]
    if s.sessions:
        out.append(f"  front end read as its own level: {'yes' if frontend else 'not seen'}")
        if campaign:
            ok = all(mp in (0, -1) for _, mp in campaign) and any(mp == 0 for _, mp in campaign)
            out.append(f"  campaign level read with isMultiplayer=0: {'CONFIRMED' if ok else 'NO: isMultiplayer was not 0 in campaign'}")
        else:
            out.append("  campaign level: not seen (play past the menus into a mission)")
        if any(mp == -1 for _, _, mp in s.sessions):
            out.append("  isMultiplayer=-1 means a link in its pointer chain was unreadable at that moment")
    out.append("")

    out.append("Weapons (firing census, manual reload research)")
    if not s.censuses:
        out.append("  no census ran")
    else:
        why = Counter(w for w, _, _ in s.censuses)
        out.append(f"  censuses: {len(s.censuses)} ({', '.join(f'{k} {v}' for k, v in why.items())}); "
                   f"most objects in one census: {max(o for _, o, _ in s.censuses)}")
        limited = [a for a, info in s.firing_info.items() if not info[3]]
        out.append(f"  limited-ammo firing objects seen: {len(limited)} (the player's guns are among these)")
        for addr in limited[:8]:
            rt, fl, cap, _, data = s.firing_info[addr]
            series = s.firing[addr]
            changes = " -> ".join(f"{lo}/{re_}" for lo, re_, _ in _dedupe(series))
            out.append(f"    {addr}: {RELOAD_TYPES.get(rt, rt)}, {FIRE_LOGIC.get(fl, fl)}, capacity {cap}; loaded/reserve {changes}")
        out.append(f"  client/server pair candidates: {len(s.pairs)}")
        if s.holders:
            out.append("  holders: " + ", ".join(f"{k} {v}" for k, v in sorted(s.holders.items())))
            both = s.holders.get("server weapon", 0) and (s.holders.get("client weapon A", 0) or s.holders.get("client weapon B", 0))
            out.append(f"  every gun has client and server copies: {'CONFIRMED' if both else 'not shown (see holders)'}")
        if s.entities:
            active = Counter(s.entities.values())
            out.append("  client weapon entities: " + ", ".join(f"active class {k}: {v}" for k, v in sorted(active.items())))
        if s.entity_holders:
            out.append("  next link (what holds a client weapon entity; nearest-vtable guesses, most frequent first):")
            for (vt, off), n in s.entity_holders.most_common(3):
                out.append(f"    vtable +0x{vt} at offset +0x{off}: {n}x")
    out.append("")

    if s.bindings:
        out.append("Bindings: " + ", ".join(f"{k}={v}" for k, v in s.bindings.items()))
    if s.view_override:
        out.append("View override: " + "; ".join(s.view_override))
    return "\n".join(out).rstrip() + "\n"


def _dedupe(series):
    out = []
    for item in series:
        if not out or out[-1][:2] != item[:2]:
            out.append(item)
    return out


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__.strip().splitlines()[2].strip())
        return 2
    text = Path(argv[1]).read_text(encoding="utf-8", errors="replace")
    sys.stdout.write(render(parse(text.splitlines())))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
