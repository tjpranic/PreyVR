"""Read-only Steam helmet contracts (src/equipment/head/HelmetNative.h, docs/ZERO-G-SCOPE-2026-10-30.md): the bone
attachment's model transform at +0x130, its relative default at +0xF8 (where Prey's HUD helmet writes the sway), its
hide bits, the player entity's world matrix, the character render the size hook rescales (the helmet's character is
drawn by a character attachment whose RenderAttachment passes an identity QuatTS), and the slots that reach them.
Never attaches."""
import json,sys
from pathlib import Path
from preydll import DEFAULT_MODULE,Image,fnv1a64 as fnv,native_rows
ROOT=Path(__file__).resolve().parents[2]
HEADER=ROOT/'src/equipment/head/HelmetNative.h'
MODEL,RELATIVE,HIDE,WORLD_TM,RENDER,RENDER_ATTACHMENT=0x822c80,0x828dc0,0x7b2b40,0x10ea260,0x81bcb0,0x334df0
BONE_VTABLE,ENTITY_VTABLE,CHR_ATTACHMENT_VTABLE,CHARACTER_VTABLE=0x1d212b8,0x1d30bd0,0x1cb1328,0x1d22200

def header_constant(name):
    """`inline constexpr ... name=0x...` from HelmetNative.h."""
    text=HEADER.read_text(encoding='utf-8')
    for part in text.replace(';',',').split(','):
        part=part.strip()
        if '=' in part and part.split('=')[0].split()[-1:]==[name]:return int(part.split('=')[1].strip(),0)
    raise KeyError(name)

def verify():
    image=Image(DEFAULT_MODULE);at=image.at
    entries=native_rows(HEADER)
    assert [rva for rva,_,_,_ in entries]==[MODEL,RELATIVE,HIDE,WORLD_TM,RENDER,RENDER_ATTACHMENT],entries
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
    for name,value in {'ModelRelative':MODEL,'RelativeDefault':RELATIVE,'Hide':HIDE,'EntityGetWorldTm':WORLD_TM,
                       'EntityVtable':ENTITY_VTABLE,'VtableModelRelative':0x68,'VtableRelativeDefault':0x58,
                       'VtableHide':0x88,'EntityWorldTmSlot':0xe8,'AttachModel':0x130,'AttachFlags':0x08,
                       'HideBits':0x70000,'HideMainPass':0x10000,'PlayerEntity':0x38,'EntityWorldTm':0x60,
                       'CharacterRender':RENDER,'RenderAttachment':RENDER_ATTACHMENT,
                       'ChrAttachmentVtable':CHR_ATTACHMENT_VTABLE,'CharacterVtable':CHARACTER_VTABLE,'AttachObject':0x20,
                       'ObjectCharacter':0x08,'VtableRenderAttachment':0x18,'VtableRender':0xc0,'OffsetFloats':8,
                       'OffsetScale':7}.items():
        assert header_constant(name)==value,name
    checks.append('the header names the bodies, slots and offsets read here')
    for slot,target,name in ((BONE_VTABLE+0x68,MODEL,'bone +0x68 GetAttModelRelative'),
                             (BONE_VTABLE+0x58,RELATIVE,'bone +0x58 SetAttRelativeDefault'),
                             (BONE_VTABLE+0x88,HIDE,'bone +0x88 HideAttachment'),
                             (ENTITY_VTABLE+0xe8,WORLD_TM,'CEntity +0xE8 GetWorldTM'),
                             (CHR_ATTACHMENT_VTABLE+0x18,RENDER_ATTACHMENT,'character attachment +0x18 RenderAttachment'),
                             (CHR_ATTACHMENT_VTABLE+0x08,0x13d1b90,'character attachment +0x08 GetAttachmentType'),
                             (CHARACTER_VTABLE+0xc0,RENDER,'CCharInstance +0xC0 Render')):
        assert image.vtable_target(slot)==target,name
        checks.append(name)
    assert at(MODEL,8)==bytes.fromhex('488d8130010000c3')
    checks.append('GetAttModelRelative returns +0x130')
    assert at(RELATIVE,0x3a)==bytes.fromhex('8b028981f80000008b42048981fc0000008b42088981000100008b420c898104010000'
                                             'f20f104210f20f1181080100008b4218898110010000c3')
    checks.append('SetAttRelativeDefault copies the QuatT to +0xF8 (q) and +0x108 (t), nothing else')
    assert at(HIDE,20)==bytes.fromhex('85d2740881490800000700c3816108fffff8ffc3')
    checks.append('HideAttachment(1) sets 0x70000 in the flags at +0x08; (0) clears exactly those')
    assert at(WORLD_TM,5)==bytes.fromhex('488d4160c3')
    checks.append('CEntity::GetWorldTM returns the matrix at +0x60')
    assert at(0x13d1b90,6)==bytes.fromhex('b802000000c3')
    checks.append('that attachment object is type 2, a character')
    for name,rva,hexbytes in (('RenderAttachment takes the character at the object +0x08',0x334e12,'488b4908'),
                              ('passes the QuatTS at RSP+0x3C (R8)',0x334e2e,'4c8d44243c'),
                              ('whose scale (+0x1C, RSP+0x58) is 1.0',0x334e74,'c74424580000803f'),
                              ("and calls the character's Render (+0xC0)",0x334e98,'ff90c0000000'),
                              ('Render keeps the offset (R8) in R14',0x81bcd3,'4d8bf0'),
                              ("and reads its scale at +0x1C",0x81bf8b,'f3410f10561c')):
        assert at(rva,len(bytes.fromhex(hexbytes)))==bytes.fromhex(hexbytes),name
        checks.append(name)
    # The HUD helmet's update writes the sway on the 'helmet' attachment through +0x58 (verify_zero_g.py has the
    # rest of its route): a QuatT with t zeroed, so the helmet at rest is its joint.
    assert image.cstring(image.rip_target(0x1327cb9,7,3))=='helmet' and at(0x1327d0e,3)==bytes.fromhex('ff5058')
    checks.append("the HUD helmet's update calls +0x58 on the slot-0 'helmet'")
    return {'target':str(DEFAULT_MODULE),'sha256':image.sha256,'environment':'static','game_launched':False,'checks':checks,
        'limits':'Proves native bytes and slots. Not exercised: whether the render draws a model transform written '
                 'before it (helmet.report kept/overwritten), the scaled draw (sizedDraws), where the visible helmet is drawn, or how it looks '
                 'in a headset.'}

if __name__=='__main__':
    report=json.dumps(verify(),indent=2)+'\n'
    if len(sys.argv)>1:Path(sys.argv[1]).write_text(report,encoding='utf-8',newline='\n')
    print(report)
