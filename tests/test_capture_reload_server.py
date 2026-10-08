import copy
import os
import sys
from pathlib import Path
import struct
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from capture_reload_server import capture, discover
from capture_reload_state import Image

class Memory:
    def __init__(self):self.bytes=bytearray(0x40000);self.reads=[];self.race=None
    def put(self,at,value):struct.pack_into('<I',self.bytes,at-0x10000,value)
    def read(self,at,size):
        self.reads.append((at,size))
        if self.race:self.race(self,at,size)
        if at<0x10000 or size<=0 or at+size>0x50000:raise ValueError('fake read bounds')
        return bytes(self.bytes[at-0x10000:at-0x10000+size])
    def u32(self,at):return struct.unpack('<I',self.read(at,4))[0]

class Inspect:
    def __init__(self):
        self.p=Memory();p=self.p;self.firing_table=0x1425814
        self.binding={'context':0x10000,'manager_table':0x1417680,'controlled_getter':0x56d410}
        self.client=dict(player=0x13000,actor=0x15000,weak=0x29000,flags=11,inventory=0x2a000,selected_slot=0,selected_weapon=0x2b000,items=[0x2b000,0])
        self.weapon_data=dict(weapon=0x2b000,data=0x1f000,firing_data=0x25000,ammo_address=0x24000,asset_name='SPAS12_sp',asset_path='Weapons/Shotguns/SPAS12/SPAS12_sp')
        self.types={0x19000:'ServerSoldierEntity',0x1f000:'SoldierWeaponData',0x21000:'ServerWeaponFiringEffects',0x28000:'WeaponSwitchingData'}
        words={0x10008:0x11000,0x11000:0x1417680,0x11004:2,0x1106c:0x12000,0x13154:0,0x12000:0x17000,
               0x17154:0,0x17000:0x30000,0x30030:0x56d410,0x17c3c:0x19000,0x17c6c:0x19000,
               0x1900c:0x31000,0x1500c:0x31000,0x19220:0x17000,0x192b4:0x1b000,0x1b004:0x28000,
               0x192b8:0x1d000,0x192bc:0x1d008,0x1d000:0x1e000,0x1b14c:0,0x1e004:0x1f000,
               0x1e00c:0x21000,0x1e010:0x23000,0x21010:0x25000,0x21128:0x17000,0x210d4:0x27000,
               0x27004:0x25000,0x23000:self.firing_table,0x23008:0x25000,0x2300c:0x24000,
               0x2303c:11,0x23040:10,0x23044:12,0x2307c:3,0x23080:24}
        for at,v in words.items():p.put(at,v)
        struct.pack_into('<f',p.bytes,0x23050-0x10000,.72)
    def owner(self):return copy.deepcopy(self.client)
    def weapon(self,address,slot):
        if address!=0x2b000 or slot!=0:raise ValueError('client weapon mismatch')
        return copy.deepcopy(self.weapon_data)
    def image_pointer(self,address,size):return address
    def require_type(self,address,wanted):
        if self.types.get(address)!=wanted:raise ValueError('wrong type: '+wanted)
        return {'name':wanted}

class ServerReloadCaptureTests(unittest.TestCase):
    def test_complete_chain_and_no_writes(self):
        i=Inspect();before=bytes(i.p.bytes);r=capture(i,i.binding)
        self.assertEqual(r['state']['loaded'],3);self.assertEqual(r['state']['reserve'],24)
        self.assertEqual(r['server_firing'],0x23000);self.assertEqual(r['server_items_end'],0x1d008)
        self.assertEqual(before,bytes(i.p.bytes));self.assertFalse(r['authority_proven'])
    def test_player_pair_links_reject(self):
        for at,value in [(0x11000,1),(0x11004,257),(0x13154,3),(0x17154,1),(0x30030,0),(0x17c6c,0x19004),(0x19220,0x17004),(0x1900c,0x31004)]:
            i=Inspect();i.p.put(at,value)
            with self.assertRaises(ValueError,msg=hex(at)):capture(i,i.binding)
    def test_span_is_checked_before_storage_read(self):
        for end in [0x1cfff,0x1d003,0x1d104,0x1d000]:
            i=Inspect();i.p.put(0x192bc,end)
            with self.assertRaises(ValueError):capture(i,i.binding)
            self.assertFalse(any(at==0x1d000 for at,size in i.p.reads))
    def test_selected_slot_and_unique_item(self):
        for at,value in [(0x1b14c,2),(0x1b14c,1),(0x1d004,0x1e000),(0x1d000,0)]:
            i=Inspect();i.p.put(at,value)
            with self.assertRaises(ValueError):capture(i,i.binding)
    def test_effects_config_and_firing_ownership(self):
        for at in [0x1e004,0x21010,0x21128,0x27004,0x23000,0x23008,0x2300c]:
            i=Inspect();i.p.put(at,i.p.u32(at)+4)
            with self.assertRaises(ValueError,msg=hex(at)):capture(i,i.binding)
    def test_object_types_reject(self):
        for address in [0x19000,0x1f000,0x21000,0x28000]:
            i=Inspect();i.types[address]='Wrong'
            with self.assertRaises(ValueError):capture(i,i.binding)
    def test_owner_change_between_reads_rejects(self):
        i=Inspect();calls=0
        def race(p,at,size):
            nonlocal calls
            if at==0x10008:
                calls+=1
                if calls==2:p.put(0x1d004,0x32000)
        i.p.race=race
        with self.assertRaisesRegex(ValueError,'ownership changed'):capture(i,i.binding)
    def test_malformed_native_ammo_and_timer(self):
        for at,value in [(0x2303c,16),(0x23080,0xfffffffe),(0x23050,0x7fc00000)]:
            i=Inspect();i.p.put(at,value)
            with self.assertRaisesRegex(ValueError,'state malformed'):capture(i,i.binding)
    @unittest.skipUnless(os.environ.get('FVR_TEST_INSTALLED_BC2') == '1',
                         'Opt in with FVR_TEST_INSTALLED_BC2=1 and a configured game installation')
    def test_exact_installed_file_discovery_and_live_mismatch(self):
        image=Image();b=discover(image)
        self.assertEqual(b['context'],0x1566b18);self.assertEqual(b['manager_table'],0x1417680)
        self.assertEqual(b['proof']['item_count']['rva'],0x296bc0)
        class FileMemory:
            base=image.base
            def read(self,at,size):return image.read(at-image.base,size)
        self.assertEqual(discover(image,FileMemory()),b)
        class Changed(FileMemory):
            def read(self,at,size):
                data=bytearray(super().read(at,size))
                if at==image.base+0x296bc0:data[-1]^=1
                return bytes(data)
        with self.assertRaisesRegex(ValueError,'live code mismatch'):discover(image,Changed())

if __name__=='__main__':unittest.main()
