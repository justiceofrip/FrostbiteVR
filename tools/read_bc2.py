"""Bounded, read-only process inspection for the BC2 adapter. No write API."""
import ctypes as c,struct,os,json
from ctypes import wintypes as w
from pathlib import Path
k=c.WinDLL('kernel32',use_last_error=True); ps=c.WinDLL('psapi',use_last_error=True)
k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenProcess.restype=w.HANDLE
k.CloseHandle.argtypes=[w.HANDLE]
k.ReadProcessMemory.argtypes=[w.HANDLE,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)];k.ReadProcessMemory.restype=w.BOOL
k.QueryFullProcessImageNameW.argtypes=[w.HANDLE,w.DWORD,w.LPWSTR,c.POINTER(w.DWORD)]
ps.EnumProcessModulesEx.argtypes=[w.HANDLE,c.POINTER(c.c_void_p),w.DWORD,c.POINTER(w.DWORD),w.DWORD];ps.EnumProcessModulesEx.restype=w.BOOL
class MBI(c.Structure):_fields_=[('base',c.c_void_p),('allocation',c.c_void_p),('allocationProtect',w.DWORD),('partition',w.WORD),('size',c.c_size_t),('state',w.DWORD),('protect',w.DWORD),('type',w.DWORD)]
k.VirtualQueryEx.argtypes=[w.HANDLE,c.c_void_p,c.POINTER(MBI),c.c_size_t];k.VirtualQueryEx.restype=c.c_size_t
def configured_executable(config_path=None):
 path=Path(config_path) if config_path is not None else Path(__file__).resolve().parents[1]/'config/local.json'
 config=json.loads(path.read_text(encoding='utf-8-sig'));folder=config.get('game_path')
 if not isinstance(folder,str) or not Path(folder).is_absolute():raise ValueError('Configured game_path must be an absolute installation folder')
 return os.path.abspath(os.path.join(folder,'BFBC2Game.exe'))

def same_executable(actual,expected):
 return os.path.normcase(os.path.abspath(actual))==os.path.normcase(os.path.abspath(expected))

class Process:
 def __init__(self,pid,expected_path=None):
  expected=expected_path if expected_path is not None else configured_executable()
  if not isinstance(expected,(str,os.PathLike)) or not Path(expected).is_absolute():raise ValueError('Expected game executable must be absolute')
  self.h=k.OpenProcess(0x410,False,pid)
  if not self.h:raise OSError(c.get_last_error(),'OpenProcess read')
  try:
   name=c.create_unicode_buffer(32768);n=w.DWORD(len(name))
   if not k.QueryFullProcessImageNameW(self.h,0,name,c.byref(n)) or not same_executable(name.value,expected):raise RuntimeError('Wrong game process')
   modules=(c.c_void_p*2048)();needed=w.DWORD()
   if not ps.EnumProcessModulesEx(self.h,modules,c.sizeof(modules),c.byref(needed),3):raise OSError(c.get_last_error(),'Enumerate modules')
   if needed.value<c.sizeof(c.c_void_p) or needed.value>c.sizeof(modules) or not modules[0]:raise RuntimeError('Invalid game module enumeration')
   self.base=modules[0]
  except BaseException:
   self.close();raise
 def close(self):
  if self.h:k.CloseHandle(self.h);self.h=None
 def read(self,at,n):
  if at<0x10000 or n<=0 or n>1048576 or at+n>0x100000000:raise ValueError('Read out of bounds')
  buf=c.create_string_buffer(n);got=c.c_size_t()
  if not k.ReadProcessMemory(self.h,c.c_void_p(at),buf,n,c.byref(got)) or got.value!=n:raise OSError(c.get_last_error(),'Process read failed')
  return buf.raw
 def u32(self,at):return struct.unpack('<I',self.read(at,4))[0]
 def regions(self):
  at=0x10000
  while at<0x100000000:
   m=MBI()
   if not k.VirtualQueryEx(self.h,c.c_void_p(at),c.byref(m),c.sizeof(m)):break
   if m.state==0x1000 and m.protect&0xff in (2,4,8,0x20,0x40,0x80) and not m.protect&0x100:yield m.base,m.size,m.type,m.protect
   at=m.base+m.size
if __name__=='__main__':
 import argparse
 ap=argparse.ArgumentParser();ap.add_argument('pid',type=int);ap.add_argument('addresses',nargs='+');ap.add_argument('--size',type=lambda x:int(x,0),default=128);a=ap.parse_args();p=Process(a.pid)
 try:
  print('base',hex(p.base))
  for value in a.addresses:
   at=int(value,0);b=p.read(at,a.size);print('OBJECT',hex(at))
   for i in range(0,len(b)-3,4):
    u=struct.unpack_from('<I',b,i)[0];f=struct.unpack_from('<f',b,i)[0]
    print(f'{i:04x} {u:08x} {f:g}')
 finally:p.close()
