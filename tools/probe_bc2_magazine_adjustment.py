"""Characterize BC2's loaded-round adjustment in an isolated x86 emulator.

Reads a local executable; never opens a game process or sends input. The result
proves neither server authority nor prediction convergence. Optional research
dependencies: capstone 5.0.6 and unicorn 2.1.4. No executable bytes are exported.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

from capture_reload_state import Image

SIGNATURE = ('56 8B F1 8B 86 94 00 00 00 57 8B 7E 78 03 7C 24 0C '
             '83 F8 FF 75 1F 8B 46 08 8B 40 14 83 F8 FF 89 44 24 0C '
             '75 04 0B C0 EB 0C D9 46 70 DA 4C 24 0C E8 ?? ?? ?? ?? '
             '3B C7 7D 08 5F 89 46 78 5E C2 04 00 89 7E 78 5F 5E C2 04 00')
DESTRUCTOR = ('56 8B F1 C7 06 ?? ?? ?? ?? C7 46 04 ?? ?? ?? ?? '
              '8B 46 18 85 C0 74 0E 3B 46 28 74 09 50 E8 ?? ?? ?? ?? '
              '83 C4 04 8B 4E 14 85 C9 74 0C 8B 01 8B 90 8C 00 00 00 '
              '6A 01 FF D2 F6 44 24 08 01')
SNAPSHOT_EXPORT = '8B 51 3C 8B 44 24 04 89 10 8B 51 44 89 50 04 D9 41 50 D9 58 08 D9 41 48'
SNAPSHOT_RESTORE = '56 57 8B 7C 24 0C 8A 47 39 8B F1 3A 86 A6 00 00 00 74 24 53 8B 5E 18 3B'
REFILL = '51 55 56 8B F1 8B AE 80 00 00 00 85 ED 75 2D F6 86 A8 00 00 00 08 75 24'


def discover(im):
    rva = im.find(SIGNATURE)
    code = im.read(rva, 0x47)
    destructor = im.find(DESTRUCTOR)
    primary, secondary = struct.unpack_from('<I', im.read(destructor, 16), 5)[0], struct.unpack_from('<I', im.read(destructor, 16), 12)[0]
    target = struct.unpack('<I', im.read(secondary-im.base+12, 4))[0]
    if primary != secondary+16 or target != im.base+rva:
        raise ValueError('Firing secondary interface and adjustment slot differ')
    helper = im.base+rva+0x35+struct.unpack_from('<i', code, 0x31)[0]
    if code[0x30] != 0xe8:
        raise ValueError('Rounding call differs')
    snapshots = {}
    for name,pattern,size in (('export',SNAPSHOT_EXPORT,206),('restore',SNAPSHOT_RESTORE,276)):
        at = im.find(pattern)
        snapshots[name] = {'rva':at,'size':size,'sha256':hashlib.sha256(im.read(at,size)).hexdigest()}
    refill = im.find(REFILL)
    return {'rva': rva, 'size': len(code), 'sha256': hashlib.sha256(code).hexdigest(),
            'secondary_vtable_rva': secondary-im.base, 'slot': 3, 'this_adjustment': 4,
            'rounding_helper': helper, 'primary_vtable': primary, 'secondary_vtable': secondary,
            'snapshots':snapshots, 'refill':{'rva':refill,'size':254,'sha256':hashlib.sha256(im.read(refill,254)).hexdigest()}}


class Emulator:
    def __init__(self, im, binding):
        import unicorn as uc
        from unicorn import x86_const as x86
        self.uc, self.x86, self.im, self.binding = uc, x86, im, binding
        self.vm = uc.Uc(uc.UC_ARCH_X86, uc.UC_MODE_32)
        self.vm.mem_map(im.base, (im.size+4095)&~4095)
        for rva, _, raw, size, _ in im.sections:
            self.vm.mem_write(im.base+rva, im.data[raw:raw+size])
        self.object, self.ammo, self.stack, self.stop = 0x10000000, 0x10001000, 0x10010000, 0x10020000
        self.vm.mem_map(self.object, 8192)
        self.vm.mem_map(self.stack, 0x10000)
        self.vm.mem_map(self.stop, 4096)
        self.writes = []
        self.vm.hook_add(uc.UC_HOOK_MEM_WRITE, self._write)

    def _write(self, vm, access, address, size, value, context):
        if self.stack <= address and address+size <= self.stack+0x10000:
            return
        self.writes.append((address, size))

    def call(self, loaded, delta, capacity, reserve=183, *, derived=False):
        x = self.x86
        raw = bytearray([0xA5]*0xb0)
        for at, value in ((0,self.binding['primary_vtable']),(4,self.binding['secondary_vtable']),
                          (0xc,self.ammo),(0x7c,loaded),(0x80,reserve),(0x98,-1 if derived else capacity)):
            struct.pack_into('<I', raw, at, value & 0xffffffff)
        struct.pack_into('<f', raw, 0x74, 1.0)
        self.vm.mem_write(self.object, bytes(raw))
        ammo = bytearray(0x30)
        struct.pack_into('<i', ammo, 0x14, capacity)
        self.vm.mem_write(self.ammo, bytes(ammo))
        self.invoke(self.binding['rva'],self.object+4,delta)
        after = bytes(self.vm.mem_read(self.object,len(raw)))
        if not self.writes or any(w != (self.object+0x7c,4) for w in self.writes):
            raise ValueError(f'Native adjustment wrote outside loaded rounds: {self.writes}')
        checked = bytearray(after); checked[0x7c:0x80] = raw[0x7c:0x80]
        if checked != raw or bytes(self.vm.mem_read(self.ammo,len(ammo))) != ammo:
            raise ValueError('Native adjustment altered unrelated firing/config state')
        return struct.unpack_from('<i',after,0x7c)[0]

    def invoke(self,rva,this,*args):
        x = self.x86
        sp = self.stack+0x8000
        self.vm.mem_write(sp, struct.pack('<'+'I'*(1+len(args)),self.stop,*(arg & 0xffffffff for arg in args)))
        preserved = {x.UC_X86_REG_EBX:0x11111111,x.UC_X86_REG_ESI:0x22222222,
                     x.UC_X86_REG_EDI:0x33333333,x.UC_X86_REG_EBP:0x44444444}
        for reg, value in preserved.items(): self.vm.reg_write(reg,value)
        self.vm.reg_write(x.UC_X86_REG_ECX,this)
        self.vm.reg_write(x.UC_X86_REG_ESP,sp)
        self.vm.reg_write(x.UC_X86_REG_EFLAGS,0x202)
        self.vm.reg_write(x.UC_X86_REG_FPCW,0x37f)
        self.writes.clear()
        self.vm.emu_start(self.im.base+rva,self.stop,count=10000)
        if self.vm.reg_read(x.UC_X86_REG_EIP) != self.stop or self.vm.reg_read(x.UC_X86_REG_ESP) != sp+4*(1+len(args)):
            raise ValueError('Native adjustment did not return with thiscall stack cleanup')
        if any(self.vm.reg_read(reg) != value for reg,value in preserved.items()):
            raise ValueError('Native adjustment changed a callee-preserved register')

    def refill(self,loaded,reserve,capacity,reload_type,*,derived=False,argument=0,magazines=5,reserve_multiplier=1.):
        raw=bytearray([0xA5]*0xb0)
        for at,value in ((0,self.binding['primary_vtable']),(4,self.binding['secondary_vtable']),
                         (8,self.ammo+0x80),(12,self.ammo),(0x7c,loaded),(0x80,reserve),(0x98,-1 if derived else capacity)):
            struct.pack_into('<I',raw,at,value & 0xffffffff)
        for at in (0x74,0x78):struct.pack_into('<f',raw,at,1.)
        struct.pack_into('<f',raw,0x78,reserve_multiplier)
        raw[0xa8]=0
        config=bytearray(0x200)
        struct.pack_into('<ii',config,0x10,magazines,capacity)
        struct.pack_into('<I',config,0xc0,self.ammo+0x100)
        struct.pack_into('<i',config,0x120,reload_type)
        self.vm.mem_write(self.object,bytes(raw));self.vm.mem_write(self.ammo,bytes(config))
        self.invoke(self.binding['refill']['rva'],self.object,argument)
        after=bytes(self.vm.mem_read(self.object,len(raw)))
        if any(at not in (self.object+0x7c,self.object+0x80) or size!=4 for at,size in self.writes):
            raise ValueError('Refill wrote outside the two ammunition counts')
        checked=bytearray(after);checked[0x7c:0x84]=raw[0x7c:0x84]
        if checked!=raw or bytes(self.vm.mem_read(self.ammo,len(config)))!=config:
            raise ValueError('Refill altered unrelated firing/config bytes')
        return struct.unpack_from('<ii',after,0x7c)

    def snapshots(self,loaded,capacity):
        objects = [self.object+n*0x200 for n in range(3)]
        snapshot = self.ammo+0x800
        raw = bytearray(0xb0)
        for at,value in ((0,self.binding['primary_vtable']),(4,self.binding['secondary_vtable']),
                         (0xc,self.ammo),(0x3c,2),(0x40,2),(0x44,2),(0x7c,loaded),(0x80,183),(0x98,capacity)):
            struct.pack_into('<I',raw,at,value)
        struct.pack_into('<f',raw,0x74,1.0)
        for obj in objects: self.vm.mem_write(obj,bytes(raw))
        self.vm.mem_write(snapshot,bytes(0x40))
        server = objects[2]

        def export():
            self.invoke(self.binding['snapshots']['export']['rva'],server,snapshot)
            if any(not(snapshot<=at and at+size<=snapshot+0x40) for at,size in self.writes):
                raise ValueError('Export wrote outside its snapshot')
            return bytes(self.vm.mem_read(snapshot,0x40))

        def restore(obj):
            self.invoke(self.binding['snapshots']['restore']['rva'],obj,snapshot)
            if any(not(obj<=at and at+size<=obj+0xb0) for at,size in self.writes):
                raise ValueError('Restore wrote outside its firing object')

        old_snapshot = export()
        for delta,wanted in ((-loaded,0),(loaded,loaded)):
            self.invoke(self.binding['rva'],server+4,delta)
            if self.writes != [(server+0x7c,4)]: raise ValueError('Adjustment write differs')
            export()
            for obj in objects[:2]: restore(obj)
            expected = bytearray(raw); struct.pack_into('<i',expected,0x7c,wanted)
            if any(bytes(self.vm.mem_read(obj,len(raw))) != expected for obj in objects):
                raise ValueError('Explicit snapshot round trip changed unrelated state')

        # A negative control: a copied pre-removal snapshot restores its OLD
        # rounds. The real adapter must establish prediction/replication ordering;
        # local function success cannot grant persistent removal authority.
        self.invoke(self.binding['rva'],server+4,-loaded)
        self.vm.mem_write(snapshot,old_snapshot)
        restore(objects[0])
        stale = struct.unpack('<i',self.vm.mem_read(objects[0]+0x7c,4))[0]
        if stale != loaded: raise ValueError('Stale snapshot negative control differs')
        return {'capacity':capacity,'rounds':loaded,'explicit_three_copy_round_trip':True,
                'stale_snapshot_restores_old_rounds':True,'replication_scheduling_emulated':False}


def characterize(im):
    binding = discover(im)
    em = Emulator(im,binding)
    cases = []
    for derived in (False,True):
        for capacity in (1,5,6,8,15,20,30,32,50,100,200,1000000):
            for loaded in sorted({1,max(1,capacity//2),capacity}):
                empty = em.call(loaded,-loaded,capacity,derived=derived)
                returned = em.call(empty,loaded,capacity,derived=derived)
                if empty != 0 or returned != loaded:
                    raise ValueError('Native remove/return round trip differs')
                cases.append({'capacity':capacity,'loaded':loaded,'derived_capacity':derived,
                              'after_remove':empty,'after_return':returned,'unrelated_bytes_unchanged':True})
    # Characterize dangerous raw behavior too. Callers must bound signed delta;
    # the native primitive clamps only the upper limit, not zero or overflow.
    unsafe = {'underflow':em.call(5,-6,30),'signed_overflow':em.call(1,0x7fffffff,30),
              'infinite_capacity':em.call(5,0,-1),'upper_clamp':em.call(29,10,30)}
    if unsafe != {'underflow':-1,'signed_overflow':-2147483648,'infinite_capacity':-1,'upper_clamp':30}:
        raise ValueError('Unexpected unsafe-input semantics')
    snapshots = [em.snapshots(loaded,capacity) for capacity in (15,20,30,32,50,100,200)
                 for loaded in (capacity//2,capacity)]
    refills=[]
    for derived in (False,True):
        for capacity in (1,5,8,15,30,32,50,100,200):
            for loaded in sorted({0,capacity//2,capacity-1}):
                for reserve in (0,1,7,183):
                    for reload_type in (0,1):
                        units=min(reserve,1 if reload_type==0 else capacity-loaded)
                        expected=(loaded+units,reserve-units)
                        result=em.refill(loaded,reserve,capacity,reload_type,derived=derived)
                        if result!=expected:raise ValueError('Finite native refill semantics differ')
                        refills.append({'loaded':loaded,'reserve':reserve,'capacity':capacity,'reload_type':reload_type,
                                        'derived_capacity':derived,'units':units,'after':list(result),'unrelated_bytes_unchanged':True})
    # The unused ABI argument cannot select how many rounds to insert.
    multiplier_cases=[]
    for magazines in (0,2,6,8,100):
        for multiplier in (.5,1.,2.,3.,8.):
            for derived in (False,True):
                for reserve in (0,1,7,191):
                    for reload_type in (0,1):
                        units=min(reserve,1 if reload_type==0 else 30)
                        result=em.refill(0,reserve,30,reload_type,derived=derived,
                                         magazines=magazines,reserve_multiplier=multiplier)
                        if result!=(units,reserve-units):raise ValueError('Native reserve multiplier changed finite transfer')
                        multiplier_cases.append(dict(magazines=magazines,multiplier=multiplier,derived_capacity=derived,
                                                     reserve=reserve,reload_type=reload_type,after=list(result)))
    for argument in (0,1,8,0x3f800000,0xffffffff):
        if em.refill(0,60,30,1,argument=argument)!=(30,30):raise ValueError('Unexpected native argument behavior')
    overfull=em.refill(8,24,8,0)
    if overfull!=(9,23):raise ValueError('Per-shell full-capacity negative control differs')
    return {'schema':'fvr.bc2.loaded_adjustment_emulation.v1','read_only':True,
            'game_process_opened':False,'native_gameplay_verified':False,'server_authority_verified':False,
            'prediction_convergence_verified':False,'headset_verified':False,
            'executable_sha256':hashlib.sha256(im.data).hexdigest(),'binding':binding,
            'cases':cases,'unsafe_raw_behavior':unsafe,'snapshot_cases':snapshots,
            'refill_cases':refills,'reserve_multiplier_cases':multiplier_cases,
            'refill_full_shell_unsafe':list(overfull),'refill_argument_unused':True,'passed':True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args = parser.parse_args()
    if args.output.exists(): parser.error('Output exists; preserve prior evidence')
    report = characterize(Image(args.exe))
    args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'passed':report['passed'],'round_trip_cases':len(report['cases']),
                      'explicit_snapshot_cases':len(report['snapshot_cases']),
                      'native_refill_cases':len(report['refill_cases']),
                      'reserve_multiplier_cases':len(report['reserve_multiplier_cases']),
                      'output':str(args.output),'gameplay_verified':False}))


if __name__ == '__main__': main()
