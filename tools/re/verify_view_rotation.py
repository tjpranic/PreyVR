"""Verify Steam ArkPlayer view-rotation and entity-transform contracts offline; never opens a process.

Usage: py -3 tools/re/verify_view_rotation.py [out.json]
PREYVR_PREYDLL overrides the module path (preydll.resolve_module).
Static byte contracts for docs/BODY-YAW-2026-10-11.md: they say what the setter writes,
not that calling it per frame is safe, nor how the body looks in a headset.
"""
import json,struct,sys
from pathlib import Path
from preydll import DEFAULT_MODULE as MODULE,Image
IMAGE_BASE=0x180000000
def verify():
 image=Image(MODULE);digest,at=image.sha256,image.at
 checks=[]
 def proof(name,rva,hexbytes):
  b=bytes.fromhex(hexbytes);assert at(rva,len(b))==b,name;checks.append(name)
 def slot(name,vtable,index,target):
  value=struct.unpack('<Q',at(vtable+index*8,8))[0]
  assert value==IMAGE_BASE+target,(name,hex(value));checks.append(name)
 # ArkPlayer's constructor installs its primary vtable at +0 (RTTI is stripped).
 proof('ArkPlayer ctor stores primary vtable 0x1E56B30 at +0',0x157a43c,
  '488d05edc68d004c896108488bf14c89611048890148')
 # Slot 19 (+0x98): ArkPlayer::Update, R-021's function; TorsoLane hooks its entry.
 slot('vtable slot 19 is ArkPlayer::Update 0x1584FE0',0x1e56b30,19,0x1584fe0)
 proof('ArkPlayer::Update prologue (TorsoLane hook target)',0x1584fe0,
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
 # The concrete CEntity transform (docs/ROOMSCALE-2026-10-11.md). Its vtable is the
 # one holding the hash-gated GetPhysics (SceneQueryNative.h) at +0x228.
 ENTITY=0x1d30bd0
 slot('CEntity +0x228 is the hash-gated GetPhysics 0x9042B0',ENTITY,0x228//8,0x9042b0)
 slot('CEntity +0xE8 is GetWorldTM 0x10EA260',ENTITY,0xe8//8,0x10ea260)
 slot('CEntity +0x108 is SetPos 0x90AF40',ENTITY,0x108//8,0x90af40)
 slot('CEntity +0x110 is GetPos 0x10EA250',ENTITY,0x110//8,0x10ea250)
 slot('CEntity +0x118 is SetRotation 0x90B220',ENTITY,0x118//8,0x90b220)
 slot('CEntity +0x120 is GetRotation 0x9043A0',ENTITY,0x120//8,0x9043a0)
 proof('GetPos returns &m_vPos (+0x38)',0x10ea250,'488d4138c3')
 proof('GetWorldTM returns the matrix at +0x60',0x10ea260,'488d4160c3')
 proof('GetRotation returns &m_qRotation (+0x44)',0x9043a0,'488d4144c3')
 proof('SetPos: unchanged returns; else m_vPos, flags|=2 (POS), transform update +0x350',0x90af40,
  'f30f1002458bd00f2e41387516f30f1042040f2e413c750bf30f1042080f2e41407424f20f10024183ca02f20f114138'
  '450fb6c18b4208418bd2894140488b0148ffa050030000c3')
 proof('SetRotation: m_qRotation, flags|=4 (ROT), transform update +0x350',0x90b220,
  'f30f100a458bc80f2e49447521f30f1042040f2e41487516f30f1042080f2e414c750bf30f10420c0f2e4150742bf30f1149'
  '444183c9048b42044533c08941488b420889414c8b420c418bd1894150488b0148ffa050030000c3')
 # The game's own use: the stance change moves the player entity by SetPos(pos,0,0,0)
 # after a RayWorldIntersection(Game) against the ceiling.
 proof('stance change takes the player entity (+0x38)',0x157b9dc,'4c8b7138')
 proof('stance change calls entity SetPos (+0x108)',0x157bacd,'ff9008010000')
 return dict(target=str(MODULE),sha256=digest,checks=checks,
  evidence='static byte contracts; no in-game or headset acceptance',game_launched=False)
if __name__=='__main__':
 result=json.dumps(verify(),indent=2)
 if len(sys.argv)>1:Path(sys.argv[1]).write_text(result+'\n',encoding='utf-8',newline='\n')
 print(result)
