# Bf4MakeSignature.py - Ghidra script (Jython 2.7 / PyGhidra compatible).
# Builds the shortest UNIQUE byte pattern starting at the cursor, wildcarding operand
# bytes that hold addresses or branch displacements so the pattern survives relinking.
# Prints a ready-to-paste BindingSpec line for src/games/bf4/Bf4Bindings.cpp.
# @category FrostbiteVR
# @menupath Tools.FrostbiteVR.Make BF4 signature
from ghidra.program.model.lang import OperandType

MAX_BYTES = 64
mem = currentProgram.getMemory()
listing = currentProgram.getListing()
text = mem.getBlock(".text")


def instruction_mask(ins):
    raw = [b & 0xFF for b in ins.getBytes()]
    mask = [1] * len(raw)
    wild = False
    for i in range(ins.getNumOperands()):
        t = ins.getOperandType(i)
        if ins.getOperandReferences(i) or OperandType.isAddress(t) or OperandType.isCodeReference(t) \
                or OperandType.isDataReference(t):
            wild = True
    flow = ins.getFlowType()
    if wild or flow.isCall() or flow.isJump():
        # x64: address/displacement operands are the trailing 4 bytes (rel8 jumps: 1 byte),
        # unless an immediate follows; keep it simple and conservative.
        n = 1 if len(raw) == 2 and flow.isJump() else min(4, len(raw) - 1)
        for k in range(len(raw) - n, len(raw)):
            mask[k] = 0
    return raw, mask


def count_matches(raw, mask, limit=2):
    import jarray
    pat = jarray.array([(b if b < 128 else b - 256) for b in raw], "b")
    msk = jarray.array([(-1 if m else 0) for m in mask], "b")
    found, start = 0, text.getStart()
    while found < limit:
        hit = mem.findBytes(start, text.getEnd(), pat, msk, True, monitor)
        if hit is None:
            break
        found += 1
        start = hit.add(1)
    return found


ins = listing.getInstructionAt(currentAddress)
if ins is None:
    popup("Put the cursor on an instruction in .text")
else:
    raw, mask = [], []
    unique = False
    while ins is not None and len(raw) < MAX_BYTES:
        r, m = instruction_mask(ins)
        raw += r
        mask += m
        if sum(mask) >= 6 and count_matches(raw, mask) == 1:
            unique = True
            break
        ins = ins.getNext()
    pattern = " ".join(("%02X" % b) if m else "??" for b, m in zip(raw, mask))
    fn = getFunctionContaining(currentAddress)
    print("pattern (%d bytes, unique=%s): %s" % (len(raw), unique, pattern))
    print('    {names::kXXX, "%s", BindingKind::Code, 0, 0, 0, "%s @ %s; validated: <how>"},'
          % (pattern, fn.getName() if fn else "?", currentAddress))
    if not unique:
        print("NOT UNIQUE within 64 bytes: start from a different instruction or anchor on a caller.")
