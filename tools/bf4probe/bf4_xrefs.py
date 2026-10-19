"""Offline RE over a mapped bf4.exe research image (RVA == file offset).

  python -I -B bf4_xrefs.py <bf4-image.bin> <capstone dir> [--callers-of RVA ...] [--riprefs-of RVA ...] [--disasm RVA[:N] ...]

1. NVAPI wrappers: `mov ecx, imm32` (B9 xx xx xx xx) with a known NVAPI function ID.
2. Wrapper function starts (back-scan to int3/ret padding) and their direct callers
   (E8/E9 rel32), so stereo-gating code can be found from each NVAPI call.
3. RIP-relative references to setting strings ("Stereoscopy", ...).
Read-only analysis of a local file; prints text only.
"""
import struct
import sys

img_path = sys.argv[1]
sys.path.insert(0, sys.argv[2])
import capstone  # noqa: E402

data = open(img_path, "rb").read()
pe = struct.unpack_from("<I", data, 0x3C)[0]
nsec = struct.unpack_from("<H", data, pe + 6)[0]
opt = pe + 24
image_base = struct.unpack_from("<Q", data, opt + 24)[0]
opt_size = struct.unpack_from("<H", data, pe + 20)[0]
sections = []
for i in range(nsec):
    s = opt + opt_size + i * 40
    name = data[s:s + 8].rstrip(b"\0").decode("latin-1")
    vsize, va = struct.unpack_from("<II", data, s + 8)
    chars = struct.unpack_from("<I", data, s + 36)[0]
    sections.append((name, va, vsize, chars))
code = [(n, va, vs) for n, va, vs, c in sections if c & 0x20000000]  # IMAGE_SCN_MEM_EXECUTE

IDS = {
    0x0150e828: "Initialize", 0xd22bdd7e: "Unload", 0x6c2d048c: "GetErrorMessage",
    0x01053fa5: "GetInterfaceVersionString", 0x2926aaad: "SYS_GetDriverAndBranchVersion",
    0xe5ac921f: "EnumPhysicalGPUs", 0x48b3ea59: "EnumLogicalGPUs", 0x4b708b54: "D3D_GetCurrentSLIState",
    0x1e9d8a31: "DISP_GetGDIPrimaryDisplayId", 0xdc6dc8d3: "Mosaic_GetDisplayViewportsByResolution",
    0x33c7358c: "private_33c7358c", 0x593e8644: "private_593e8644", 0xd44d3c4e: "private_d44d3c4e",
    0x239c4545: "Stereo_Enable", 0x2ec50c2b: "Stereo_Disable", 0x348ff8e1: "Stereo_IsEnabled",
    0x296c434d: "Stereo_GetStereoSupport", 0xac7e37f4: "Stereo_CreateHandleFromIUnknown",
    0x3a153134: "Stereo_DestroyHandle", 0xf6a1ad68: "Stereo_Activate", 0x2d68de96: "Stereo_Deactivate",
    0x1fb0bc30: "Stereo_IsActivated", 0x451f2134: "Stereo_GetSeparation", 0x5c069fa3: "Stereo_SetSeparation",
    0x4ab00934: "Stereo_GetConvergence", 0x3dd6b54b: "Stereo_SetConvergence",
    0xe6839b43: "Stereo_GetFrustumAdjustMode", 0x7be27fa2: "Stereo_SetFrustumAdjustMode",
    0xc7177702: "Stereo_InitActivation", 0x0d6c6cd2: "Stereo_Trigger_Activation",
    0x3cd58f89: "Stereo_ReverseStereoBlitControl", 0x6b9b409e: "Stereo_SetNotificationMessage",
    0x96eea9f8: "Stereo_SetActiveEye", 0x5e8f0bec: "Stereo_SetDriverMode", 0xce653127: "Stereo_GetEyeSeparation",
    0x40c8ed5e: "Stereo_IsWindowedModeSupported", 0xf5dcfcba: "Stereo_SetSurfaceCreationMode",
    0x36f1c736: "Stereo_GetSurfaceCreationMode", 0xbe7692ec: "Stereo_CreateConfigurationProfileRegistryKey",
    0xf117b834: "Stereo_DeleteConfigurationProfileRegistryKey", 0x24409f48: "Stereo_SetConfigurationProfileValue",
    0x49bceecf: "Stereo_DeleteConfigurationProfileValue", 0xda044458: "Stereo_DecreaseSeparation",
    0xc9a8ecec: "Stereo_IncreaseSeparation", 0x4c87e317: "Stereo_DecreaseConvergence",
    0xa17daabe: "Stereo_IncreaseConvergence", 0x932cb140: "Stereo_CaptureJpegImage",
    0x8b7e99b5: "Stereo_CapturePngImage", 0xed4416c5: "Stereo_Debug_WasLastDrawStereoized",
    0x44f0ecd1: "Stereo_SetDefaultProfile", 0x624e21c2: "Stereo_GetDefaultProfile",
}

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)


def func_start(rva):
    """Back-scan to the byte after int3/ret padding (heuristic, good for MSVC)."""
    lo = max(0, rva - 0x400)
    for p in range(rva, lo, -1):
        if data[p - 1] == 0xCC and data[p - 2] == 0xCC:
            return p
    return rva


import re  # noqa: E402

_calls = None
_riprefs = None


def _index():
    """One pass: every E8/E9 rel32 target and every RIP-relative lea/mov target."""
    global _calls, _riprefs
    if _calls is not None:
        return
    _calls, _riprefs = {}, {}
    for _, va, vs in code:
        seg = data[va:va + vs]
        for m in re.finditer(rb"[\xe8\xe9]", seg):
            i = m.start()
            if i + 5 > len(seg):
                continue
            tgt = va + i + 5 + struct.unpack_from("<i", seg, i + 1)[0]
            _calls.setdefault(tgt, []).append((va + i, "call" if seg[i] == 0xE8 else "jmp"))
        for m in re.finditer(rb"[\x48\x4c][\x8d\x8b\x89][\x05\x0d\x15\x1d\x25\x2d\x35\x3d]", seg):  # lea/load/store
            j = m.start()
            if j + 7 > len(seg):
                continue
            tgt = va + j + 7 + struct.unpack_from("<i", seg, j + 3)[0]
            _riprefs.setdefault(tgt, []).append(va + j)


def direct_refs(target):
    _index()
    return sorted(_calls.get(target, []))


def rip_refs(target):
    _index()
    return sorted(_riprefs.get(target, []))

def disasm(rva, n=40):
    out = []
    for ins in md.disasm(data[rva:rva + n * 15], image_base + rva):
        out.append(f"  +0x{ins.address - image_base:08x}  {ins.mnemonic:8s} {ins.op_str}")
        if len(out) >= n:
            break
    return "\n".join(out)


args = sys.argv[3:]
MODES = ("--disasm", "--callers-of", "--riprefs-of")
if any(m in args for m in MODES):
    mode = None
    for a in args:
        if a in MODES:
            mode = a
            continue
        rva_s, _, n = a.partition(":")
        rva = int(rva_s, 16)
        if mode == "--disasm":
            print(f"== disasm +0x{rva:x}\n{disasm(rva, int(n) if n else 40)}")
        elif mode == "--riprefs-of":
            refs = rip_refs(rva)
            print(f"== rip refs to +0x{rva:x} ({len(refs)}): " + ", ".join(
                f"+0x{r:x}({'st' if data[r + 1] == 0x89 else 'ld'})" for r in refs))
        else:
            refs = direct_refs(rva)
            print(f"== refs to +0x{rva:x}: " + ", ".join(f"{k}@+0x{r:x}(fn +0x{func_start(r):x})" for r, k in refs))
    sys.exit(0)

print(f"image_base=0x{image_base:x} size={len(data)} sections=" + " ".join(f"{n}@0x{va:x}+0x{vs:x}" for n, va, vs, _ in sections))
found = {}
for _, va, vs in code:
    seg = data[va:va + vs]
    for fid, name in IDS.items():
        pat = b"\xb9" + struct.pack("<I", fid)
        i = seg.find(pat)
        while i != -1:
            found.setdefault(fid, []).append(va + i)
            i = seg.find(pat, i + 1)
print(f"\nNVAPI IDs present as `mov ecx, id`: {len(found)} of {len(IDS)}")
for fid in sorted(found, key=lambda f: IDS[f]):
    for site in found[fid]:
        fs = func_start(site)
        refs = direct_refs(fs)
        print(f"  {IDS[fid]:46s} 0x{fid:08x} at +0x{site:x} wrapper +0x{fs:x} callers={len(refs)} "
              + " ".join(f"+0x{r:x}" for r, _ in refs[:6]))
missing = sorted(IDS[f] for f in IDS if f not in found)
print("absent: " + ", ".join(missing))

print("\nsetting strings and RIP-relative code references:")
for s in (b"Stereoscopy\0", b"StereoConvergence\0", b"GstRender.Stereoscopy\0", b"StereoSeparationScale\0",
          b"StereoConvergenceScale\0", b"StereoSoldierZoomConvergenceScale\0", b"Stereo\0", b"nvapi64.dll\0",
          b"NvAPI_Initialize\0", b"nvapi_QueryInterface\0"):
    pos = []
    i = data.find(s)
    while i != -1 and len(pos) < 8:
        pos.append(i)
        i = data.find(s, i + 1)
    for p in pos:
        xrefs = rip_refs(p)
        print(f"  {s.rstrip(bytes(1)).decode():34s} +0x{p:x} refs=" + " ".join(f"+0x{x:x}" for x in xrefs[:8]))
