Microsoft Detours, exact upstream revision 1dbd4ea281d73d478aa8d09a8b83fbd265f715f7.
Source: https://github.com/microsoft/Detours/tree/1dbd4ea281d73d478aa8d09a8b83fbd265f715f7
The thirteen files under src/ are byte-identical to that revision. CMake builds
the ten upstream Makefile objects; uimports.cpp is included by creatwth.cpp.
No upstream source modifications. LICENSE.md is the upstream MIT license;
the packaged runtime retains its copy in licenses/Detours-MIT.txt.
