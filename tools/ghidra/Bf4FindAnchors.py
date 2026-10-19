# Bf4FindAnchors.py - Ghidra script (Jython 2.7 / PyGhidra compatible; no f-strings).
# Lists strings and imports that anchor the BF4 RE tasks (docs/BF4-RE-PLAYBOOK.md),
# with every referencing function, and writes them to JSON for the binding notes.
# @category FrostbiteVR
# @menupath Tools.FrostbiteVR.Find BF4 anchors
import json

from ghidra.program.model.data import StringDataInstance

ANCHORS = {
    "R2 stereo": ["Stereo", "NvAPI_Stereo", "StereoSeparationScale", "StereoConvergenceScale",
                  "StereoSoldierZoomConvergenceScale", "AmdDxExt", "atidxx64", "nvapi64"],
    "R3 camera": ["GameRenderer", "RenderView", "FieldOfView", "fovY", "CameraData", "WorldRender"],
    "R4 reflection": ["SoldierWeaponData", "ShotConfigData", "WeaponFiringData", "TypeInfo", "ClassInfo"],
    "R5 session": ["Levels/SP_", "Levels/MP_", "ClientLevel", "ServerPeer", "pbcl", "PunkBuster"],
    "R6 input": ["ConceptReload", "Reload", "EntryInputAction", "InputConcept"],
    "R7 ui input": ["Scaleform", "UIInput", "MouseX", "Cursor"],
}
IMPORTS = ["GetCursorPos", "SetCursorPos", "ScreenToClient", "GetRawInputData", "RegisterRawInputDevices",
           "DirectInput8Create", "GetAsyncKeyState", "XInputGetState", "CreateDXGIFactory", "CreateDXGIFactory1",
           "D3D11CreateDevice", "D3D11CreateDeviceAndSwapChain", "LoadLibraryA", "LoadLibraryW"]

listing = currentProgram.getListing()
refman = currentProgram.getReferenceManager()
fm = currentProgram.getFunctionManager()


def callers_of(addr, limit=25):
    out = []
    for ref in refman.getReferencesTo(addr):
        fn = fm.getFunctionContaining(ref.getFromAddress())
        out.append({"from": str(ref.getFromAddress()), "function": fn.getName() if fn else None,
                    "entry": str(fn.getEntryPoint()) if fn else None})
        if len(out) >= limit:
            break
    return out


result = {"program": currentProgram.getName(), "image_base": str(currentProgram.getImageBase()),
          "strings": {}, "imports": {}}
for group in ANCHORS:
    result["strings"][group] = []

it = listing.getDefinedData(True)
while it.hasNext() and not monitor.isCancelled():
    data = it.next()
    sdi = StringDataInstance.getStringDataInstance(data)
    if sdi is None or sdi == StringDataInstance.NULL_INSTANCE:
        continue
    value = sdi.getStringValue()
    if not value:
        continue
    for group, needles in ANCHORS.items():
        for needle in needles:
            if needle.lower() in value.lower():
                result["strings"][group].append({"address": str(data.getAddress()), "value": value[:160],
                                                 "refs": callers_of(data.getAddress())})
                break

for sym in currentProgram.getSymbolTable().getExternalSymbols():
    if sym.getName() in IMPORTS:
        thunk_refs = []
        for ref in refman.getReferencesTo(sym.getAddress()):
            thunk_refs.extend(callers_of(ref.getFromAddress(), 10))
        result["imports"][sym.getName()] = {"library": str(sym.getParentNamespace()),
                                            "callers": callers_of(sym.getAddress()) + thunk_refs}

out = askFile("Save BF4 anchors JSON", "Save")
with open(out.getAbsolutePath(), "w") as fh:
    json.dump(result, fh, indent=2)
for group in sorted(result["strings"]):
    print("%-14s %d strings" % (group, len(result["strings"][group])))
print("imports found: " + ", ".join(sorted(result["imports"].keys())))
