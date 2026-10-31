"""Read-only Steam head-lamp contracts: why the flashlight ignores the headset (its ArkLight entity rides a bone
attachment on the eye joint, placed every frame by CEntityAttachment::ProcessAttachment from the attachment's world
placement), and what the hook relies on (that placement is the light's only producer; the bone update's layout; the
world placement's composition; the flashlight component at ArkPlayer +0x11C0). Never attaches."""
import json,struct,sys
from pathlib import Path
from preydll import DEFAULT_MODULE,Image,fnv1a64 as fnv,native_rows
ROOT=Path(__file__).resolve().parents[2]
HEADER=ROOT/'src/equipment/head/HeadLampNative.h'
PLACE,WORLD,UPDATE=0x335010,0x7a2250,0x7a3390
BINDING_VTABLE,BONE_VTABLE=0x1cb13b0,0x1d212b8
ENTITY_SYSTEM=0x224da38

def header_constant(name):
    """`inline constexpr ... name=0x...` from HeadLampNative.h."""
    text=HEADER.read_text(encoding='utf-8')
    for part in text.replace(';',',').split(','):
        part=part.strip()
        if '=' in part and part.split('=')[0].split()[-1:]==[name]:return int(part.split('=')[1].strip(),0)
    raise KeyError(name)

def verify():
    image=Image(DEFAULT_MODULE);at=image.at
    entries=native_rows(HEADER)
    assert [rva for rva,_,_,_ in entries]==[PLACE,WORLD,UPDATE],entries
    relocs=image.relocations();checks=[]
    for rva,size,expected,name in entries:
        b=at(rva,size)
        assert fnv(b)==expected,name
        assert not any(rva<=p<rva+size for p in relocs),name+' relocates in memory'
        corrupt=bytearray(b);corrupt[len(b)//2]^=1
        assert fnv(corrupt)!=expected,name+' mutation control'
        assert at(rva+size,1)==b'\xcc',name+' is the whole function'
        checks.append(name+': complete body (padding after it), no base relocations, mutation refused')
    mine={rva for rva,_,_,_ in entries}
    for header in sorted((ROOT/'src').rglob('*Native.h')):
        if header.name!=HEADER.name:assert not mine&{rva for rva,_,_,_ in native_rows(header)},header.name
    checks.append('no row in another *Native.h table')
    for name,value in {'ProcessAttachment':PLACE,'BindingVtable':BINDING_VTABLE,'VtableProcessAttachment':0x10,
                       'BindingEntity':0x08,'BindingScale':0x0c,'BoneVtable':BONE_VTABLE,'BoneWorldAbsolute':WORLD,
                       'VtableWorldAbsolute':0x70,'AttachName':0x10,'AttachManager':0x28,'ManagerCharacter':0x18,
                       'AttachRelative':0xf8,'AttachK':0x14c,'AttachJoint':0x15c,'EntitySetPosRotScale':0x138,
                       'PlacementFlags':0x10}.items():
        assert header_constant(name)==value,name
    checks.append("HeadLampNative.h's constants are the ones witnessed below")

    def witness(group,table):
        for name,(rva,code) in table.items():
            b=bytes.fromhex(code);assert at(rva,len(b))==b,group+': '+name;checks.append(group+': '+name)

    # The two vtable slots the hook and its read go through, and nothing else holding the placement's address.
    assert image.vtable_target(BINDING_VTABLE+0x10)==PLACE and image.vtable_target(BONE_VTABLE+0x70)==WORLD
    checks.append('CEntityAttachment vtable 0x1CB13B0 +0x10 is the placement; bone vtable 0x1D212B8 +0x70 the world placement')
    rdata=image.section('.rdata');table=at(rdata.rva,rdata.raw_size)
    for target,slot in ((PLACE,BINDING_VTABLE+0x10),(WORLD,BONE_VTABLE+0x70)):
        found=[];i=table.find(struct.pack('<Q',image.base+target))
        while i!=-1:found.append(rdata.rva+i);i=table.find(struct.pack('<Q',image.base+target),i+1)
        assert found==[slot],(hex(target),[hex(x) for x in found])
    checks.append('each is held by that one slot in .rdata, and by no other table')

    witness('placement 0x335010',{
        "the attachment's world placement, its vtable +0x70, into a QuatTS on the stack":(0x335019,'488b024c8bc2488bd9488d542460498bc8ff5070'),
        'gEnv->pEntitySystem':(0x33502d,'488b0d048af101'),
        "the binding's entity id (+0x08)":(0x335034,'8b5308'),
        'q (+0x00..+0x0C) to the rotation':(0x335037,'f30f10400cf30f1144244cf30f1008f30f114c2440'),
        't (+0x10..+0x18) to the position':(0x335062,'f30f104010f30f11442450f30f104814f30f114c2454f30f104018f30f11442458'),
        's (+0x1C)':(0x335083,'f30f10481cf30f114c245c'),
        'GetEntity (vtable +0x70)':(0x33508e,'488b01ff5070'),
        'nothing bound: nothing placed':(0x335094,'488bc84885c07453'),
        "the binding's scale (+0x0C..+0x14) times s":(0x33509c,'f30f104c245c4c8d4c2430f30f10430c'),
        'flags 0x10':(0x3350c7,'c744242010000000'),
        'SetPosRotScale (entity vtable +0x138)':(0x3350e9,'ff9038010000'),
    })
    assert image.rip_target(0x33502d,7,3)==ENTITY_SYSTEM
    checks.append('placement 0x335010: the entity system is gEnv +0xB8 (0x224DA38)')

    witness('world placement 0x7A2250',{
        "the model transform's position (+0x140)":(0x7a225a,'f30f10a940010000'),
        'the manager (+0x28)':(0x7a2262,'488b4128'),
        "the manager's character (+0x18)":(0x7a2280,'4c8b4018'),
        "the character's location (+0xA90)":(0x7a2289,'4981c0900a0000'),
        "the model transform's orientation (+0x130..)":(0x7a2290,'f3440f108134010000'),
    })
    witness('bone update 0x7A3390',{
        'only a bound joint (+0x15C)':(0x7a339c,'83b95c01000000'),
        "reprojects when not projected (flag 0x4000 at +0x08)":(0x7a33af,'f7410800400000'),
        "the relative default's w (+0x104)":(0x7a34db,'f30f10ab04010000'),
        "and its x (+0xF8)":(0x7a34f4,'f30f109bf8000000'),
        "K's w (+0x158)":(0x7a357c,'f30f10b358010000'),
        'the model transform written at +0x130':(0x7a358d,'488dab30010000'),
        "K's x (+0x14C)":(0x7a35f8,'f30f10a34c010000'),
        "then the binding's (+0x20) ProcessAttachment (+0x10), handed the attachment":(0x7a3780,'488b4b20488bd3488b01ff5010'),
    })
    witness('bind 0x12647A0',{
        "the light's owner entity (+0x20)":(0x12647b3,'8b5320ff5070'),
        'its character, slot 0 (vtable +0x2D8)':(0x12647c5,'33d2488bc841ff90d8020000'),
        'its attachment manager (vtable +0x40)':(0x12647e1,'ff5240'),
        'the attachment by the name at +0x30 (vtable +0x38)':(0x12647e4,'488b5330488bc84c8b0041ff5038'),
        'a binding of the entity-attachment vtable':(0x1264813,'488d0596cba400488902'),
        'scale 1, 1, 1':(0x126481d,'c7420c0000803fc742100000803fc742140000803f'),
        "the light entity's id (+0x24) bound":(0x1264836,'8b4324'),
        'AddBinding (vtable +0xD8)':(0x1264848,'ff90d8000000'),
    })
    assert image.rip_target(0x1264813,7,3)==BINDING_VTABLE
    checks.append('bind 0x12647A0: the binding it makes is a CEntityAttachment (0x1CB13B0)')
    witness('flashlight component',{
        'ArkPlayer::Update updates the flashlight at ArkPlayer +0x11C0':(0x1585115,'488d8fc0110000'),
        "its light's ArkLight by the entity id at +0x34":(0x158af11,'8b4b34'),
    })
    assert image.call_target(0x158511f)==0x158add0
    checks.append('ArkPlayer::Update 0x1584FE0 calls the flashlight update 0x158ADD0')
    return {'target':str(DEFAULT_MODULE),'sha256':image.sha256,'environment':'static','game_launched':False,'checks':checks,
        'limits':'Proves native bytes and call routes. The light attachments\' names (`flashlight`, `psychoscope_light` on '
                 'camera_jnt) come from the character, measured by ik.skel (run player-20261025-125133); their mounts and '
                 'when the attachment pass runs in the frame are not static facts. Not exercised: the hook in a running '
                 'game, or how the light looks in a headset.'}

if __name__=='__main__':
    report=json.dumps(verify(),indent=2)+'\n'
    if len(sys.argv)>1:Path(sys.argv[1]).write_text(report,encoding='utf-8',newline='\n')
    print(report)
