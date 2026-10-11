"""Verify Steam ArkPlayer view-rotation contracts offline; never opens a process.

Usage: py -3 tools/re/verify_view_rotation.py [out.json]
PREYVR_PREYDLL overrides the module path (a fork's machine may not use DEFAULT_MODULE).
Static byte contracts for docs/BODY-YAW-2026-10-11.md: they say what the setter writes,
not that calling it per frame is safe, nor how the body looks in a headset.
"""
import hashlib,json,os,struct,sys
from pathlib import Path
from verify_h005_skinning import DEFAULT_MODULE,EXPECTED_SHA256
MODULE=Path(os.environ.get('PREYVR_PREYDLL',DEFAULT_MODULE))
IMAGE_BASE=0x180000000
def verify():
 data=MODULE.read_bytes();digest=hashlib.sha256(data).hexdigest()
 assert digest==EXPECTED_SHA256,digest
 u16=lambda p:struct.unpack_from('<H',data,p)[0]
 u32=lambda p:struct.unpack_from('<I',data,p)[0]
 pe=u32(0x3c);assert u16(pe+4)==0x8664 and u16(pe+24)==0x20b
 sections=[]
 for n in range(u16(pe+6)):
  off=pe+24+u16(pe+20)+n*40
  sections.append((u32(off+12),u32(off+16),u32(off+20)))
 def at(rva,count):
  for va,size,raw in sections:
   if va<=rva and rva+count<=va+size:return data[raw+rva-va:raw+rva-va+count]
  raise ValueError(hex(rva))
 checks=[]
 def proof(name,rva,hexbytes):
  b=bytes.fromhex(hexbytes);assert at(rva,len(b))==b,name;checks.append(name)
 def slot(name,vtable,index,target):
  value=struct.unpack('<Q',at(vtable+index*8,8))[0]
  assert value==IMAGE_BASE+target,(name,hex(value));checks.append(name)
 # ArkPlayer's constructor installs its primary vtable at +0 (RTTI is stripped).
 proof('ArkPlayer ctor stores primary vtable 0x1E56B30 at +0',0x157a43c,
  '488d05edc68d004c896108488bf14c89611048890148')
 # Slot 19 (+0x98): ArkPlayer::Update, R-021's function; BodyFollow hooks its entry.
 slot('vtable slot 19 is ArkPlayer::Update 0x1584FE0',0x1e56b30,19,0x1584fe0)
 proof('ArkPlayer::Update prologue (BodyFollow hook target)',0x1584fe0,
  '488bc455574881ecf800000048895808488bf90fb699851800004c8978e84c8b')
 proof('ArkPlayer::Update reads the context frame time at +0x14',0x15850c7,'f3410f107714')
 # Slot 52 (+0x1A0) and slot 53 (+0x1A8): the IActor SetViewRotation/GetViewRotation pair.
 slot('vtable slot 52 is the view-rotation setter 0x1584CC0',0x1e56b30,52,0x1584cc0)
 slot('vtable slot 53 is the view-rotation getter 0x157CE80',0x1e56b30,53,0x157ce80)
 # Setter: Quat to +0x13F0 (ArkPlayerCamera +0x12A0 m_rotation +0x150), then
 # entity(+0x38)->vtable+0x118(q, 0), then tail-call 0x1327050 on component +0x1468.
 proof('setter copies q to +0x13F0, entity +0x118(q,0), tail-calls component +0x1468',0x1584cc0,
  '48895c2408574883ec208b02488bd98981f01300004533c08b4204488bfa8981f41300008b42088981f8130000'
  '8b420c8981fc130000488b4938488b01ff9018010000488d8b68140000488bd7488b5c24304883c4205fe93423daff')
 proof('getter returns the +0x13F0 quaternion by hidden pointer',0x157ce80,
  '8b81fc13000089420c8b81f013000089028b81f41300008942048b81f8130000894208488bc2c3')
 proof('component +0x1468 keeps its own rotation copy at +0x08',0x1327050,
  '8b028941088b420489410c8b42088941108b420c894114c3')
 # Entity rotation events copy the entity rotation (vtable +0x120) back into +0x13F0.
 proof('entity rotation event syncs +0x13F0 from entity GetRotation',0x1580ae4,
  '488b4e38488b01ff90200100008b08898ef01300008b4804898ef41300008b4808898ef81300008b400c8986fc13')
 proof('player event rebuilds rotation from entity and #camera attachment',0x1580ee7,
  '488b5e38488d4c2448e86b80f0ff488b13488bcb488bf8ff9220010000')
 proof('save game serialises +0x13F0 as "rotation"',0x157c490,'4d8d86f01300004533c9488d153f1c7400488b01ff90c8000000')
 proof('"rotation" name string',0x1cbe0e0,'726f746174696f6e00')
 proof('UpdateView reads the camera +0x150 quaternion',0x148b927,'4881c750010000')
 return dict(target=str(MODULE),sha256=digest,checks=checks,
  evidence='static byte contracts; no in-game or headset acceptance',game_launched=False)
if __name__=='__main__':
 result=json.dumps(verify(),indent=2)
 if len(sys.argv)>1:Path(sys.argv[1]).write_text(result+'\n',encoding='utf-8',newline='\n')
 print(result)
