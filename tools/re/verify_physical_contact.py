"""Read-only Steam physical-contact contracts (phase 0): broadphase, per-entity primitive
collision, dynamics status, articulated bodies and surface friction. Never attaches."""
import json,struct,sys
from pathlib import Path
from preydll import DEFAULT_MODULE,Image,fnv1a64 as fnv,native_rows
ROOT=Path(__file__).resolve().parents[2]
BASE=0x180000000

def verify():
    image=Image(DEFAULT_MODULE);digest,at=image.sha256,image.at
    entries=native_rows(ROOT/'include/preyvr/ContactNative.h')
    assert len(entries)==11
    relocs=image.relocations()
    checks=[]
    for rva,size,expected,name in entries:
        b=at(rva,size)
        assert fnv(b)==expected,name
        assert not any(rva<=p<rva+size for p in relocs),name+' relocates in memory'
        corrupt=bytearray(b);corrupt[len(b)//2]^=1
        assert fnv(corrupt)!=expected,name+' mutation control'
        checks.append(name+': complete body, no base relocations, mutation refused')
    # The rigid Action (set_velocity, impulse) and GetType are PhysicalNative.h's rows.
    rigid={rva for rva,_,_,_ in native_rows(ROOT/'include/preyvr/PhysicalNative.h')}
    assert {0xbcf990,0x13d1b90}<=rigid
    checks.append('rigid Action 0xBCF990 and GetType 0x13D1B90 hashed by PhysicalNative.h')
    slots=[('world',0x1d82e70,0x110,0xba8560),('world',0x1d82e70,0x1d0,0xba1d80),('world',0x1d82e70,0xd0,0xba9810),
           ('world',0x1d82e70,0xc0,0xbb2a10),
           ('rigid',0x1d83870,0x08,0x13d1b90),('rigid',0x1d83870,0x30,0xbe2bb0),('rigid',0x1d83870,0x38,0xbcf990),
           ('articulated',0x1d803e8,0x08,0xb2d060),('articulated',0x1d803e8,0x30,0xb2c660),('articulated',0x1d803e8,0x38,0xb1cf30)]
    for owner,table,slot,callee in slots:
        assert struct.unpack('<Q',at(table+slot,8))[0]==BASE+callee,(owner,hex(slot))
        checks.append(f'{owner} vtable {table:#x}+{slot:#x} -> {callee:#x}')
    # The articulated class shares the rigid class's inherited slots (CArticulatedEntity : CRigidEntity).
    for slot in (0x10,0x18,0x40,0x48,0x50):
        assert at(0x1d803e8+slot,8)==at(0x1d83870+slot,8),hex(slot)
    checks.append('articulated vtable shares rigid slots +0x10 +0x18 +0x40 +0x48 +0x50')
    witnesses={
        'native melee calls broadphase slot +0x110':(0x13bdd4d,'ff9010010000'),
        'native melee calls per-entity primitive collision slot +0x1d0':(0x13bde06,'ff90d0010000'),
        'broadphase takes the world lock at +0x5784':(0xba8570,'f00fb18b84570000'),
        'broadphase passes caller index 4':(0xba8595,'c744243804000000'),
        "broadphase compares the caller's prealloc with the count":(0xba83f4,'4439bda0010000'),
        "broadphase copies into the caller's buffer":(0xba8538,'488904d1'),
        "collide reads the entity's class byte at +0x44":(0xba1db1,'8b4244'),
        'collide refuses class 5':(0xba1db9,'3d00000005'),
        'collide initialises hit dist to 1e10':(0xba2037,'41c706f9021550'),
        'collide dispatches on itype':(0xba2071,'4585e4'),
        'collide box centre at +0x28':(0xba2143,'f20f104728'),
        'collide sphere centre at +0':(0xba2116,'f20f1007'),
        'collide sets the sweep flag from |dir|^2 > 0':(0xba229d,'0f974520'),
        'collide takes the caller lock at +0x5774':(0xba254f,'f0430fb194a574570000'),
        'collide walks parts: count at +0x128':(0xba258e,'399e28010000'),
        'collide writes collider at hit+8':(0xba2a52,'49897608'),
        'collide writes part index at hit+0x10':(0xba2a59,'41895e10'),
        'collide writes part id at hit+0x14':(0xba2a71,'41894e14'),
        'collide writes surface at hit+0x18':(0xba2a81,'6641894618'),
        'collide writes point at hit+0x24':(0xba2adf,'f2410f114624'),
        'collide writes normal at hit+0x30':(0xba2b18,'f2410f114630'),
        'collide returns dist < 1e9':(0xba2c12,'c1ee1f'),
        'dynamics status v at +0x0c':(0xbe31a8,'f20f11470c'),
        'dynamics status w at +0x18':(0xbe31be,'f20f114718'),
        'dynamics status centre of mass read from body +0x298':(0xbe3542,'f20f108398020000'),
        'dynamics status centre of mass at +0x3c':(0xbe354a,'f20f11473c'),
        'dynamics status mass at +0x4c':(0xbe3567,'89474c'),
        'rigid action set_velocity is type 10':(0xbd2776,'83f80a'),
        'rigid set_velocity writes body v at +0x2d8':(0xbd27ab,'f20f1183d8020000'),
        'rigid set_velocity wakes a sleeping body':(0xbd2bf6,'ff90c0010000'),
        'articulated action names its impulse branch':(0xb1db1f,'488d15ea2b2601'),
        'articulated impulse reads ipart at +0x2c':(0xb1dad9,'8b5f2c'),
        'articulated impulse finds the part by partid at +0x28':(0xb1daf8,'8b5728'),
        'articulated action set_velocity is type 10':(0xb1f273,'83f80a'),
        'surface getter bounds the index at 512':(0xba9813,'81fa00020000'),
        'surface getter bounciness table at world+0x10f0':(0xba9821,'418b848af0100000'),
        'surface getter friction table at world+0x18f0':(0xba982c,'418b848af0180000'),
        'surface setter dynamic friction table at world+0x20f0':(0xbb2a7b,'f3410f118488f0200000'),
        "contact friction sums both surfaces' friction":(0xbe62b2,'f30f108481f0180000'),
        'contact friction second surface':(0xbe62bb,'f30f588491f0180000'),
        'contact friction halves the clamped sum':(0xbe62cf,'f30f590dad940901'),
    }
    for name,(rva,code) in witnesses.items():
        b=bytes.fromhex(code);assert at(rva,len(b))==b,name;checks.append(name)
    def rip(rva,length):return rva+length+struct.unpack('<i',at(rva+length-4,4))[0]
    assert image.cstring(rip(0xb1db1f,7),64)=='CArticulatedEntity:Action(action_impulse)'
    checks.append('articulated impulse branch string is "CArticulatedEntity:Action(action_impulse)"')
    assert struct.unpack('<f',at(rip(0xbe62cf,8),4))[0]==0.25
    checks.append('contact friction scale constant is 0.25: max(0, a+b) * 0.5')
    assert rip(0xbb2a46,8)==0x1d83808 and abs(struct.unpack('<d',at(0x1d83808,8))[0]-2/3)<1e-12
    checks.append('dynamic friction is friction * 2/3')
    assert rip(0xba2ba6,8)==0x1c86478 and struct.unpack('<f',at(0x1c86478,4))[0]==1e9
    checks.append('collide success threshold is dist < 1e9')
    return {'target':str(DEFAULT_MODULE),'sha256':digest,'environment':'static','game_launched':False,'checks':checks,
        'limits':'Proves native bytes, layouts and call routes. Not exercised: query results in a scene, '
                 'overlap depth conventions, lock contention, articulated set_velocity, friction feel or performance.'}

if __name__=='__main__':
    report=json.dumps(verify(),indent=2)+'\n'
    if len(sys.argv)>1:Path(sys.argv[1]).write_text(report,encoding='utf-8',newline='\n')
    print(report)
