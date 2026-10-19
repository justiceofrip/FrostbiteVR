"""Offline Frostbite reflection reader over a mapped bf4.exe research image (RVA == file offset).

  python -I -B bf4_reflection.py <bf4-image.bin> FieldName [FieldName ...]

For each field name: finds its FieldInfoData entries ({char* name; u16 flags; u16 offset;
u32 pad; TypeInfo* type}, stride 0x18), walks the whole field array it belongs to, and
names the owning class when a ClassInfoData points at the array. Read-only, text output.
"""
import struct
import sys

BASE = 0x140000000
d = open(sys.argv[1], "rb").read()


def rva(va):
    r = va - BASE
    return r if 0 < r < len(d) - 8 else None


def cstr(va, limit=96):
    r = rva(va)
    if r is None:
        return None
    e = d.find(b"\0", r, r + limit)
    s = d[r:e]
    return s.decode("latin-1") if e > r and all(32 <= c < 127 for c in s) else None


def type_name(tptr):
    r = rva(tptr)
    if r is None:
        return None
    data = struct.unpack_from("<Q", d, r)[0]
    dr = rva(data)
    return cstr(struct.unpack_from("<Q", d, dr)[0]) if dr is not None else None


def field_at(r):
    name = cstr(struct.unpack_from("<Q", d, r)[0], 64)
    if not name:
        return None
    flags, off = struct.unpack_from("<HH", d, r + 8)
    t = struct.unpack_from("<Q", d, r + 16)[0]
    tn = type_name(t)
    if tn is None:
        return None
    return name, flags, off, tn


def owning_class(array_rva):
    needle = struct.pack("<Q", BASE + array_rva)
    i = d.find(needle)
    hits = []
    while i != -1 and len(hits) < 4:
        for back in range(8, 0x60, 8):  # ClassInfoData starts with its name pointer
            n = cstr(struct.unpack_from("<Q", d, i - back)[0], 64)
            if n and n[0].isupper() and " " not in n:
                hits.append(n)
                break
        i = d.find(needle, i + 1)
    return hits


def is_array_start(r):
    return d.find(struct.pack("<Q", BASE + r)) != -1


def owner_candidates(array_rva):
    """ClassInfoData/EnumInfoData point at their field array; the type name pointer sits
    a little before that pointer. Report every identifier found in the 0x60 bytes before."""
    i = d.find(struct.pack("<Q", BASE + array_rva))
    out = []
    while i != -1 and len(out) < 3:
        for back in range(8, 0x68, 8):
            n = cstr(struct.unpack_from("<Q", d, i - back)[0], 64)
            if n and (n[0].isupper() or n[0].islower()) and " " not in n and "/" not in n:
                out.append(f"{n}(-0x{back:x})")
        i = d.find(struct.pack("<Q", BASE + array_rva), i + 1)
    return out


seen = set()
for want in sys.argv[2:]:
    off = d.find(want.encode() + bytes(1))
    needle = struct.pack("<Q", BASE + off)
    i = d.find(needle)
    while i != -1:
        if field_at(i) and i not in seen:
            start = i
            while not is_array_start(start) and field_at(start - 0x18):
                start -= 0x18
            end = start
            while field_at(end + 0x18) and not is_array_start(end + 0x18):
                end += 0x18
            for r in range(start, end + 0x18, 0x18):
                seen.add(r)
            print(f"== array +0x{start:x} ({(end - start) // 0x18 + 1} fields) owner? {owner_candidates(start)[:4]}")
            rows = sorted((field_at(r) for r in range(start, end + 0x18, 0x18)), key=lambda f: f[2])
            for name, flags, foff, tn in rows:
                print(f"   +0x{foff:04x}  {name:40s} {tn}")
        i = d.find(needle, i + 1)
