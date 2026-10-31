"""Read-only Steam contracts for room-scale's physics calls on the player's living body:
moving it without the engine's teleport (pe_params_pos, bRecalcBounds 0x21), reading
its velocities (pe_status_living) and asking whether its collider fits somewhere
(pe_status_check_stance). docs/BODY-FOLLOW.md, the capsule's physics. Never attaches."""
import json,struct,sys
from pathlib import Path
from preydll import DEFAULT_MODULE,Image,fnv1a64 as fnv,native_rows
ROOT=Path(__file__).resolve().parents[2]
BASE=0x180000000

def verify():
    image=Image(DEFAULT_MODULE);at=image.at
    rows=native_rows(ROOT/'src/body/RoomScaleNative.h')
    assert len(rows)==5
    contact={name:(rva,size,value) for rva,size,value,name in native_rows(ROOT/'src/physics/contact/ContactNative.h')}
    assert contact['LivingGetStatus'][0]==0xb475d0 and contact['StaticGetStatus'][0]==0xb83040
    relocs=image.relocations()
    checks=[]
    for rva,size,expected,name in rows:
        body=at(rva,size)
        assert fnv(body)==expected,name
        assert not any(rva<=p<rva+size for p in relocs),name+' relocates in memory'
        corrupt=bytearray(body);corrupt[len(body)//2]^=1
        assert fnv(corrupt)!=expected,name+' mutation control'
        checks.append(name+': complete body, no base relocations, mutation refused')
    checks.append('living GetStatus 0xB475D0 and the base GetStatus 0xB83040 are hashed by ContactNative.h')
    for slot,callee in ((0x20,0xb49610),(0x30,0xb475d0)):
        assert struct.unpack('<Q',at(0x1d81580+slot,8))[0]==BASE+callee,hex(slot)
        checks.append(f'living vtable 0x1D81580+{slot:#x} -> {callee:#x}')
    witnesses={
        # The base SetParams' pe_params_pos fields.
        'base SetParams reads pos at +0x04':(0xb88e04,'f3410f104704'),
        "base SetParams reads q's w at +0x1C":(0xb88e3e,'f3410f10471c'),
        'base SetParams takes q from +0x10':(0xb88e70,'410f104710'),
        'base SetParams skips an unused sim class at +0x38':(0xb88e7d,'41817f3800000080'),
        'base SetParams recomputes bounds when bRecalcBounds (+0x3C) is non-zero':(0xb88eda,'4139773c'),
        # The living SetParams' teleport, and what skips it.
        'living SetParams skips the teleport when bRecalcBounds & 0x20':(0xb499d6,'f6473c20'),
        'the teleport sets the airborne flag':(0xb49a57,'838bc002000001'),
        "the teleport zeroes the body's velocity":(0xb49a76,'4889b36c010000'),
        # The physics proxy's XForm: pe_params_pos with the position alone, bRecalcBounds 1.
        'proxy pe_params_pos type 0 at its base':(0x96be47,'8945d7'),
        'proxy marks pos unused (+0x04) unless the event moves it':(0x96be4e,'c745dbffffbfff'),
        "proxy marks q unused by its w (+0x1C)":(0x96be5c,'c745f3ffffbfff'),
        'proxy marks scale unused (+0x20)':(0x96be55,'c745f7ffffbfff'),
        'proxy marks the sim class unused (+0x38)':(0x96be63,'c7450f00000080'),
        'proxy clears pMtx3x4 (+0x28)':(0x96be6a,'488945ff'),
        'proxy clears pMtx3x3 (+0x30)':(0x96be6e,'48894507'),
        'proxy sets bRecalcBounds 1 (+0x3C)':(0x96be72,'c7451301000000'),
        'proxy clears bEntGridUseOBB (+0x40)':(0x96be79,'884517'),
        'proxy calls SetParams with bThreadSafe 0':(0x96bf9d,'4533c0'),
        'proxy calls the physics SetParams slot +0x20':(0x96bfa3,'ff5020'),
        # pe_status_living: the body's velocity and the requested one.
        'living status reads the velocity at +0x168':(0xb47929,'f20f108368010000'),
        'living status writes it at +0x30':(0xb47931,'f20f114730'),
        'living status reads the requested velocity at +0x174':(0xb4793f,'f20f108374010000'),
        'living status writes it at +0x3C':(0xb47947,'f20f11473c'),
        # pe_status_check_stance.
        'living GetStatus handles check_stance (type 0x15)':(0xb47cfb,'83f815'),
        'check_stance passes dirUnproj (+0x30) in and out':(0xb47e2a,'488d4f30'),
        'check_stance runs the stance test':(0xb47e61,'e89aea0000'),
        'check_stance writes the depth at +0x3C':(0xb47e74,'f30f11473c'),
        'check_stance returns 1 when the depth is zero':(0xb47e7b,'be01000000'),
        'the stance test returns zero when nothing overlaps':(0xb57615,'450f57ff'),
        "the stance test returns a contact's depth":(0xb57841,'f2440f103a'),
        'the stance test returns a constant for an overlapping living body':(0xb577ea,'f3440f103d79e31201'),
        'the stance test returns that value':(0xb5770e,'410f28c7'),
        # When a SetParams applies at once rather than after the step (the change request).
        'the living SetParams runs the change request first':(0xb4967e,'e84d26fdff'),
        "the change request queues while the world's step lock (+0x576C) is held":(0xb1bd83,'8b886c570000'),
        'the change request queues while world +0x57B0 is set':(0xb1bd94,'8b88b0570000'),
        "the change request's queued-changes unit is 0x20000":(0xb1bd78,'be00000200'),
        "the change request reads the entity's queued changes at +0x40":(0xb1bda2,'8b4340'),
        'the change request queues at one queued change or more':(0xb1bda5,'3bc60f83'),
        "the change request compares the caller with the physics thread (+0x10E8)":(0xb1bdbd,'8b8be8100000'),
        "the change request queues while the world lock (+0x5784) is held":(0xb1bdd9,'8b8884570000'),
        'applying at once takes the step lock':(0xb1bdfc,'f0440fb1a16c570000'),
    }
    for name,(rva,code) in witnesses.items():
        b=bytes.fromhex(code);assert at(rva,len(b))==b,name;checks.append(name)
    def rip(rva,length):return rva+length+struct.unpack('<i',at(rva+length-4,4))[0]
    assert rip(0xb47e61,5)==0xb56900
    checks.append('the call at 0xB47E61 reaches 0xB56900')
    assert rip(0xb4967e,5)==0xb1bcd0
    checks.append('the call at 0xB4967E reaches the change request 0xB1BCD0')
    overlap=struct.unpack('<f',at(rip(0xb577ea,9),4))[0]
    assert overlap!=0
    checks.append(f'an overlapping living body reports depth {overlap:g}: not zero, so it never fits')
    return {'target':str(DEFAULT_MODULE),'sha256':image.sha256,'environment':'static','game_launched':False,'checks':checks,
        'limits':'Proves native bytes, layouts and call routes. Not exercised: whether the stance test and the '
                 "living step agree on every collider in a scene, the queue's timing against the physics thread, "
                 'or how a moved body settles on stairs and ramps.'}

if __name__=='__main__':
    report=json.dumps(verify(),indent=2)+'\n'
    if len(sys.argv)>1:Path(sys.argv[1]).write_text(report,encoding='utf-8',newline='\n')
    print(report)
