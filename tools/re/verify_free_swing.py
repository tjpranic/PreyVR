"""Read-only Steam free-swing wrench contracts (phase 0): Prey's wrench hit called without its attack,
the artificial collision it sends, stamina, and the surface types the log names. Never attaches."""
import json,struct,sys
from pathlib import Path
from preydll import DEFAULT_MODULE,Image,fnv1a64 as fnv,native_rows
ROOT=Path(__file__).resolve().parents[2]
BASE=0x180000000

def verify():
    image=Image(DEFAULT_MODULE);digest,at=image.sha256,image.at
    entries=native_rows(ROOT/'include/preyvr/SwingNative.h')
    assert len(entries)==7
    relocs=image.relocations()
    checks=[]
    for rva,size,expected,name in entries:
        b=at(rva,size)
        assert fnv(b)==expected,name
        assert not any(rva<=p<rva+size for p in relocs),name+' relocates in memory'
        corrupt=bytearray(b);corrupt[len(b)//2]^=1
        assert fnv(corrupt)!=expected,name+' mutation control'
        checks.append(name+': complete body, no base relocations, mutation refused')
    # What it calls or hooks that other tables already hash, and no body in two tables.
    tables={h:{rva for rva,_,_,_ in native_rows(ROOT/'include/preyvr'/h)}
            for h in ('PhysicalNative.h','SceneQueryNative.h','ContactNative.h')}
    assert {0x13bf730,0x13bd620,0x13bd250,0x16ae410,0x16b1fd0,0x16b2bc0}<=tables['PhysicalNative.h']
    assert 0x157c990 in tables['SceneQueryNative.h'] and {0xba8560,0xba1d80}<=tables['ContactNative.h']
    mine={rva for rva,_,_,_ in entries}
    assert not any(mine&t for t in tables.values())
    checks.append('component hit, GetHits, hit append, impulse wrapper, attack and WeaponHit hashed by PhysicalNative.h; '
                  'player singleton by SceneQueryNative.h; broadphase and collide by ContactNative.h; no row in two tables')
    slots=[('world',0x1d82e70,0x2e8,0xb9f370),
           ('C3DEngine',0x1c912a0,0x460,0x20a540),
           ('CMatMan',0x1c9d0b8,0x98,0x248610),('CMatMan',0x1c9d0b8,0xa0,0x8332a0),
           ('CSurfaceTypeManager',0x1c9d878,0x18,0x24e270),
           ('CMaterialSurfaceType',0x1c9d8b8,0x10,0x16e5d60),('CMaterialSurfaceType',0x1c9d8b8,0x18,0x1170a30),
           ('CMaterialSurfaceType',0x1c9d8b8,0x28,0x24dfe0),('CMaterialSurfaceType',0x1c9d8b8,0x38,0x24dd40),
           ('static',0x1d82188,0x08,0x1a5a880)]
    for owner,table,slot,callee in slots:
        assert struct.unpack('<Q',at(table+slot,8))[0]==BASE+callee,(owner,hex(slot))
        checks.append(f'{owner} vtable {table:#x}+{slot:#x} -> {callee:#x}')
    witnesses={
        # The component hit (0x13BF730): its arguments, the dodge flag, the packages, damage,
        # the collision, breakable surfaces and fatigue.
        'hit tests the dodge flag at component+0x3f':(0x13bf74a,'80793f00'),
        'hit keeps the weapon from R8':(0x13bf74e,'4d8bf0'),
        'hit keeps the component from RCX':(0x13bf756,'4c8be1'),
        'hit keeps the damage scale from XMM3':(0x13bf761,'440f28cb'),
        'hit keeps the angle from XMM1':(0x13bf765,'440f28f9'),
        'hit clears the dodge flag':(0x13bf76b,'c6413f00'),
        'a dodged hit returns 0':(0x13bf76f,'33c0'),
        'hit reads the normal package at +0x00':(0x13bfa47,'498b1c24'),
        'hit reads the charged flag (5th argument)':(0x13bfa50,'4038b590020000'),
        'hit reads the charged critical package at +0x18':(0x13bfa5e,'498b5c2418'),
        'hit reads the charged package at +0x10':(0x13bfa6a,'498b5c2410'),
        'hit reads the critical package at +0x08':(0x13bfa7b,'498b442408'),
        'hit sends the damage package':(0x13bfd50,'e80b8b2400'),
        'hit builds and sends the artificial collision':(0x13bfe4a,'e8d1f02e00'),
        'hit looks up the surface type':(0x13bfe63,'ff5018'),
        'hit asks its breakability (+0x38)':(0x13bfe71,'ff5238'),
        'a breakable surface makes the hit return 0':(0x13bfe7b,'0f4ff1'),
        'fatigue: the player singleton':(0x13bfea3,'e8e8ca1b00'),
        'fatigue: player+0x678':(0x13bfea8,'488d8878060000'),
        'fatigue: its stamina accessor':(0x13bfeaf,'e86c7d1c00'),
        'fatigue is the damage scale times component+0x38':(0x13bfebb,'f3410f594c2438'),
        'fatigue is charged':(0x13bfec5,'e8460eeaff'),
        'stamina accessor returns +0x88':(0x1587c20,'488b8188000000c3'),
        # WeaponHit (0x16B2BC0): how Prey calls the hit.
        'WeaponHit reads the full charge time at +0x528':(0x16b2bcf,'f30f109928050000'),
        'WeaponHit reads the charge time at +0x524':(0x16b2bda,'f30f108124050000'),
        'WeaponHit passes the weapon in R8':(0x16b2be9,'4c8bc7'),
        'WeaponHit reads the charged flag at +0x531':(0x16b2bec,'0fb68731050000'),
        'WeaponHit passes it as the 5th argument':(0x16b2bf3,'88442420'),
        'WeaponHit scales by the charge bonus at +0x52c':(0x16b2c0e,'f30f59992c050000'),
        'WeaponHit calls the component hit':(0x16b2c21,'e80acbd0ff'),
        # The collision builder (0x16AEF20).
        'builder keeps the direction from R8':(0x16aef39,'498be8'),
        'builder loads the collision speed':(0x16aefaf,'f30f1005f1075d00'),
        'builder reads the direction':(0x16aefba,'f30f105500'),
        'builder writes vloc[0] at +0x54':(0x16aefec,'f20f114354'),
        'builder sets iForeignData[0] = 2 (an entity)':(0x16af044,'c7433002000000'),
        'builder tests its dispatch flag':(0x16af08b,'38842488000000'),
        'builder dispatches a collision (type 2)':(0x16af09e,'ba02000000'),
        'builder calls world slot +0x2e8':(0x16af0a6,'ff90e8020000'),
        # AddDeferredEvent (0xB9F370).
        'deferred events take types 0..14':(0xb9f385,'83fa0e'),
        'a collision is logged under pef_log_collisions (0x2000000)':(0xb9f3cd,'ba00000002'),
        # Stamina (0x12618E0).
        'stamina check reads +0x14':(0x12618e6,'f30f104114'),
        'stamina check compares it with +0x10':(0x12618ee,'0f2e4110'),
        'drained returns false':(0x1261913,'32c0'),
        # Surface types.
        'GetMaterialManager returns a global':(0x20a540,'488b0589012302c3'),
        'CMatMan GetSurfaceTypeManager returns +0xe0':(0x8332a0,'488b81e0000000c3'),
        "CMatMan GetSurfaceType jumps to the manager's":(0x248610,'488b89e0000000e9545c0000'),
        'GetSurfaceType bounds ids 1..254':(0x24e274,'8d42ff3dfd000000'),
        'GetSurfaceType table at +0x18':(0x24e286,'488b7cc118'),
        'surface type id at +0x18':(0x16e5d60,'0fb74118c3'),
        'surface type name at +0x08':(0x1170a30,'488b4108c3'),
        'surface type flags at +0x1c':(0x24dfe0,'8b411cc3'),
        'surface type breakability at +0x68':(0x24dd40,'8b4168c3'),
        'the material manager is constructed':(0x2047a2,'e8d9280400'),
        'and stored in its global':(0x2047a7,'488905225f2302'),
        'CMatMan constructor installs its vtable':(0x24708f,'488d052260a501'),
        'CMatMan vtable store':(0x247099,'488901'),
        'CSurfaceTypeManager constructor installs its vtable':(0x24d96f,'488d0502ffa401'),
        'CSurfaceTypeManager vtable store':(0x24d978,'488901'),
        'mat_default is a CMaterialSurfaceType':(0x24d9cd,'488d05e4fea401'),
        'mat_default vtable store':(0x24d9d8,'488903'),
        # The query.
        "Prey's melee broadphase asks for classes 0x117":(0x13bdccd,'c744242017010000'),
        'static GetType returns 1':(0x1a5a880,'b801000000c3'),
        # What a collider is, for the log.
        'hit reads the entity system global':(0x13bf7a1,'4c8b2d90e2e800'),
        'hit asks it for the entity behind the collider (+0xe0)':(0x13bfb18,'ff90e0000000'),
        "hit reads the entity's id (+0x08)":(0x13bfc0a,'ff5008'),
        "builder reads the collider's foreign-data kind (+0x70)":(0x16af056,'ff5070'),
        "load-game message: the entity's class (+0x18)":(0x4c223f,'ff5018'),
        "load-game message: the class's name (+0x10)":(0x4c224c,'ff5210'),
        "load-game message: the entity's id (+0x08)":(0x4c2258,'ff5208'),
        "dispatch message: the entity's class (+0x18)":(0x5618c9,'ff5018'),
        "dispatch message: the class's name (+0x10)":(0x5618d2,'41ff5010'),
        "dispatch message: the entity's name (+0x78)":(0x5618e1,'41ff5078'),
        'the player singleton fetches actor 0x7777':(0x157c9ad,'ba77770000'),
        "the wrench's attack passes its charge bonus to the fatigue setter (XMM2)":(0x16b05a3,'f30f10902c050000'),
        'and its component (RCX)':(0x16b05ab,'488d88c8040000'),
        'and its charged flag (DL)':(0x16b05b2,'0fb69031050000'),
        'and calls the fatigue setter':(0x16b05b9,'e832fad0ff'),
        'the setter keeps the charged flag from DL':(0x13c0009,'0fb6da'),
        'and the bonus from XMM2':(0x13c000c,'0f28f2'),
        'and the component from RCX':(0x13c000f,'488be9'),
        "the hit reads the weapon's entity at +0x40":(0x13bf95d,'498b5e40'),
        "the component's fatigue setter writes +0x38 (phase 2)":(0x13c0058,'f30f114538'),
    }
    for name,(rva,code) in witnesses.items():
        b=bytes.fromhex(code);assert at(rva,len(b))==b,name;checks.append(name)
    def rel(rva,length):return rva+length+struct.unpack('<i',at(rva+length-4,4))[0]
    assert rel(0x13bfea3,5)==0x157c990 and rel(0x13bfeaf,5)==0x1587c20 and rel(0x13bfec5,5)==0x1260d10
    assert rel(0x13bfd50,5)==0x1608860 and rel(0x13bfe4a,5)==0x16aef20 and rel(0x16b2c21,5)==0x13bf730
    checks.append('call targets: singleton 0x157C990, accessor 0x1587C20, StaminaCharge 0x1260D10, '
                  'SendSignalPackage 0x1608860, builder 0x16AEF20, component hit 0x13BF730')
    assert struct.unpack('<f',at(rel(0x16aefaf,8),4))[0]==100.0
    checks.append('collision speed is 100 m/s')
    assert rel(0x16af094,7)==0x224d9c8
    checks.append('the builder dispatches to the physics world (scene query WorldPointer 0x224D9C8)')
    # The switch: rdx = the image base, ecx = the case's RVA from the table at 0xB9FBDC.
    assert rel(0xb9f391,7)==0 and at(0xb9f398,7)==bytes.fromhex('8b8c82dcfbb900')
    assert struct.unpack('<I',at(0xb9fbdc+2*4,4))[0]==0xb9f3c8 and rel(0xb9f3d5,5)==0xb9e160
    checks.append('deferred event case 2 (collision) at 0xB9F3C8 logs it through 0xB9E160')
    assert rel(0x20a540,7)==0x243a6d0 and rel(0x2047a7,7)==0x243a6d0 and rel(0x2047a2,5)==0x247080
    assert rel(0x24708f,7)==0x1c9d0b8 and rel(0x24d96f,7)==0x1c9d878 and rel(0x24d9cd,7)==0x1c9d8b8
    assert rel(0x248610+7,5)==0x24e270
    assert image.cstring(rel(0x24d9e9,7),32)=='mat_default'
    checks.append('material manager global 0x243A6D0 holds a CMatMan (0x1C9D0B8); its surface type manager '
                  '(0x1C9D878) creates mat_default as a CMaterialSurfaceType (0x1C9D8B8)')
    assert image.cstring(rel(0x12618fc,7),32)=='staminaDrained'
    checks.append('stamina check sends the HUD "staminaDrained"')
    assert rel(0x13bf7a1,7)==0x224da38
    assert image.cstring(rel(0x4c225e,7),80).startswith('[LoadGame] Entity class mismatch ID=%d %s')
    assert image.cstring(rel(0x5618e8,7),80)=='No such function dispatch index %d on entity %s (class %s)'
    checks.append('entity system global 0x224DA38; the two messages that print an entity\'s id, name and class')
    return {'target':str(DEFAULT_MODULE),'sha256':digest,'environment':'static','game_launched':False,'checks':checks,
        'limits':'Proves native bytes, layouts and call routes. Not exercised: the hit from the mod\'s thread on a '
                 'character, which classes live skeletons and glass are in, what the collision listeners do with the '
                 'event, stamina values, or how a strike feels.'}

if __name__=='__main__':
    report=json.dumps(verify(),indent=2)+'\n'
    if len(sys.argv)>1:Path(sys.argv[1]).write_text(report,encoding='utf-8',newline='\n')
    print(report)
