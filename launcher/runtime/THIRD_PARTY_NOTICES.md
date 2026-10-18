# Portable runtime dependencies

BC2 VR is independent software, not an EA/DICE product. Battlefield and Frostbite names identify compatibility. No game executable, sound, mesh, texture, or animation is included. Body equipment and short audio cues are generated locally from the player's owned game and must not be redistributed.

The framework and BFVR-derived code use MIT; notices are in `licenses/BFVR-MIT.txt` and the package's `LICENSE`. MinHook is statically linked under its retained BSD notices in `licenses/MinHook-BSD.txt`. OpenXR loader and headers use Apache 2.0 (`licenses/OpenXR-Apache-2.0.txt`); its bundled JsonCpp uses MIT (`licenses/JsonCpp-MIT.txt`). The Granny curve parser adapts Norbyte LSLib layouts under MIT (`licenses/LSLib-MIT.txt`).

The portable Python distribution retains its included license. Its source and release information are at https://www.python.org/downloads/windows/ . Supplied dependency hashes are recorded in `runtime.json`.

FFmpeg is not included in this package. First-time asset preparation downloads a pinned, unmodified LGPL shared build directly from https://github.com/BtbN/FFmpeg-Builds and retains its license beside the executable. The provider is listed at https://ffmpeg.org/download.html . The exact upstream release, archive and retained file hashes are recorded in `config/ffmpeg-download.json`; the local cache receipt records that recipe. Downloads and extracted files stay on the chosen installation drive. FFmpeg is used only to decode audio from the player's installed game. Setup validation and launches with prepared assets do not download it.

If compatible PowerShell is not installed, the installer downloads a hash-pinned portable Microsoft release to the selected data drive. PowerShell is MIT licensed; the downloaded release contains its license and third-party notices. Source: https://github.com/PowerShell/PowerShell . No system-wide Python, FFmpeg, PowerShell, DirectX or Visual C++ runtime installation is performed.

Windows, SteamVR/an OpenXR runtime, a connected headset, and an owned compatible Battlefield: Bad Company 2 installation are external prerequisites. The executables use the static MSVC runtime.
