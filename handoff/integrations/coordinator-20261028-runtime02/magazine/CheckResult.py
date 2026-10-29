"""Pinned terminal-status checker; independent native audit remains required."""
import hashlib
from pathlib import Path
import sys
sys.dont_write_bytecode = True
upstream = Path(r'G:\Unity\VRMMO\bc2vr-recovery\performance436\generic-magazine-r56-prep\CheckResult.py')
raw = upstream.read_bytes()
if hashlib.sha256(raw).hexdigest() != '3e15094479211486a8c6ac98dba803d3c59b9c11749f7a0ba234ee31c4dfc3bb':
    raise RuntimeError('Pinned generic terminal checker changed')
exec(compile(raw.decode('utf-8-sig'), str(upstream), 'exec'), globals())
