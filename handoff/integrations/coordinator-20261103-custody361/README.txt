Shared held-ammunition repair351, finalized361 after independent355/359 review.
Actual348 showed FixedRaw/RawInvalid dropping a still-fresh tracked AmmoObject without a controller release.
The fixed-container adapter now retains the exact held ammunition across missing rendered contact when current hand safety, native resource identity and same-gun ownership remain valid. Missing geometry performs no motion or native dispatch.
Fresh recorded geometry may advance an active insertion even when current safety has already renewed its hand claim. Geometry keeps its original clocks/history; actual release and native receipts still own completion.
Recurring delayed publications no longer starve motion. Preserved endpoint dwell expires before a new source deadline replaces the old one, including the one-missing-tick boundary identified by359.
Four changed files: Bc2PhysicalFixedContainer.cpp, FixedContainerReload.h and their two existing test suites. No weapon-name branch, native ammunition operation, origin write or muzzle enrollment changes.
Source/static checks passed. Regression coverage includes84 fixed-loader profiles, four-kind gap recovery,13 adapter invalidations,40 recurring delayed pairs, gesture expiry/recovery and original geometry lease boundaries.
Required C++ validation belongs to the next normal x86/x64 builds once each; no separate isolated run or future pass is claimed here.
Candidate patch19145906e5808dc71f980de15cde221c28458ac079790f307fb8ce6f6525d06b; exact source/application/review pins in reports/coordinator-20261103-feedback361. All workers retired.
Normal354 subsequently passed345x86/344x64 on7c9899c. Delivery was held after367 identified the separate first-pickup boundary; combined370 supersedes this candidate for installation. Private58 remains installed. No completed native launcher reload, all-weapon or headset acceptance follows from these checks.
