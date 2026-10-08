# Licenses and dependencies

Frostbite/BC2-specific code and imported BFVR/2142 math and policy code use the MIT license in `LICENSE` and `licenses/BFVR-MIT.txt`. Original BFVR project: https://github.com/JayBiggsGMG/BFVR-Battlefield-1942-VR-Mod. The Battlefield 2142 adaptation is https://github.com/justiceofrip/BF2142VR. The imported lineage recorded by this workspace is BFVR base `fe1ebc3ba42166c98e42dd217deac8f763e9c256`, adaptation HEAD `0777f8124001addb583f6655989af09f433102af`, including subsequent local changes. This is provenance, not a claim that the current package equals either upstream revision.

MinHook 1.3.4 is compiled statically into the x86 native module. Its BSD licenses, including the HDE disassembler notices, are reproduced in `licenses/MinHook-BSD.txt`; unchanged vendored source retains its copyright headers. Upstream: https://github.com/TsudaKageyu/minhook.

Khronos OpenXR 1.1.61 headers and the x64 loader are used under the retained Apache 2.0 license in `licenses/OpenXR-Apache-2.0.txt`. Header notices also offer MIT as an alternative. The packaged loader SHA-256 is pinned by the packager to `a69729a3348bc4dcb9be126a49d594074e477be7312f65180b0d082bf100fa5f`. Source/header and binary manifests record the exact supplied bytes. Upstream: https://github.com/KhronosGroup/OpenXR-SDK.

The supplied OpenXR loader also contains JsonCpp, copyright (c) 2007-2010 Baptiste Lepilleur and The JsonCpp Authors. Its selected MIT license is retained in `licenses/JsonCpp-MIT.txt`. JsonCpp is compiled into the loader, so there is no separate JsonCpp DLL to install. References: [Khronos loader build](https://github.com/KhronosGroup/OpenXR-SDK/blob/release-1.1.61/src/loader/CMakeLists.txt), [JsonCpp license](https://github.com/open-source-parsers/jsoncpp/blob/1.9.6/LICENSE).

Windows, Direct3D, an OpenXR runtime, Python, and a legally installed copy of Battlefield: Bad Company 2 are external prerequisites. They are not supplied or installed by this package. Current project executables use the static MSVC runtime; the packaged binaries' imported DLL names are recorded in `binary-manifest.json`. The current loader imports only Windows system DLLs. No general Visual C++ redistributable, DirectX redistributable, SteamVR, game assets, or Python runtime is bundled.

Battlefield and Frostbite names identify compatibility. The mod is an independent project and is not an EA/DICE product. The project license does not grant rights to the game or its assets.

The Granny curve format decoder in `tools/bc2_granny_curves.py` adapts quantized curve layouts from Norbyte's LSLib. Its MIT license and copyright are retained in `licenses/LSLib-MIT.txt`. Upstream: https://github.com/Norbyte/lslib/tree/master/LSLib/Granny/Model/CurveData. Game resources used as local calibration input are not redistributed.
