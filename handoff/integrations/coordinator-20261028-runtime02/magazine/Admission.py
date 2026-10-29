"""Pinned generic metadata admission; current binding is external evidence."""
import hashlib
from pathlib import Path
import sys
sys.dont_write_bytecode = True
upstream = Path(r'G:\Unity\VRMMO\bc2vr-recovery\performance436\generic-magazine-r56-prep\Admission.py')
raw = upstream.read_bytes()
if hashlib.sha256(raw).hexdigest() != '55e23c0d0267cc9f31c45c5d89d18d64cbe6af19c902acbebd462b577ae8f485':
    raise RuntimeError('Pinned generic metadata predicates changed')
source = raw.decode('utf-8-sig')
old = 'HERE = Path(__file__).resolve().parent'
if source.count(old) != 1:
    raise RuntimeError('Generic admission binding anchor changed')
source = source.replace(old, "HERE = Path(r'G:\\Unity\\VRMMO\\FrostbiteVR\\reports\\coordinator-20261028-runtime02\\magazine')")
exec(compile(source, str(upstream), 'exec'), globals())
