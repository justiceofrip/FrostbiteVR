# Opening knife acceptance failure

The user's 0.1.10 run did not prepare any selected-knife VR attack and did not
allow progress through the opening encounter. It ended with probe exit
`0xC0000096` before the authorized stop; the final report is empty. The periodic
telemetry and host logs were preserved without copying game assets or binaries.
No crash cause is established.

Correction to the older checkpoint428 wording: `cause: 1` in selected-knife
records is `SelectedMeleeCause::Stroke`, not a native damage cause. The previous
bounded hit and narrative response did not establish two kills or objective
progression. The immediate health observation is inside the damage callback,
before the rest of the native attack completes.

Investigate repeated native evaluation of one controller packet and expose
recognizer rejection reasons in periodic telemetry. Any fix must retain exact
hand/actor ownership and one outstanding native attack. Duplicate render or
input evaluations must neither manufacture movement nor destroy legitimate
physical motion history merely because derived rendering arithmetic changed.
