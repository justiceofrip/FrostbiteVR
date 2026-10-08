import copy,importlib.util,json,struct,sys,tempfile,unittest
from pathlib import Path
TOOLS=Path(__file__).resolve().parents[1]/'tools'
sys.path.insert(0,str(TOOLS))
import bc2_native_operation_receipts as r
class Tests(unittest.TestCase):
 def fixture(self,root):
  for name in r.ROOTS:
   p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('#include "Common.h"\n',encoding='utf8')
  (root/'src/games/bc2/Common.h').write_text('#include "Bc2VisibilityDescriptorData.h"\n',encoding='utf8')
  (root/'src/games/bc2/Bc2VisibilityDescriptorData.h').write_text('data1',encoding='utf8')
 def test_source_nonrecursive_data_separated(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);self.fixture(root);a=r.source_receipt(root)
   (root/'ignored-receipt.json').write_text(json.dumps(a),encoding='utf8')
   self.assertEqual(a,r.source_receipt(root))
   (root/'src/games/bc2/Bc2VisibilityDescriptorData.h').write_text('data2',encoding='utf8');b=r.source_receipt(root)
   self.assertEqual(a['source_sha256'],b['source_sha256']);self.assertNotEqual(a['excluded_data_sha256'],b['excluded_data_sha256'])
 def test_source_verifies_current_code_and_header(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);self.fixture(root);header=root/'reviewed.h';header.write_text('empty table',encoding='utf8')
   a=r.source_receipt(root,header);receipt=root/'receipt.json';receipt.write_text(json.dumps(a),encoding='utf8')
   self.assertEqual(a,r.verify_source(root,receipt,a['source_sha256'],header))
   header.write_text('changed table',encoding='utf8')
   with self.assertRaises(ValueError):r.verify_source(root,receipt,a['source_sha256'],header)
   header.write_text('empty table',encoding='utf8');(root/r.ROOTS[0]).write_text('changed code',encoding='utf8')
   with self.assertRaises(ValueError):r.verify_source(root,receipt,a['source_sha256'],header)
 def test_missing_and_ambiguous_dependencies_rejected(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);self.fixture(root);(root/r.ROOTS[0]).write_text('#include "missing.h"',encoding='utf8')
   with self.assertRaises(ValueError):r.source_receipt(root)
   (root/r.ROOTS[0]).write_text('#include "Common.h"',encoding='utf8')
   for d in ('one','two'):
    p=root/'src'/d/'Common.cpp';p.parent.mkdir();p.write_text('',encoding='utf8')
   with self.assertRaises(ValueError):r.source_receipt(root)
 def trace(self):
  masks=[0xffffffff,0xffffffff,(1<<12)|(1<<14)|(1<<29),(1<<1)|(1<<4)|(1<<5)|(1<<6)]
  challenge=dict(input=10,native_tick=20,request=1,cache=100,staged=True,committed=True,unrelated_preserved=True,
   before=[0,0,1,0],challenged=[0x3f800000,0x3f800000,masks[2]|1,masks[3]],written=[0,0,1,0],observed=[0,0,1,0])
  row=dict(input=10,native_tick=20,request=1,suppressed=True,free_right=True,hidden=True,blocks_actions=True,paired_free_copies=3,draw_serial=4,phase=6)
  challenge.update(cache=0x70000,native_owner=dict(player=0x10000,soldier=0x20000,weak=0x30000,weapon=0x40000,actor_generation=1,equip_generation=2,space=3),
   physical_owner=dict(actor=(0x30000<<32)|0x20000,actor_generation=1,equip_generation=4,space=3),
   input_observed_ns=1000000000,input_now_ns=1010000000,input_deadline_ns=1100000000,focused=True,tracked=[True,True])
  return {'gameplay':{'body_holster_probe':dict(phase=11,failure=0,diagnostic_profile=4,hidden_request=1,show_request=2,
   player=0x10000,soldier=0x20000,weak=0x30000,weapon=0x40000,actor_generation=1,native_equip_generation=2,physical_equip_generation=4,space=3,
   challenge=challenge,rows=[row,dict(phase=10,hidden=False,right_claim=5,draw_serial=6)]),
   'weapon_visibility_probe':dict(failure=0,paired_private_palette_sequence_verified=True,receipts=[2,2,2]),
   'weapon_visibility_action_suppression':dict(verified_commits=1000)}}
 def test_diagnostics_remain_separate_and_never_admit(self):
  out=r.audit_trace(self.trace());self.assertTrue(out['visibility_paired_palette_diagnostic'])
  self.assertTrue(out['configured_body_hide_show_diagnostic']);self.assertTrue(out['challenged_suppression_diagnostic'])
  self.assertFalse(out['post_draw_fire_cache_diagnostic']);self.assertFalse(out['native_admission_granted']);self.assertFalse(out['actual_shot_verified'])
 def test_neutral_counter_not_suppression_and_wrong_join_rejects(self):
  trace=self.trace();del trace['gameplay']['body_holster_probe'];out=r.audit_trace(trace)
  self.assertEqual(out['neutral_visibility_cache_observations'],1000);self.assertFalse(out['challenged_suppression_diagnostic'])
  trace=self.trace();trace['gameplay']['body_holster_probe']['challenge']['native_owner']['weapon']+=1
  self.assertFalse(r.audit_trace(trace)['challenged_suppression_diagnostic'])
 def test_partial_wrong_words_failure_and_ambiguous_reports(self):
  for change in ('failure','word','proof'):
   trace=self.trace();b=trace['gameplay']['body_holster_probe']
   if change=='failure':b['failure']=1
   if change=='word':b['challenge']['observed'][0]=0x3f800000
   if change=='proof':b['challenge']['committed']=False
   self.assertFalse(r.audit_trace(trace)['challenged_suppression_diagnostic'])
  trace=self.trace();trace['duplicate']={'body_holster_probe':{}}
  with self.assertRaises(ValueError):r.audit_trace(trace)
 def test_duplicate_json_is_not_last_writer_wins(self):
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp)/'bad.json';p.write_text('{"x":1,"x":2}',encoding='utf8')
   with self.assertRaises(ValueError):r.load(p)
 def test_binding_content_code_and_pe_provenance(self):
  raw=bytearray(0x600);raw[:2]=b'MZ';struct.pack_into('<I',raw,0x3c,0x80);raw[0x80:0x84]=b'PE\0\0'
  struct.pack_into('<HH',raw,0x84,0x14c,1);struct.pack_into('<H',raw,0x94,0xe0)
  struct.pack_into('<H',raw,0x98,0x10b);struct.pack_into('<I',raw,0x98+28,0x400000);struct.pack_into('<I',raw,0x98+56,0x2000)
  struct.pack_into('<IIII',raw,0x178+8,0x400,0x1000,0x400,0x200);struct.pack_into('<I',raw,0x178+36,0x20000000)
  key=dict(image_base=0x400000,image_bytes=0x2000,executable_bytes=len(raw),content_fingerprint_fnv64=r.fnv64(raw),
   code=[dict(rva=0x1000+n*16,bytes=4,fingerprint_fnv64=r.fnv64(raw[0x200+n*16:0x204+n*16])) for n in range(4)])
  self.assertTrue(r.verify_binding(raw,key))
  wrong=copy.deepcopy(key);wrong['code'][2]['fingerprint_fnv64']+=1
  with self.assertRaises(ValueError):r.verify_binding(raw,wrong)
  changed=bytearray(raw);changed[-1]=1
  with self.assertRaises(ValueError):r.verify_binding(changed,key)
  with self.assertRaises(ValueError):r.verify_binding(b'MZ',key)
if __name__=='__main__':unittest.main()
