"""Read-only BC2 executable inspection. Does not attach or write process memory."""
from pathlib import Path
import argparse,struct,sys,json,re

import pefile,capstone
p=argparse.ArgumentParser();p.add_argument('--exe', type=Path, required=True);p.add_argument('--callers',nargs='*',default=[]);p.add_argument('--disasm',nargs='*',default=[]);p.add_argument('--xrefs',nargs='*',default=[]);p.add_argument('--data',nargs='*',default=[]);p.add_argument('--size',type=lambda x:int(x,0),default=256);a=p.parse_args()
b=a.exe.read_bytes();pe=pefile.PE(data=b);base=pe.OPTIONAL_HEADER.ImageBase
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
def off(v):return pe.get_offset_from_rva(v-base)
for x in a.disasm:
 v=int(x,0);print('DISASM',hex(v))
 for i in md.disasm(b[off(v):off(v)+a.size],v): print(f'{i.address:08x} {i.bytes.hex():24} {i.mnemonic} {i.op_str}')
for x in a.data:
 v=int(x,0);print('DATA',hex(v));o=off(v)
 for j in range(0,a.size,16):print(hex(v+j),b[o+j:o+j+16].hex(),repr(b[o+j:o+j+16]))
for x in a.xrefs:
 v=int(x,0);pattern=struct.pack('<I',v);print('XREFS',hex(v));start=0
 while True:
  k=b.find(pattern,start)
  if k<0:break
  print(hex(base+pe.get_rva_from_offset(k)));start=k+1

for x in a.callers:
 v=int(x,0);print('CALLERS (byte candidates)',hex(v))
 for section in pe.sections:
  if not section.Characteristics&0x20000000:continue
  data=section.get_data();va=base+section.VirtualAddress
  for match in re.finditer(b'[\xe8\xe9]',data):
   k=match.start()
   if k+5<=len(data) and va+k+5+struct.unpack_from('<i',data,k+1)[0]==v:print(hex(va+k))
