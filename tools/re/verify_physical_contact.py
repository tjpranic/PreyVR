"""Read-only Steam physical-contact contracts (phases 0, 4 and 5): broadphase, per-entity primitive
collision, dynamics status, articulated bodies and their parts, surface friction, and what the
player stands on. Never attaches."""
import json,struct,sys
from pathlib import Path
from preydll import DEFAULT_MODULE,Image,fnv1a64 as fnv,native_rows
ROOT=Path(__file__).resolve().parents[2]
BASE=0x180000000

def verify():
    image=Image(DEFAULT_MODULE);digest,at=image.sha256,image.at
    entries=native_rows(ROOT/'include/preyvr/ContactNative.h')
    assert len(entries)==19
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
           ('articulated',0x1d803e8,0x08,0xb2d060),('articulated',0x1d803e8,0x30,0xb2c660),('articulated',0x1d803e8,0x38,0xb1cf30),
           ('articulated',0x1d803e8,0x28,0xb2aa00),('articulated',0x1d803e8,0x100,0xb37960),('articulated',0x1d803e8,0xd8,0xb398a0),
           ('living',0x1d81580,0x08,0x335000),('living',0x1d81580,0x30,0xb475d0),('living',0x1d81580,0x38,0xb44e60),
           ('static',0x1d82188,0x30,0xb83040),('living',0x1d81580,0x28,0xb46aa0),('static',0x1d82188,0x28,0xb81a20)]
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
        # Phase 4: one part of an articulated body.
        "collide's part id is the part's id at parts+0x48":(0xba2a66,'488b8620010000'),
        'collide copies part id +0x48':(0xba2a6d,'8b4c3848'),
        'articulated set_velocity reads ipart at +4':(0xb1f289,'8b4704'),
        'articulated set_velocity reads partid at +8':(0xb1f29d,'8b5708'),
        'articulated set_velocity walks parts at +0x120':(0xb1f3e6,'488b8e20010000'),
        'articulated set_velocity matches part id +0x48':(0xb1f3ed,'4883c148'),
        'articulated parts are 0x88 apart':(0xb1f3f7,'4881c188000000'),
        'articulated set_velocity bounds the part by count +0x128':(0xb1f403,'3b8628010000'),
        'articulated part infos are 0x94 apart':(0xb1f416,'4869c894000000'),
        'articulated part infos at +0x458':(0xb1f425,'488b8658040000'),
        "articulated part info's joint at +0x54":(0xb1f42c,'4c63440154'),
        'articulated joints at +0x460':(0xb1f443,'488b8e60040000'),
        'articulated joints are 0x330 apart':(0xb1f44f,'4969d030030000'),
        "articulated set_velocity writes the joint body's v at +0x1f0":(0xb1f456,'f20f118411f0010000'),
        "articulated set_velocity writes the joint body's w at +0x1e4":(0xb1f4fa,'f20f118411e4010000'),
        "articulated set_velocity marks the joint moving at +0x1a0":(0xb1fc50,'4489b410a0010000'),
        'articulated set_velocity skips the wake when awake (+0x1a4)':(0xb1fc5d,'80bea401000000'),
        'articulated set_velocity wakes the body (+0x1c0)':(0xb1fc85,'ff90c0010000'),
        'articulated dynamics status reads partid at +4':(0xb2c86d,'448b4a04'),
        'articulated dynamics status reads ipart at +8':(0xb2c8b8,'448b4308'),
        'articulated dynamics status: no such part is the whole body':(0xb2c8c4,'443b8728010000'),
        'articulated dynamics status: the part joint':(0xb2cb58,'48634c0154'),
        "articulated dynamics status: the joint body's inverse mass +0x200":(0xb2cba9,'f30f10841100020000'),
        "articulated dynamics status: the joint body's inverse inertia +0x240":(0xb2ce5c,'f30f59841140020000'),
        "articulated dynamics status: centre of mass from the joint body +0x1b0":(0xb2cf1b,'f20f108411b0010000'),
        "articulated dynamics status: mass from the joint body +0x1fc":(0xb2cf3c,'8b8411fc010000'),
        'articulated GetParams handles articulated_body (type 8)':(0xb2b0a1,'83f808'),
        'articulated GetParams reads the host at +0x4f8':(0xb2b12a,'488b87f8040000'),
        'articulated GetParams returns the host as pHost (+0x58)':(0xb2b131,'48894358'),
        'articulated step runs the joint pass':(0xb38f45,'e8b6300000'),
        "the joint pass reads the joint body's v":(0xb3c26d,'f20f108431f0010000'),
        "the joint pass saves it as the joint's v (+0x88)":(0xb3c276,'f20f11843188000000'),
        "StepBack restores the joint body's v from it":(0xb39a98,'f20f118419f0010000'),
        # Phase 5: the player's ground collider (pe_status_living).
        'living GetType returns 4':(0x335000,'b804000000c3'),
        'living GetStatus asks the base GetStatus first':(0xb477a3,'e898b80300'),
        'living GetStatus handles pe_status_living (type 2)':(0xb477b8,'83f802'),
        'living status takes the read lock at +0x348':(0xb477c8,'f00fc18348030000'),
        'living status reads the flags at +0x2c0':(0xb477ed,'8b83c0020000'),
        'living status writes bFlying at +4':(0xb477fc,'894f04'),
        "living status adds the ground's velocity when +0x2c8 is set":(0xb478e2,'4883bbc802000000'),
        'living status reads the ground collider at +0x2c8':(0xb4799c,'488b83c8020000'),
        'living status writes the ground collider at +0x70':(0xb479a3,'48894770'),
        'living pos status reads the position at +0xa8':(0xb47643,'f3440f1093a8000000'),
        'living pos status reads the pivot height at +0x1d0':(0xb47679,'f30f10bbd0010000'),
        'living pos status reports the shifted position':(0xb47763,'f20f1183a8000000'),
        'living status camOffset from the pivot height +0x1d0':(0xb47808,'f3440f108bd0010000'),
        'living status negates it (down from the pivot)':(0xb4782c,'440f570d3c731301'),
        'living status writes camOffset at +0x0c':(0xb478a5,'f20f11470c'),
        'living GetParams asks the base GetParams first':(0xb46ab0,'e86baf0300'),
        'living GetParams handles pe_player_dimensions (type 1)':(0xb46abf,'83f801'),
        'living dimensions take the read lock at +0x348':(0xb46ac8,'f00fc18348030000'),
        'living dimensions read heightPivot at +0x1c0':(0xb46aed,'8b83c0010000'),
        'living dimensions write heightPivot at +4':(0xb46af3,'894704'),
        'living dimensions read sizeCollider at +0x1c4':(0xb46af6,'f20f1083c4010000'),
        'living dimensions write sizeCollider at +0x0c':(0xb46afe,'f20f11470c'),
        'living dimensions write bUseCapsule (flags bit 4) at +0x34':(0xb46b4e,'894f34'),
    }
    for name,(rva,code) in witnesses.items():
        b=bytes.fromhex(code);assert at(rva,len(b))==b,name;checks.append(name)
    def rip(rva,length):return rva+length+struct.unpack('<i',at(rva+length-4,4))[0]
    assert image.cstring(rip(0xb1db1f,7),64)=='CArticulatedEntity:Action(action_impulse)'
    assert image.cstring(rip(0xb44edf,7),64)=='CLivingEntity:Action(action_move)'
    checks.append('the living vtable\'s Action names itself "CLivingEntity:Action(action_move)"')
    checks.append('articulated impulse branch string is "CArticulatedEntity:Action(action_impulse)"')
    assert struct.unpack('<f',at(rip(0xbe62cf,8),4))[0]==0.25
    checks.append('contact friction scale constant is 0.25: max(0, a+b) * 0.5')
    assert rip(0xbb2a46,8)==0x1d83808 and abs(struct.unpack('<d',at(0x1d83808,8))[0]-2/3)<1e-12
    checks.append('dynamic friction is friction * 2/3')
    assert rip(0xba2ba6,8)==0x1c86478 and struct.unpack('<f',at(0x1c86478,4))[0]==1e9
    checks.append('collide success threshold is dist < 1e9')
    return {'target':str(DEFAULT_MODULE),'sha256':digest,'environment':'static','game_launched':False,'checks':checks,
        'limits':'Proves native bytes, layouts and call routes. Not exercised: query results in a scene, '
                 'overlap depth conventions, lock contention, how a ragdoll responds to a per-part velocity, '
                 'which characters carry a host, friction feel or performance.'}

if __name__=='__main__':
    report=json.dumps(verify(),indent=2)+'\n'
    if len(sys.argv)>1:Path(sys.argv[1]).write_text(report,encoding='utf-8',newline='\n')
    print(report)
