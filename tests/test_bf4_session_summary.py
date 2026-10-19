"""BF4 developer-session log summary (tools/bf4probe/summarize_session.py): verdicts from
lines in the exact formats Bf4Module.cpp writes."""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "bf4probe"))

import summarize_session as ss  # noqa: E402


def camera(field: str, t: str, rigid: int = 1, plausible: int = 1) -> str:
    return (f"10:00:01.000 camera probe: object=000001A2B3C4D5E0 slot19=+0x70 field=+0x{field} view=000001A2B3C4E000 | "
            f"+00 rigid={rigid} t=({t}) fwd=(0.00 0.00 1.00) | +40 rigid={rigid} t=({t}) | +420 rigid=0 | "
            f"fov=55.00 near=0.100 far=1500.0 aspect=1.778 plausible={plausible}")


LOG = [
    "10:00:00.000 binding GameRenderer.Instance: ok",
    "10:00:00.000 binding RenderView.Update: ok",
    camera("60", "10.00 2.00 -5.00"),
    camera("60", "12.50 2.00 -5.00"),
    camera("60", "15.00 2.10 -4.00"),
    camera("70", "0.00 0.00 0.00", rigid=0, plausible=0),
    camera("70", "0.00 0.00 0.00", rigid=0, plausible=0),
    '10:00:02.000 session probe: level="Levels/FrontEnd/FrontEnd" mode="" isMultiplayer=0 (info=1 ctx=2 ctx+28=3 +38=4)',
    '10:00:04.000 session probe: level="Levels/FrontEnd/FrontEnd" mode="" isMultiplayer=0 (info=1 ctx=2 ctx+28=3 +38=4)',
    '10:05:00.000 session probe: level="Levels/SP_Tashgar/SP_Tashgar" mode="SP" isMultiplayer=0 (info=1 ctx=2 ctx+28=3 +38=4)',
    "10:05:30.000 firing census (periodic): 3 objects (2 without unlimited ammo), 3 local matches, 2048MB in 900ms",
    "10:05:30.001   firing 2a0 vt=+0 data=d00 state=3->3 loaded=31 reserve=120 cap=31 mags=4 reloadType=2 fireLogic=2 "
    "mode=0 step=-1 mult=1.00 thr=0.60 time=2.40 flags=00/00",
    "10:05:30.001   firing 2b0 vt=+0 data=d00 state=3->3 loaded=31 reserve=120 cap=31 mags=4 reloadType=2 fireLogic=2 "
    "mode=0 step=-1 mult=1.00 thr=0.60 time=2.40 flags=00/00",
    "10:05:30.001   firing 2c0 vt=+0 data=d10 state=3->3 loaded=12 reserve=999 cap=31 mags=4 reloadType=2 fireLogic=2 "
    "mode=0 step=-1 mult=1.00 thr=0.60 time=2.40 flags=10/00 unlimited",
    "10:05:30.002   pair candidate: 2a0 (vt +0) and 2b0 (vt +0), data d00, loaded 31 reserve 120",
    "10:05:30.003   held: firing 2a0 -> client weapon A 5a0",
    "10:05:30.003   holders of firing 2a0: 1 client weapon, 0 server weapon, 3 other: 1 2 3",
    "10:05:30.004   held: firing 2b0 -> server weapon 6a0",
    "10:05:30.005   client weapon entity 7a0 holds client weapon A 5a0; active (+0x98) = 5a0 (A 5a0, B 4a0)",
    "10:05:30.006   weapon entity 7a0 held at 8f0: nearest vtable 8a0 (vt +1d50000) at -50 (a guess)",
    "10:05:30.006   weapon entity 7a0 held at 9f0: nearest vtable 9a0 (vt +1d50000) at -50 (a guess)",
    "10:05:30.006   weapon entity 7a0 held at af0: nearest vtable aa0 (vt +1c00000) at -30 (a guess)",
    "10:07:30.000 firing census (periodic): 2 objects (2 without unlimited ammo), 2 local matches, 2048MB in 880ms",
    "10:07:30.001   firing 2a0 vt=+0 data=d00 state=3->3 loaded=12 reserve=120 cap=31 mags=4 reloadType=2 fireLogic=2 "
    "mode=0 step=-1 mult=1.00 thr=0.60 time=2.40 flags=00/00",
    "10:07:30.001   firing 2b0 vt=+0 data=d00 state=11->11 loaded=31 reserve=101 cap=31 mags=4 reloadType=2 fireLogic=2 "
    "mode=0 step=-1 mult=1.00 thr=0.60 time=2.40 flags=00/00",
]


class SessionSummaryTests(unittest.TestCase):
    def test_verdicts_from_a_developer_session(self):
        s = ss.parse(LOG)
        text = ss.render(s)
        self.assertIn("[renderer+0x60]: CONFIRMED", text)
        self.assertIn("[renderer+0x70]: NOT a camera", text)
        self.assertIn("slot 19 getter offset(s): +0x70", text)
        self.assertIn("front end read as its own level: yes", text)
        self.assertIn("campaign level read with isMultiplayer=0: CONFIRMED", text)
        self.assertEqual(len(s.sessions), 2)  # repeated identical probe lines collapse
        self.assertIn("limited-ammo firing objects seen: 2", text)
        self.assertIn("2a0: magazine (keeps a chambered round), automatic, capacity 31; loaded/reserve 31/120 -> 12/120", text)
        self.assertIn("client/server pair candidates: 1", text)
        self.assertIn("every gun has client and server copies: CONFIRMED", text)
        self.assertIn("active class A: 1", text)
        self.assertIn("vtable +0x1d50000 at offset +0x50: 2x", text)  # the most frequent guess first
        self.assertIn("GameRenderer.Instance=ok", text)

    def test_missing_evidence_is_said_plainly(self):
        text = ss.render(ss.parse([
            '10:00:02.000 session probe: level="Levels/SP_Prologue/SP_Prologue" mode="SP" isMultiplayer=1 (info=1 ctx=2 ctx+28=3 +38=4)',
            camera("60", "1.00 2.00 3.00"),
            camera("60", "1.00 2.00 3.00"),
        ]))
        self.assertIn("NO: isMultiplayer was not 0 in campaign", text)
        self.assertIn("never moved", text)
        self.assertIn("no census ran", text)
        self.assertIn("front end read as its own level: not seen", text)


if __name__ == "__main__":
    unittest.main()
