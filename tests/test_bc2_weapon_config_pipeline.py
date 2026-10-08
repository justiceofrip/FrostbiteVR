from pathlib import Path
import math,struct,sys,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import bc2_weapon_config_pipeline as p
GUID='12345678-1234-1234-1234-123456789abc'
PART='22345678-1234-1234-1234-123456789abc'

def varint(n):
 out=bytearray()
 while n>=128:out.append((n&127)|128);n>>=7
 out.append(n);return bytes(out)

def n(tag,attrs=None,value='',children=None,kind=2,width=0,count=1):
 return p.Node(tag,attrs or {},p.Value(kind,width,count,value),tuple(children or ()),0)

def encode(root):
 strings=['']
 def add(text):
  if text not in strings:strings.append(text)
 def collect(node):
  add(node.tag)
  for a,b in node.attributes.items():add(a);add(b)
  if node.value.kind==2:add(node.value.data)
  for c in node.children:collect(c)
 collect(root);offsets=[];names=b''
 for text in strings:offsets.append(len(names));names+=text.encode()+b'\0'
 end=24+4*len(strings)+len(names)
 header=b'{binary}'+struct.pack('>4I',end,0,end-24,len(strings))+struct.pack('>'+'I'*len(strings),*offsets)+names
 def emit(item):
  container=item.tag in ('partition','instance','complex') or bool(item.children)
  flags=(item.value.kind<<4)|len(item.attributes)|(128 if container else 0)
  result=varint(strings.index(item.tag))+bytes([flags])
  for key,val in item.attributes.items():result+=varint(strings.index(key))+varint(strings.index(val))
  v=item.value
  if v.kind==2:result+=varint(strings.index(v.data))
  elif v.kind==6:result+=varint(v.count)+v.data
  elif v.kind==7:result+=varint(v.count)+varint(v.width)+v.data
  # Child recursion deliberately uses a separate helper to avoid accidental
  # scalar auto-conversion; bytes are specified explicitly by each test.
  if container:
   for child in item.children:result+=emit(child)
   result+=b'\0'
  return result
 return header+emit(root)

def document(children=(),kind='GameSharedResources.WeaponFiringData'):
 instance=n('instance',{'guid':GUID,'type':kind},children=children)
 return n('partition',{'guid':PART,'primaryInstance':GUID,'exportMode':'All'},children=[instance])

def field_groups(groups):
 result=[]
 for path,fields in groups.items():
  target=result
  for key in path.split('.'):
   existing=next((x for x in target if x.attributes['name']==key),None)
   if existing is None:
    # Fixture containers use mutable lists so nested fields share one parent.
    existing=p.Node('complex',{'name':key},p.Value(2,0,1,''),[],0);target.append(existing)
   target=existing.children
  target.extend(fields)
 return result

class ConfigTests(unittest.TestCase):
 def test_composed_weapon_to_firing_to_function_keeps_exact_owner_and_values(self):
  function_guid='33345678-1234-1234-1234-123456789abc'
  groups={}
  for path,kind in p.SCALARS.items():
   group,key=path.rsplit('.',1)
   if kind=='string':field=n('field',{'name':key},'rtMagazine' if key=='ReloadType' else 'authored_enum')
   elif kind=='bool':field=n('field',{'name':key},b'\0',kind=6,width=1)
   else:field=n('field',{'name':key},struct.pack('>i',30) if kind=='int32' else struct.pack('>f',3.2),kind=7,width=4)
   groups.setdefault(group,[]).append(field)
  function=n('instance',{'guid':function_guid,'type':'GameSharedResources.FiringFunctionData'},children=field_groups(groups))
  firing=n('instance',{'guid':GUID,'type':'GameSharedResources.WeaponFiringData'},children=[n('field',{'name':'PrimaryFire','ref':function_guid}),n('field',{'name':'AbortReloadOnSprint'},b'\0',kind=6,width=1)])
  firing_doc=p.parse('Objects/Firing.dbx',encode(n('partition',{'guid':PART,'primaryInstance':GUID},children=[firing,function])))
  state=n('complex',children=[n('field',{'name':'AnimTree1p','ref':'objects/animation/tree/'+PART}),
      n('field',{'name':'AnimTree3p','ref':'null'}),n('array',{'name':'Meshes1p'},children=[])])
  other_state=n('complex',children=[n('array',{'name':'Meshes1p'},children=[])])
  fields=[n('field',{'name':'Name'},'ExactCampaignAsset'),n('field',{'name':'WeaponClass'},'wcAssault'),n('field',{'name':'WeaponFiring','ref':'objects/firing/'+GUID}),n('array',{'name':'WeaponStates'},children=[state,other_state])]
  weapon_doc=p.parse('Objects/Weapon.dbx',encode(document(fields,'GameSharedResources.SoldierWeaponData')))
  out=p.effective_weapon(p.Resolver([weapon_doc,firing_doc]),weapon_doc,weapon_doc.instances[GUID])
  self.assertEqual(out['native_name'],'ExactCampaignAsset');self.assertEqual(out['fields']['Ammo.MagazineCapacity']['value'],30)
  self.assertAlmostEqual(out['fields']['FireLogic.ReloadTime']['value'],3.2,5);self.assertEqual(out['function_guid'],function_guid)
  self.assertFalse(out['runtime_admitted']);self.assertEqual(out['missing_fields'],[])
  self.assertAlmostEqual(out['fields']['FireLogic.BoltAction.BoltActionTime']['value'],3.2,5)
  self.assertFalse(out['fields']['FireLogic.BoltAction.HoldBoltActionUntilFireRelease']['value'])
  self.assertEqual(out['fields']['FireLogic.FireInputAction']['value'],'authored_enum')
  self.assertEqual(out['weapon_states'][0]['animation_tree_references'],{'AnimTree1p':'objects/animation/tree/'+PART,'AnimTree3p':'null'})
  self.assertEqual(out['weapon_states'][1]['animation_tree_references'],{})
  self.assertIn('AnimTree1p',[m['field'] for m in out['weapon_states'][1]['missing']])
  # Same-named values in another branch must not fill a missing bolt field.
  groups['FireLogic.BoltAction'][:]=[x for x in groups['FireLogic.BoltAction'] if x.attributes['name']!='BoltActionTime']
  groups['FireLogic'].append(n('field',{'name':'BoltActionTime'},struct.pack('>f',99),kind=7,width=4))
  missing_bolt=n('instance',function.attributes,children=field_groups(groups))
  missing_doc=p.parse('Objects/Firing.dbx',encode(n('partition',{'guid':PART,'primaryInstance':GUID},children=[firing,missing_bolt])))
  missing=p.effective_weapon(p.Resolver([weapon_doc,missing_doc]),weapon_doc,weapon_doc.instances[GUID])
  self.assertNotIn('FireLogic.BoltAction.BoltActionTime',missing['fields'])
  self.assertEqual([x['field'] for x in missing['missing_fields']],['FireLogic.BoltAction.BoltActionTime'])
  groups['FireLogic.BoltAction'].append(n('field',{'name':'BoltActionTime'},struct.pack('>f',3.2),kind=7,width=4))
  # A missing value is reported rather than copied from a sibling or default.
  groups['Ammo'][:]=[x for x in groups['Ammo'] if x.attributes['name']!='MagazineCapacity']
  changed=n('instance',function.attributes,children=field_groups(groups))
  changed_doc=p.parse('Objects/Firing.dbx',encode(n('partition',{'guid':PART,'primaryInstance':GUID},children=[firing,changed])))
  out=p.effective_weapon(p.Resolver([weapon_doc,changed_doc]),weapon_doc,weapon_doc.instances[GUID])
  self.assertNotIn('Ammo.MagazineCapacity',out['fields']);self.assertEqual(out['missing_fields'][0]['field'],'Ammo.MagazineCapacity')
 def test_complete_tree_and_scalar_encodings(self):
  children=[n('field',{'name':'ReloadTime'},struct.pack('>f',3.2),kind=7,width=4),n('field',{'name':'Capacity'},struct.pack('>i',30),kind=7,width=4),n('field',{'name':'Abort'},b'\0',kind=6,width=1),n('field',{'name':'ReloadType'},'rtMagazine')]
  doc=p.parse('a.dbx',encode(document(children)));instance=doc.instances[GUID]
  self.assertAlmostEqual(p.scalar(p.named(instance,'ReloadTime'),'float32'),3.2,5)
  self.assertEqual(p.scalar(p.named(instance,'Capacity'),'int32'),30);self.assertFalse(p.scalar(p.named(instance,'Abort'),'bool'));self.assertEqual(p.scalar(p.named(instance,'ReloadType'),'string'),'rtMagazine')
 def test_vector_stays_opaque_not_misread_as_scalar(self):
  node=n('complex',{'name':'Position'},struct.pack('>4f',1,2,3,0),kind=7,width=4,count=4)
  # Binary vector is a leaf, not a children-bearing container.
  raw=encode(document([n('field',{'name':'V'},node.value.data,kind=7,width=4,count=4)]));doc=p.parse('a.dbx',raw)
  with self.assertRaises(ValueError):p.scalar(p.named(doc.instances[GUID],'V'),'float32')
 def test_all_truncations_and_trailing_bytes_reject(self):
  raw=encode(document([n('field',{'name':'Value'},struct.pack('>f',2.8),kind=7,width=4)]))
  for end in range(len(raw)):
   with self.assertRaises((ValueError,struct.error)):p.parse('a.dbx',raw[:end])
  with self.assertRaises(ValueError):p.parse('a.dbx',raw+b'\0')
 def test_wrong_scalar_types_nonfinite_and_boolean_reject(self):
  for node,kind in [(n('field',value=b'\x02',kind=6,width=1),'bool'),(n('field',value=struct.pack('>f',float('nan')),kind=7,width=4),'float32'),(n('field',value=b'\0\0',kind=7,width=2),'int32'),(n('field',value='30'),'int32')]:
   with self.assertRaises(ValueError):p.scalar(node,kind)
 def test_duplicate_and_missing_named_fields_reject(self):
  owner=n('instance',children=[n('field',{'name':'A'}),n('field',{'name':'A'})])
  for key in ('A','B'):
   with self.assertRaises(ValueError):p.named(owner,key)
 def test_instance_guid_primary_and_duplicate_validation(self):
  root=document();root.attributes['primaryInstance']='33345678-1234-1234-1234-123456789abc'
  with self.assertRaises(ValueError):p.parse('a.dbx',encode(root))
  root=document();bad=n('partition',root.attributes,children=[root.children[0],root.children[0]])
  with self.assertRaises(ValueError):p.parse('a.dbx',encode(bad))
 def test_exact_local_external_reference_and_wrong_type(self):
  a=p.parse('Objects/A.dbx',encode(document()));b=p.parse('Objects/B.dbx',encode(document(kind='GameSharedResources.FiringFunctionData')));r=p.Resolver([a,b])
  self.assertEqual(r.resolve(a,GUID,'GameSharedResources.WeaponFiringData'),(a,a.instances[GUID]))
  self.assertEqual(r.resolve(a,'objects/b/'+GUID,'GameSharedResources.FiringFunctionData'),(b,b.instances[GUID]))
  for ref in ('null','objects/missing/'+GUID,'../B/'+GUID,'objects/b/not-guid'):
   with self.assertRaises(ValueError):r.resolve(a,ref,'GameSharedResources.FiringFunctionData')
  with self.assertRaises(ValueError):r.resolve(a,'objects/b/'+GUID,'GameSharedResources.WeaponFiringData')
 def test_guid_does_not_resolve_from_unrelated_document(self):
  a=p.parse('a.dbx',encode(document()));root=document();root.children[0].attributes['guid']='33345678-1234-1234-1234-123456789abc';root.attributes['primaryInstance']=root.children[0].attributes['guid'];b=p.parse('b.dbx',encode(root));r=p.Resolver([a,b])
  with self.assertRaises(ValueError):r.resolve(b,GUID,'GameSharedResources.WeaponFiringData')
 def test_duplicate_resource_path_rejects_even_same_guid(self):
  a=p.parse('a.dbx',encode(document()));b=p.parse('A.dbx',encode(document()))
  with self.assertRaises(ValueError):p.Resolver([a,b])
 def test_varint_overflow_noncanonical_and_array_bounds(self):
  for raw in (b'\x80\0',b'\xff\xff\xff\xff\x7f',b'\x80'):
   with self.assertRaises(ValueError):p.Reader(raw,[''],0).integer()
  raw=bytes([1,0x70])+varint(1000001)+b'\x04'
  with self.assertRaises(ValueError):p.Reader(raw,['','field'],0).node()
 def test_unknown_kind_and_string_reference_reject(self):
  for raw in (b'\x01\x10',b'\x02\x20\0',b'\x01\x20\x02'):
   with self.assertRaises(ValueError):p.Reader(raw,['','field'],0).node()
 def test_nesting_bound(self):
  raw=(b'\x01\xa0\0'*131)+b'\0'*131
  with self.assertRaises(ValueError):p.Reader(raw,['','complex'],0).node()

if __name__=='__main__':unittest.main()
