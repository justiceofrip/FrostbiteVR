Prepared weapon tests now use the existing canonical snapshot receiver.

Actual private79/05 passed baseline binding, then its second snapshot completed
80.020ms after the250ms response deadline. Native publication took239.447ms;
that interval includes unseparated file operations and scheduling, not a proven
specific I/O cause. No controller command was sent; shutdown restored everything.
The inherited92 receiver called timeout twice, aborting its neutral observation
hold. The final prepared class now uses the existing canonical receiver, which
keeps late bytes diagnostic-only and requests a fresh nonce within original bounds.
12 focused checks reproduce the old failure and retain invalid/held/owner/deadline
refusals. Evidence: WT reports/coordinator-20261108-feedback790/snapshot-timeout.
793 also checked the actual prepared + readiness +787 budget composition:
one timeout/discard/fresh request, no double charge and unchanged deadline refusal.
No native source or deadline changed; native79 build receipts remain applicable.
Native publication latency and actual LMG/headset reliability remain unproved.
