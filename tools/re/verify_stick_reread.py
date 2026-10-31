"""Read-only Steam contracts for Prey's re-read of the player's stick axes, which stopped the
player while the stick was held (docs/MOVE-STOP-2026-10-24.md), and for what StickReread.cpp
hooks and reads to keep the mod's stick there. Never attaches."""
import json,struct,sys
from pathlib import Path
from preydll import DEFAULT_MODULE,Image,fnv1a64 as fnv,native_rows
ROOT=Path(__file__).resolve().parents[2]
BASE=0x180000000

def verify():
    image=Image(DEFAULT_MODULE);at=image.at
    rows=native_rows(ROOT/'src/controls/input/StickRereadNative.h')
    assert len(rows)==7
    relocs=image.relocations()
    checks=[]
    for rva,size,expected,name in rows:
        body=at(rva,size)
        assert fnv(body)==expected,name
        # PlayerSingleton is the row SceneQueryNative.h and EquipmentNative.h hash too.
        assert name=='PlayerSingleton' or body[-1:]==b'\xc3',name+' ends in its ret'
        assert not any(rva<=p<rva+size for p in relocs),name+' relocates in memory'
        corrupt=bytearray(body);corrupt[len(body)//2]^=1
        assert fnv(corrupt)!=expected,name+' mutation control'
        checks.append(name+': complete body, no base relocations, mutation refused')
    for vtable,slot,callee,what in ((0x1e580f8,0x8,0x1590390,'ArkPlayerInput listener vtable: OnActionMapEvent'),
                                    (0x1cbfc48,0x1a8,0x3cdfe0,'CActionMapManager vtable: ActionFiltered'),
                                    (0x1cbfc48,0xd8,0x3cf050,'CActionMapManager vtable: EnableActionMap'),
                                    (0x1cbfc48,0xe0,0x3cf1c0,'CActionMapManager vtable: EnableFilter'),
                                    (0x1cbe190,0x38,0x3c3770,'CActionFilter vtable: Enable')):
        assert struct.unpack('<Q',at(vtable+slot,8))[0]==BASE+callee,what
        checks.append(f'{what} ({vtable:#x}+{slot:#x}) -> {callee:#x}')
    witnesses={
        # The listener: which events re-read.
        'the listener reads the event type':(0x15903ab,'8b02'),
        'event 3 (a map changed state)':(0x15903b2,'83f803'),
        'event 3 re-reads only for an enabled map (+0x10)':(0x15903bb,'48837a1000'),
        "event 3 asks the map's name (map +0x08, slot +0xC8)":(0x15903c6,'488b4a08488b01ff90c8000000'),
        'event 2 (a filter changed)':(0x15904bc,'83f802'),
        "event 2 re-reads while the input's mode stack (+0x10..+0x18) is not empty":(0x15904c1,'488b411048394108'),
        '... and its top mode is 0 (player)':(0x15904cb,'8378f800'),
        # The re-read: what it stores where.
        're-read: xi_movex (action table +0x2F0)':(0x158e976,'4881c2f0020000'),
        're-read: move X := the read, unless the gate (+0x94)':(0x158e986,'83bb9400000000'),
        're-read: move X stored at +0x5C':(0x158e99a,'f30f11435c'),
        're-read: xi_movey (+0x2F8)':(0x158e9eb,'4881c2f8020000'),
        're-read: move Y stored at +0x60':(0x158ea03,'f30f114660'),
        're-read: xi_rotatepitch (+0x310)':(0x158ea51,'4881c210030000'),
        're-read: pitch stored negated at +0x44':(0x158ea82,'0f57c6f30f114644'),
        're-read: xi_rotateyaw (+0x308)':(0x158ea9a,'4881c208030000'),
        're-read: yaw stored negated at +0x4C':(0x158eac3,'0f57c6f30f11464c'),
        # AxisRead: filtered gives 0, else the physical pad's symbol.
        'AxisRead: the manager is **context':(0x158eb9a,'488b01'),
        'AxisRead: ActionFiltered(manager, action) first':(0x158eba9,'ff90a8010000'),
        'AxisRead: a filtered action reads 0':(0x158ebb1,'756b'),
        'AxisRead: the pad index from CUIManager+0x3C':(0x158ebf7,'448b423c'),
        'AxisRead: device type 3, the gamepad':(0x158ebfb,'ba03000000'),
        'AxisRead: IInput slot +0xD8, LookupSymbol':(0x158ec03,'ff90d8000000'),
        "AxisRead: returns the symbol's value (+0x1C)":(0x158ec0e,'f30f10401c'),
        'AxisRead: otherwise 0':(0x158ec1e,'0f57c0'),
        # The handlers the lanes' posts reach.
        'xi_movex handler stores +0x5C':(0x158fd32,'f30f11415c'),
        'xi_movey handler stores +0x60':(0x158fdb4,'f30f114960'),
        'yaw handler stores nothing while +0xB4 is set':(0x158fe50,'80b9b400000000'),
        '... or the gate is 2':(0x158fe59,'83b99400000002'),
        'yaw handler negates':(0x158fe68,'0f570501ed6e00'),
        'yaw handler stores +0x4C':(0x158fe6f,'f30f11414c'),
        # Why the re-read never finds the mod's stick.
        "PostInputEvent reads the event's symbol (+0x28)":(0x9d6e0d,'488b5328'),
        '... and skips the symbol bookkeeping when it is null':(0x9d6e11,'4885d2'),
        # Who sends the events.
        'EnableActionMap broadcasts only on a change':(0x3cf0d9,'403ac5'),
        'EnableActionMap: event 3, map at +0x08, enable at +0x10':(0x3cf0ff,'488b4328488d5424204889442428488bcf400fb6c548894424'),
        'CActionFilter::Enable: enable at +0x08':(0x3c3841,'400fb6cd488d54242048894c2428'),
        'CActionFilter::Enable: event 2 on every call, the name at +0x10':(0x3c3853,'c7442420020000004889442430'),
        "EnableFilter: event 2 again, enable at +0x10, the name at +0x08":(0x3cf27a,'4889442430c7442420020000004889'
                                                                              '7c2428'),
        # The action names the table offsets index.
        'the action table is a qword global (RIP 0x2C16838)':(0x158e968,'488b15c97e6801'),
        # The player input object: ArkPlayer +0x8E0 (the pause transition resets it there).
        'ArkPlayerInput is at ArkPlayer +0x8E0':(0x119d41a,'488d88e0080000'),
    }
    for name,(rva,code) in witnesses.items():
        b=bytes.fromhex(code);assert at(rva,len(b))==b,name;checks.append(name)
    for site,callee,what in ((0x15903f0,0x158e780,'event 3'),(0x15904d6,0x158e780,'event 2'),
                             (0x158e981,0x158eb90,'move X'),(0x158e9f2,0x158eb90,'move Y'),
                             (0x158ea7d,0x158eb90,'pitch'),(0x158eabe,0x158eb90,'yaw'),
                             (0x3cf121,0x3ce5c0,'EnableActionMap'),(0x3c3860,0x3ce5c0,'CActionFilter::Enable'),
                             (0x3cf28c,0x3ce5c0,'EnableFilter')):
        assert image.call_target(site)==callee,what
        checks.append(f'{what}: the call at {site:#x} reaches {callee:#x}')
    for rva,name in ((0x1708b34,'xi_movex'),(0x1708b47,'xi_movey'),(0x1708b6d,'xi_rotateyaw'),(0x1708b80,'xi_rotatepitch')):
        assert image.cstring(image.rip_target(rva,7,3))==name,name
    for lea,offset in ((0x1708b3b,0x2f0),(0x1708b4e,0x2f8),(0x1708b74,0x308),(0x1708b87,0x310)):
        assert at(lea,3)==bytes.fromhex('488d8b') and struct.unpack('<I',at(lea+3,4))[0]==offset,hex(lea)
    checks.append('the action table holds xi_movex +0x2F0, xi_movey +0x2F8, xi_rotateyaw +0x308, xi_rotatepitch +0x310')
    # The re-read is AxisRead's only caller.
    calls=[p for p in range(0x158e780,0x158e780+1029) if at(p,1)==b'\xe8' and image.call_target(p)==0x158eb90]
    assert calls==[0x158e981,0x158e9f2,0x158ea7d,0x158eabe],calls
    checks.append('the re-read calls AxisRead exactly four times')
    # The lean buttons, zero-G's roll and brake: read again through ButtonRead, its only caller the re-read.
    calls=[p for p in range(0x158e780,0x158e780+1029) if at(p,1)==b'\xe8' and image.call_target(p)==0x158e540]
    assert calls==[0x158e819,0x158e844,0x158eb23,0x158eb53],calls
    for name,rva,code in (('re-read: leanleft (+0xA00) through ButtonRead',0x158eb13,'4981c0000a0000'),
                          ('re-read: stored at ArkPlayerInput +0xB5',0x158eb37,'8881b5000000'),
                          ('re-read: leanright (+0xA08)',0x158eb48,'4981c0080a0000'),
                          ('re-read: stored at +0xB6',0x158eb5b,'8881b6000000'),
                          ('ButtonRead: ActionFiltered(manager RDX, action R8) first',0x158e540,
                           '48895c2408574883ec20488b02488bca498bd0498bf9498bd8ff90a8010000')):
        assert at(rva,len(bytes.fromhex(code)))==bytes.fromhex(code),name
        checks.append(name)
    assert image.call_target(0x158eb64)==0x15910b0
    checks.append('the re-read calls ButtonRead four times, and recomputes the lean axis (0x15910B0) after both leans')
    return {'target':str(DEFAULT_MODULE),'sha256':image.sha256,'environment':'static','game_launched':False,'checks':checks,
        'limits':'Proves native bytes, layouts and call routes within the re-read, and that the re-read is the only '
                 "caller of AxisRead inside its body (a whole-module caller scan is Ghidra's: four calls, 2026-10-24). "
                 'Not exercised: how often the events fire in play, or which level scripts toggle filters.'}

if __name__=='__main__':
    report=json.dumps(verify(),indent=2)+'\n'
    if len(sys.argv)>1:Path(sys.argv[1]).write_text(report,encoding='utf-8',newline='\n')
    print(report)
