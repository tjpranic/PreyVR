"""Read-only Steam weapon draw contracts: where Prey builds and plays a weapon's select action, what that
action's Enter and Exit do for the weapon, why the finish waits a frame, and what each weapon class changes;
and what plays the draw's sound (the PlaySound clip, the trigger lookup, the entity audio proxy). Never attaches."""
import json,re,struct,sys,zlib
from pathlib import Path
from preydll import DEFAULT_MODULE,Image,fnv1a64 as fnv,native_rows
ROOT=Path(__file__).resolve().parents[2]
BASE=0x180000000

def verify():
    image=Image(DEFAULT_MODULE);digest,at=image.sha256,image.at
    entries=native_rows(ROOT/'src/equipment/weapon/WeaponDrawNative.h')
    assert len(entries)==17
    relocs=image.relocations()
    checks=[]
    for rva,size,expected,name in entries:
        b=at(rva,size)
        assert fnv(b)==expected,name
        assert not any(rva<=p<rva+size for p in relocs),name+' relocates in memory'
        corrupt=bytearray(b);corrupt[len(b)//2]^=1
        assert fnv(corrupt)!=expected,name+' mutation control'
        checks.append(name+': complete body, no base relocations, mutation refused')
    # SelectItem, the weapon lookup and the player are EquipmentNative.h's; no body in two tables.
    tables={h:{rva for rva,_,_,_ in native_rows(next((ROOT/'src').rglob(h)))}
            for h in ('EquipmentNative.h','PhysicalNative.h','SwingNative.h','ContactNative.h','SceneQueryNative.h')}
    assert {0x12748d0,0x16a5650,0x157c990}<=tables['EquipmentNative.h']
    for rva,size,expected,name in native_rows(ROOT/'src/equipment/body/EquipmentNative.h'):
        if rva in (0x12748d0,0x16a5650,0x157c990):assert fnv(at(rva,size))==expected,name
    mine={rva for rva,_,_,_ in entries}
    assert not any(mine&t for t in tables.values())
    checks.append('SelectItem 0x12748D0, the weapon lookup 0x16A5650 and the player 0x157C990 hashed by '
                  'EquipmentNative.h (their rows hold); no row in two tables')
    slots=[('wrench',0x1e92f00,0x08,0x1699800),('wrench',0x1e92f00,0x100,0x1692e60),
           ('wrench',0x1e92f00,0x108,0x1692e70),('wrench',0x1e92f00,0x110,0x1692f50),
           ('weapon base',0x1e6c640,0x08,0x1699800),('weapon base',0x1e6c640,0x100,0x1692e60),
           ('weapon base',0x1e6c640,0x108,0x1692e70),('weapon base',0x1e6c640,0x110,0x1692f50),
           ('select action',0x1e6d698,0x10,0x169d8d0),('select action',0x1e6d698,0x20,0x169dbd0),
           ('select action',0x1e6d698,0xd0,0x12bc380)]
    for owner,table,slot,callee in slots:
        assert struct.unpack('<Q',at(table+slot,8))[0]==BASE+callee,(owner,hex(slot))
        checks.append(f'{owner} vtable {table:#x}+{slot:#x} -> {callee:#x}')
    witnesses={
        # The tail of OnEquip (0x1699800): the select action, built and played.
        'OnEquip needs the fragment table at weapon+0x470':(0x1699bcf,'4c39b670040000'),
        'a first pickup reads the global +0xE5C':(0x1699c0c,'4439b05c0e0000'),
        'a first pickup: the select flag clear':(0x1699c2b,'4533c9'),
        'a first pickup: fragment table +0xCC':(0x1699c2e,'448b81cc000000'),
        'the action is 0x90 bytes':(0x1699c37,'b990000000'),
        'the fragment table again':(0x1699c46,'488b8e70040000'),
        'a select: the select flag set':(0x1699c4d,'41b101'),
        'a select: fragment table +0xC4':(0x1699c50,'448b81c4000000'),
        "the weapon's item id from +0x38":(0x1699c57,'8b5638'),
        "the caller's reference (+0x50)":(0x1699c6e,'41ff4650'),
        # PlayAction (0x169A5B0).
        'PlayAction skips a fragment of -1 (+0x38)':(0x169a5d3,'837938ff7427'),
        'PlayAction writes its float to +0x54':(0x169a5e4,'f30f117154'),
        "PlayAction queues on the controller's +0x90":(0x169a5f9,'41ff9090000000'),
        "PlayAction drops the caller's reference":(0x169a608,'ff4950837950007f09'),
        'and releases through the action vtable +0xD0':(0x169a614,'ff90d0000000'),
        # The select action's constructors.
        'the select constructor stores its flag at +0x88':(0x169d713,'4088bb88000000'),
        'and its vtable':(0x169d71a,'488903'),
        'the action base stores the fragment at +0x38':(0x169d479,'44894738'),
        'and the item id at +0x80':(0x169d4ae,'899f80000000'),
        # Enter (0x169D8D0) and Exit (0x169DBD0).
        'Enter tests the select flag':(0x169d8d6,'80b98800000000'),
        "Enter reads the item id":(0x169d8e2,'8b8980000000'),
        "Enter calls the weapon's +0x100":(0x169d8f8,'ff9200010000'),
        "Exit reads the item id":(0x169dbd9,'8b8980000000'),
        "Exit calls the weapon's +0x110":(0x169dbef,'ff9210010000'),
        # The weapon's side.
        'SelectBegun: weapon+0x48B = 0':(0x1692e60,'c6818b04000000c3'),
        "SelectDone: the player's weapon component":(0x1692e8f,'488d88b8140000'),
        'SelectDone: only when this is the current weapon':(0x1692e9b,'483bc77507'),
        'SelectDone: player+0x992 = 0':(0x1692ea0,'c6839209000000'),
        'SelectDone: weapon+0x48B = 1':(0x1692ead,'c6878b04000001'),
        'SelectFinished ends in SelectDone (+0x108)':(0x1693090,'48ffa008010000'),
        'the current weapon is the one at component+0x58':(0x1274c80,'8b4958'),
        # SelectItem (0x12748D0): OnEquip first, then the current id.
        "SelectItem calls the weapon's OnEquip (+8)":(0x1274a12,'ff5008'),
        'then stores it as current (+0x58)':(0x1274a22,'897b58'),
        'the lookup returns the weapon (entity extension - 8)':(0x16a5686,'488d43f8'),
        # The fragment table (0x1637940) stores each name's index one name late.
        'Wpn_Select is stored at +0xC4':(0x163870e,'448987c4000000'),
        'Wpn_Pickup is stored at +0xCC':(0x163879e,'448987cc000000'),
    }
    for name,(rva,code) in witnesses.items():
        b=bytes.fromhex(code);assert at(rva,len(b))==b,name;checks.append(name)
    def rel(rva,length):return rva+length+struct.unpack('<i',at(rva+length-4,4))[0]
    assert rel(0x1699c5d,5)==0x169d6f0 and rel(0x1699c81,5)==0x169a5b0
    checks.append('OnEquip builds the action with 0x169D6F0 and plays it with PlayAction 0x169A5B0')
    assert rel(0x169d707,5)==0x169d420 and rel(0x169d70c,7)==0x1e6d698
    checks.append('the select constructor runs the action base 0x169D420 and installs vtable 0x1E6D698')
    assert rel(0x169d8e8,5)==0x16a5650 and rel(0x169dbdf,5)==0x16a5650 and rel(0x1274c83,5)==0x16a5650
    checks.append('Enter, Exit and the current-weapon getter find the weapon with the lookup 0x16A5650')
    assert rel(0x1692e96,5)==0x1274c80
    checks.append('SelectDone compares with the current weapon (0x1274C80)')
    assert at(0x1274c83,1)==b'\xe9'
    checks.append('the current-weapon getter tail-calls the lookup')
    strings={0x16386be:'Wpn_Select',0x1638707:'Wpn_Unselect',0x163874e:'Wpn_Pickup'}
    for rva,text in strings.items():
        assert image.cstring(rel(rva,7),32)==text,text
    assert at(0x16386cc,1)==b'\xe8' and at(0x1638715,1)==b'\xe8' and at(0x163875c,1)==b'\xe8' and at(0x16387a5,1)==b'\xe8'
    assert rel(0x16386cc,5)==rel(0x1638715,5)==rel(0x163875c,5)==rel(0x16387a5,5)
    checks.append('fragment names: Wpn_Select is looked up, then stored at +0xC4 while Wpn_Unselect loads; '
                  'Wpn_Pickup at +0xCC while the next name loads; one lookup function for all')
    checks.extend(verify_stow(image,rel))
    checks.extend(verify_classes(image,rel))
    checks.extend(verify_sound(image,relocs,rel,mine,tables))
    return {'target':str(DEFAULT_MODULE),'sha256':digest,'environment':'static','game_launched':False,'checks':checks,
        'limits':'Proves native bytes, layouts and call routes. Not exercised: the skip in a running game, that the '
                 'controller falls through to the wrench idle, what Mannequin blends, which trigger the draw plays, or how the '
                 'hand looks in a headset.'}

def verify_stow(image,rel):
    """The stow: the shared OnUnequip's animated and instant paths, and the stow action's Exit."""
    at=image.at;checks=[]
    for owner,table,slot,callee in [('wrench',0x1e92f00,0x10,0x17392f0),
                                    ('stow action',0x1e6d770,0x20,0x169dca0),('stow action',0x1e6d770,0xd0,0x12bc380)]:
        assert struct.unpack('<Q',at(table+slot,8))[0]==BASE+callee,(owner,hex(slot))
        checks.append(f'{owner} vtable {table:#x}+{slot:#x} -> {callee:#x}')
    witnesses={
        # The wrench's OnUnequip (0x17392F0).
        'the wrench keeps animate from DL':(0x17392fd,'0fb6da'),
        'cancels its charge (+0x90)':(0x1739303,'ff9090000000'),
        'and passes animate on':(0x1739309,'0fb6d3488bcf'),
        # The shared OnUnequip (0x169A000).
        'OnUnequip needs the fragment table (+0x470)':(0x169a0d3,'4c8bb370040000'),
        'tests the attack flag (+0x2C9)':(0x169a0e3,'80bbc902000000'),
        'mid-attack and animating: the closing fragment (+0xE8)':(0x169a136,'458b86e8000000'),
        'no attack: tests animate':(0x169a196,'4084ed7451'),
        'animating: Wpn_Unselect (+0xC8)':(0x169a1b7,'458b86c8000000'),
        'not animating: needs the hand attachment (+0x2B0)':(0x169a1ec,'4883bbb002000000741a'),
        # Stow (0x16924E0): the equipment after it.
        "Stow takes the player's weapon component":(0x1692867,'4805b8140000'),
        'last equipped = the current id':(0x169287c,'4489405c'),
        'the current id cleared':(0x1692885,'897058'),
        'unequip in progress cleared':(0x169288f,'40887078'),
    }
    for name,(rva,code) in witnesses.items():
        b=bytes.fromhex(code);assert at(rva,len(b))==b,name;checks.append(name)
    assert rel(0x173930f,5)==0x169a000
    checks.append("the wrench's OnUnequip calls the shared 0x169A000")
    assert rel(0x169a140,5)==0x169d5c0 and rel(0x169a162,5)==0x169a5b0
    assert rel(0x169a1c3,5)==0x169d790 and rel(0x169a1e5,5)==0x169a5b0
    checks.append('animating, OnUnequip builds the stow action (0x169D790; mid-attack: 0x169D5C0) and plays it with PlayAction')
    assert rel(0x169a1f9,5)==0x16924e0 and image.cstring(rel(0x169a201,7),16)=='empty' and rel(0x169a20b,5)==0x1583f50
    checks.append('not animating, OnUnequip calls Stow 0x16924E0 and sets the HUD "empty" (0x1583F50)')
    assert rel(0x169d79f,5)==0x169d420 and rel(0x169d7a4,7)==0x1e6d770
    checks.append('the stow action is the action base with vtable 0x1E6D770')
    assert rel(0x169dcc0,5)==0x16924e0 and image.cstring(rel(0x169dcf7,7),16)=='empty' and rel(0x169dd01,5)==0x1583f50
    checks.append("the stow action's Exit calls the same Stow and \"empty\"")
    return checks

def verify_sound(image,relocs,rel,drawRows,tables):
    """WeaponSoundNative.h: what plays the draw's sound, for the capture and a replay."""
    at=image.at;checks=[]
    entries=native_rows(ROOT/'src/equipment/weapon/WeaponSoundNative.h')
    assert len(entries)==4
    for rva,size,expected,name in entries:
        b=at(rva,size)
        assert fnv(b)==expected,name
        assert not any(rva<=p<rva+size for p in relocs),name+' relocates in memory'
        corrupt=bytearray(b);corrupt[len(b)//2]^=1
        assert fnv(corrupt)!=expected,name+' mutation control'
        checks.append('sound '+name+': complete body, no base relocations, mutation refused')
    mine={rva for rva,_,_,_ in entries}
    assert not (mine&drawRows) and not any(mine&t for t in tables.values())
    checks.append('no sound row in another table')
    for owner,table,slot,callee in [('CAudioSystem',0x1d8ee30,0x40,0xd72300),
                                    ('CEntityAudioProxy',0x1d31a30,0xd8,0x911f40),
                                    ('CEntityAudioProxy',0x1d31a30,0xe0,0x913d90)]:
        assert struct.unpack('<Q',at(table+slot,8))[0]==BASE+callee,(owner,hex(slot))
        checks.append(f'{owner} vtable {table:#x}+{slot:#x} -> {callee:#x}')
    witnesses={
        # The PlaySound params (Serialize 0x3AE010).
        'StartTrigger is params+0x08':(0x3ae018,'488d4108'),
        'StopTrigger is params+0x10':(0x3ae04f,'488d4610'),
        # The PlaySound clip's OnEnter (0x3AE460).
        'OnEnter tests the sound flags (+0x28)':(0x3ae47a,'4183792800'),
        'OnEnter keeps the params from R9':(0x3ae47f,'498be9'),
        'OnEnter keeps the clip from RCX':(0x3ae482,'488bf1'),
        "OnEnter reads the clip's entity (+0x08)":(0x3ae4a2,'488b5e08'),
        "and its id (+0x08)":(0x3ae4b4,'ff5208'),
        'OnEnter reads the start trigger':(0x3ae527,'488b4508'),
        'and resolves it with the audio system (+0x40)':(0x3ae540,'488b5508488b01ff5040'),
        'OnEnter tests fire-and-forget (+0x20)':(0x3ae55e,'807d2000'),
        'OnEnter resolves the stop trigger':(0x3ae588,'488b5510'),
        "the play takes the context's proxy (+0x18)":(0x3aebef,'488b5918'),
        'and calls its ExecuteTrigger (+0xD8)':(0x3aec40,'ff90d8000000'),
        # CEntityAudioProxy::ExecuteTrigger (0x911F40) and the trigger lookup.
        "ExecuteTrigger reads its entity (+0x40)":(0x911f4d,'4c8b5140'),
        'GetAudioTriggerID is the ATL at +0x2E8':(0xd72300,'4881c1e8020000e9'),
        # The audio system: constructed, then stored at gEnv+0xD0.
        'CAudioSystem installs its vtable':(0xd7159e,'488d058bd80101488901'),
        'it is 0x720 bytes':(0xd74b24,'41b820070000'),
        'and stored at environment+0xD0':(0xd74b76,'49899fd0000000'),
        # Prey's Lua ExecuteAudioTrigger (0x940B90).
        "the bind takes the entity's audio proxy (+0x210, type 3)":(0x940bf7,'41b803000000'),
        'through IEntity +0x210':(0x940c05,'ff9010020000'),
        'and calls its ExecuteTrigger (+0xD8)':(0x940ce8,'ff90d8000000'),
        # The replay's way to the player's proxy, as Prey's own code goes.
        "OnEquip's attachment lookup reads the entity system":(0x169b473,'488b0dbe25bb00'),
        'and asks it for an entity by id (+0x70)':(0x169b47a,'8bd0'),
        'GetEntity is the entity system +0x70':(0x169b482,'41ff5070'),
        "the Lua StopAudioTrigger asks for the audio proxy (type 3)":(0x960fc9,'ba03000000'),
        'through IEntity +0x200 GetProxy':(0x960fce,'ff9000020000'),
        'and stops through the proxy +0xE0':(0x960fe4,'41ff91e0000000'),
    }
    for name,(rva,code) in witnesses.items():
        b=bytes.fromhex(code);assert at(rva,len(b))==b,name;checks.append(name)
    assert rel(0x3ae535,7)==0x224da50 and rel(0x3ae559,5)==0x3aebe0 and rel(0xd72307,5)==0xd59f40
    assert rel(0xd7159e,7)==0x1d8ee30 and rel(0xd74b54,5)==0xd71580
    checks.append('OnEnter reads gEnv->pAudioSystem (0x224DA50) and plays through 0x3AEBE0; GetAudioTriggerID jumps '
                  'to 0xD59F40; CAudioSystem (vtable 0x1D8EE30) is built by 0xD71580')
    assert image.cstring(rel(0x3ae03d,7),32)=='StartTrigger'
    assert image.cstring(rel(0xd74b04,7),32)=='CAudioSystem'
    assert image.cstring(rel(0x9120ca,7),100).startswith('<Audio> Trying to execute an audio trigger on an EntityAudioProxy')
    assert image.cstring(rel(0xd5a008,7),40)=='Unknown trigger name: %s'
    checks.append("names: StartTrigger, CAudioSystem, the EntityAudioProxy's ExecuteTrigger error, the ATL's unknown trigger")
    # The trigger id: ~CRC32 of the lowercased name, the standard table.
    table=[]
    for n in range(256):
        c=n
        for _ in range(8):c=(c>>1)^0xedb88320 if c&1 else c>>1
        table.append(c)
    assert list(struct.unpack('<256I',at(0x1c8ae40,1024)))==table
    checks.append('the lookup table at 0x1C8AE40 is the standard CRC32 table, so an id is zlib.crc32(name.lower())')
    assert rel(0x169b473,7)==0x224da38
    checks.append('the entity system is the global 0x224DA38 (gEnv+0xB8)')
    # The triggers the capture named (player-20261031-211309), and their ids, from the header.
    text=(ROOT/'src/equipment/weapon/WeaponSoundNative.h').read_text()
    triggers={key:(name,int(value,16)) for key,name,value in re.findall(r'(\w+)\{"(\w+)",0x([0-9A-F]+)\}',text)}
    assert len(triggers)==12 and {'ArmWrench','SwitchWeapon','Unequip'}<=set(triggers)
    for name,value in triggers.values():
        assert zlib.crc32(name.lower().encode())==value,name
    checks.append(f'{len(triggers)} triggers have the ids the lookup computes: '+', '.join(n for n,_ in triggers.values()))
    assert triggers['ArmWrench']==('Play_Player_Arm_Wrench',0x446c5bfd)
    assert triggers['SwitchWeapon']==('Play_switch_weapon',0x364da6fe)
    assert triggers['Unequip']==('Play_Player_Unequip',0xe06723d1)
    classes=re.findall(r'\{"(Ark\w+)",(\w+),(\w+)\}',text)
    assert len(classes)==11 and 'ArkWeaponWrench' in [name for name,_,_ in classes]
    lists=dict(re.findall(r'(\w+)\[\]=\{([\w,]+)\}',text))
    for name,draw,stow in classes:
        assert all(key in triggers for key in lists[draw].split(',')+lists[stow].split(',')),name
    checks.append(f'{len(classes)} classes name their draw and stow lists, and every entry is one of those triggers')
    # The clip's empty callback info: {0, 0, 0, flags 1} at 0x2423D78, built once in 0x3AEBE0.
    assert rel(0x3aec25,7)==0x2423d78 and at(0x3aec31,6)==bytes.fromhex('41b901000000')
    # mov [rip+disp32], imm32: the displacement precedes the immediate.
    def store(rva,length,disp_at):return rva+length+struct.unpack('<i',at(rva+disp_at,4))[0]
    assert at(0x3aec6e,11)==bytes.fromhex('48c7050f51070200000000') and store(0x3aec6e,11,3)==0x2423d88
    assert at(0x3aec80,10)==bytes.fromhex('c7050651070201000000') and store(0x3aec80,10,2)==0x2423d90
    checks.append("the clip plays on proxy local id 1 with a static callback info at 0x2423D78: +0x10 = 0, flags "
                  "(+0x18) = 1 (the replay passes the same {0, 0, 0, 1})")
    return checks

def verify_classes(image,rel):
    """What differs between the weapon classes, and that the skip reaches every one of them."""
    at=image.at;checks=[];slot=lambda table,offset:image.vtable_target(table+offset)
    # Name -> factory -> create -> constructor -> vtable. A create of 0 in `ctor` builds the object inline.
    names={0x1dfde20:'ArkWeapon',0x1e94f28:'ArkWeaponDiscRifle',0x1e94f40:'ArkWeaponDoubleWrench',
           0x1e94f58:'ArkWeaponEMPGrenade',0x1e94f70:'ArkWeaponGooGun',0x1e94f80:'ArkWeaponInstalaser',
           0x1e94f98:'ArkWeaponLureGrenade',0x1e94fb0:'ArkWeaponNullwaveTransmitter',0x1e6fc90:'ArkWeaponPistol',
           0x1e94fd0:'ArkWeaponRecyclerGrenade',0x1e94ff0:'ArkWeaponShotgun',0x1e95008:'ArkWeaponStunGun',
           0x1e6fca0:'ArkWeaponToyGun',0x1e95020:'ArkWeaponWrench'}
    for rva,name in names.items():assert image.cstring(rva,48)==name,name
    checks.append('the fourteen class names Prey registers in 0x17327F0')
    def calls(fn,target,span):
        return any(at(fn+n,1)==b'\xe8' and rel(fn+n,5)==target for n in range(span))
    def loads(fn,target,span):
        return any(at(fn+n,1)==b'\x48' and at(fn+n+1,1)==b'\x8d' and rel(fn+n,7)==target for n in range(span))
    chain=[(0x1e6c640,0x1e95770,0x172f800,0x1690030),(0x1e308d0,0x1e95788,0x172f880,0x13b6e70),
           (0x1e31290,0x1e957d0,0x172fb00,0x13b9b30),(0x1e6e390,0x1e956c8,0x172f900,0x169e1d0),
           (0x1e6e9f0,0x1e95728,0x172fa00,0x169fe70),(0x1e6f148,0x1e957b8,0x172f980,0x16a4710),
           (0x1e6f780,0x1e95758,0x172fa80,0x16a6f00),(0x1e92a08,0x1e957a0,0x172f480,0),(0x1e92f00,0x1e957e8,0x172f5e0,0)]
    for vtable,factory,create,ctor in chain:
        assert slot(factory,8)==create,hex(factory)
        if ctor:assert calls(create,ctor,0x40),hex(create)
        assert loads(ctor or create,vtable,0x100),hex(vtable)
        checks.append(f'factory {factory:#x} creates {vtable:#x}')
    # The table in the header against the image, slot by slot.
    text=(ROOT/'src/equipment/weapon/WeaponDrawNative.h').read_text()
    bodies={'SelectBegun':0x1692e60,'SelectDone':0x1692e70,'SelectFinished':0x1692f50,'NoOp':0x1706520,
            'QBeamFinished':0x16a07a0,'DisruptorDone':0x13b9e80,'GrenadeDone':0x16a5300}
    table=re.search(r'WeaponClasses\[\]=\{(.*?)\};',text,re.S).group(1)
    rows=re.findall(r'\{0x([0-9A-F]+),0x([0-9A-F]+),(\w+),(\w+),(\w+)\}, // ([^\n]+)',table)
    assert sorted(int(row[0],16) for row in rows)==sorted(link[0] for link in chain)
    for vtable,unequip,begun,done,finished,label in rows:
        vtable,unequip=int(vtable,16),int(unequip,16)
        for offset,target in ((0x10,unequip),(0x100,bodies[begun]),(0x108,bodies[done]),(0x110,bodies[finished])):
            assert slot(vtable,offset)==target,(label,hex(offset))
        checks.append(f'{label} {vtable:#x}: +0x10 {unequip:#x}, +0x100 {begun}, +0x108 {done}, +0x110 {finished}')
    # Each class's OnEquip runs the shared one first, and the select action has one builder.
    assert slot(0x1e92f00,8)==slot(0x1e92a08,8)==slot(0x1e6c640,8)==0x1699800
    for entry,site in ((0x13b7a10,0x13b7a19),(0x13bb1d0,0x13bb1d9),(0x169f3f0,0x169f3f9),(0x16a29a0,0x16a29a9),
                       (0x16a5de0,0x16a5ded),(0x16aa8c0,0x16aa8c9)):
        assert rel(site,5)==0x1699800 and not calls(entry,0x1699800,site-entry),hex(entry)
    checks.append('three classes use the shared OnEquip; the six others run it as their first call')
    # Every OnUnequip reaches the shared one with animate as it came.
    for name,entry in (('DiscRifle',0x13b7b60),('guns',0x16aab20),('DoubleWrench',0x17392d0)):
        assert at(entry,9)==bytes.fromhex('40534883ec20488bd9') and rel(entry+9,5)==0x169a000,name
        checks.append(f'{name} OnUnequip {entry:#x} calls the shared one before anything touches DL')
    for name,entry,site,keep,restore in (('Disruptor',0x13bc2b0,0x13bc32e,'0fb6fa','400fb6d7'),
                                         ('GLOO',0x169f6c0,0x169f70c,'0fb6da','0fb6d3'),
                                         ('Q-Beam',0x16a3530,0x16a3554,'0fb6da','0fb6d3'),
                                         ('grenade',0x16a5ea0,0x16a5faa,'0fb6ea','400fb6d5'),
                                         ('wrench',0x17392f0,0x173930f,'0fb6da','0fb6d3')):
        assert bytes.fromhex(keep) in at(entry,0x50) and bytes.fromhex(restore) in at(site-0x24,0x24),name
        assert rel(site,5)==0x169a000,name
        checks.append(f'{name} OnUnequip {entry:#x} keeps DL and hands it back at {site:#x}')
    assert slot(0x1e6c640,0x10)==0x169a000 and at(0x169a00c,3)==bytes.fromhex('0fb6ea')
    checks.append('the shared OnUnequip takes animate from DL')
    # The Q-Beam's busy byte.
    assert at(0x16a29b6,7)==bytes.fromhex('c6838405000001')
    for site in (0x16a02b6,0x16a3b46,0x16a3c46):assert at(site,7)==bytes.fromhex('80b98405000000'),hex(site)
    assert at(0x16a07a0,7)==bytes.fromhex('c6818405000000') and rel(0x16a07a7,5)==0x1692f50
    checks.append("the Q-Beam's OnEquip sets +0x584, its fire update, reload and fire end stop on it, and only its "
                  "finish clears it on the way to SelectFinished")
    assert rel(0x13b9e89,5)==0x1692e70 and rel(0x16a5312,5)==0x1692e70
    checks.append("the Disruptor's and the grenade's done run SelectDone first")
    assert at(0x1706520,3)==bytes.fromhex('c20000')
    checks.append("the Disruptor's +0x100 is the no-op: it never goes busy")
    # +0x2C9 is an attack under way: StartAttack sets it, StopAttack clears it.
    assert slot(0x1e6c640,0xe8)==0x169bd80 and slot(0x1e6c640,0xf8)==0x169bfa0
    assert at(0x169bddf,9)==bytes.fromhex('66c787c90200000100') and at(0x169bfd4,9)==bytes.fromhex('66c781c90200000000')
    checks.append('base StartAttack (+0xE8) sets +0x2C9, base StopAttack (+0xF8) clears it')
    # Animation events never reach a draw or a stow.
    assert slot(0x1e6d9f8,0x98)==slot(0x1e6e268,0x98)==0x169dd20
    assert slot(0x1e6d698,0x98)==slot(0x1e6d770,0x98)==0x1706520
    checks.append('the weapon event handler 0x169DD20 is at +0x98 of two other actions; in the select and stow actions '
                  'that slot is the no-op')
    # The class name's route, as Prey's metrics read it, and the entity at weapon+0x40.
    assert at(0x13b7e16,3)==bytes.fromhex('ff5018') and at(0x13b7e1f,3)==bytes.fromhex('ff5210')
    checks.append('IEntity +0x18 GetClass, then +0x10 GetName (0x13B7E16)')
    assert at(0x16a3a46,15)==bytes.fromhex('488b494033d2488b01ff9000020000')
    checks.append('weapon+0x40 is the entity: the Q-Beam calls GetProxy (+0x200) on it')
    return checks

if __name__=='__main__':
    report=json.dumps(verify(),indent=2)+'\n'
    if len(sys.argv)>1:Path(sys.argv[1]).write_text(report,encoding='utf-8',newline='\n')
    print(report)
