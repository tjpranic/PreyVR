//==== 13B7A10  FUN_1813b7a10 entry=0x13B7A10

void FUN_1813b7a10(longlong param_1)

{
  longlong lVar1;
  undefined8 uVar2;
  
  FUN_181699800();
  FUN_1813b7dc0(param_1,"onWeaponEquipped",0);
  lVar1 = FUN_18157c990();
  uVar2 = FUN_1810839f0(lVar1 + 0x678);
  uVar2 = FUN_1815ad020(uVar2);
  *(undefined8 *)(param_1 + 0x4e0) = uVar2;
  FUN_1815d8e80(uVar2,param_1 + 0x4c0);
  return;
}


//==== 13BB1D0  FUN_1813bb1d0 entry=0x13BB1D0

void FUN_1813bb1d0(undefined8 param_1)

{
  FUN_181699800();
  FUN_1813bc4c0(param_1);
  return;
}


//==== 169F3F0  FUN_18169f3f0 entry=0x169F3F0

void FUN_18169f3f0(longlong param_1)

{
  FUN_181699800();
  *(undefined1 *)(param_1 + 0x4e8) = 0;
  FUN_18169f730(param_1);
  return;
}


//==== 16A29A0  FUN_1816a29a0 entry=0x16A29A0

void FUN_1816a29a0(longlong param_1)

{
  FUN_181699800();
  FUN_1816a3a20(param_1);
  *(undefined1 *)(param_1 + 0x584) = 1;
  return;
}


//==== 16A5DE0  FUN_1816a5de0 entry=0x16A5DE0

void FUN_1816a5de0(longlong *param_1)

{
  char cVar1;
  undefined4 uVar2;
  longlong lVar3;
  
  FUN_181699800();
  if (param_1[0x56] != 0) {
    cVar1 = (**(code **)(*param_1 + 0x208))(param_1);
    if (cVar1 != '\0') {
      (**(code **)(*(longlong *)param_1[0x56] + 0x88))((longlong *)param_1[0x56],0);
      (**(code **)(*(longlong *)param_1[0x56] + 0xa8))((longlong *)param_1[0x56],1);
    }
  }
  lVar3 = FUN_181665780();
  if (lVar3 != 0) {
    if (0 < *(int *)(DAT_182c16830 + 0x938)) {
      FUN_18118f690(lVar3,"ammoOpen");
    }
    uVar2 = (**(code **)(*param_1 + 0x28))(param_1);
    FUN_181330910(lVar3,"ammoUpdate",1,uVar2,1);
  }
  *(undefined1 *)((longlong)param_1 + 0x48b) = 1;
  *(undefined1 *)((longlong)param_1 + 0x522) = 0;
  return;
}


//==== 16AA8C0  FUN_1816aa8c0 entry=0x16AA8C0

void FUN_1816aa8c0(longlong *param_1)

{
  longlong lVar1;
  undefined4 uVar2;
  
  FUN_181699800();
  uVar2 = FUN_1816a80a0(param_1);
  *(undefined4 *)(param_1 + 0x9c) = uVar2;
  if (*(char *)((longlong)param_1 + 0x4ec) != '\0') {
    lVar1 = FUN_181665780();
    if (lVar1 != 0) {
      if (0 < *(int *)(DAT_182c16830 + 0x938)) {
        FUN_18118f690(lVar1,"ammoOpen");
      }
                    /* WARNING: Could not recover jumptable at 0x0001816aa91b. Too many branches */
                    /* WARNING: Treating indirect jump as call */
      (**(code **)(*param_1 + 0x218))(param_1);
      return;
    }
  }
  return;
}


//==== 13B7B60  FUN_1813b7b60 entry=0x13B7B60

void FUN_1813b7b60(longlong param_1)

{
  longlong lVar1;
  
  FUN_18169a000();
  FUN_1813b7dc0(param_1,"onWeaponUnequipped",0);
  *(undefined1 *)(param_1 + 0x504) = 0;
  lVar1 = FUN_18157c9c0();
  if (lVar1 != 0) {
    FUN_1815d7f40(*(undefined8 *)(param_1 + 0x4e0),0);
    FUN_1815d9d20(*(undefined8 *)(param_1 + 0x4e0),param_1 + 0x4c0);
    return;
  }
  return;
}


//==== 13BC2B0  FUN_1813bc2b0 entry=0x13BC2B0

void FUN_1813bc2b0(longlong *param_1,undefined1 param_2)

{
  if (*(char *)((longlong)param_1 + 0x511) != '\0') {
    *(undefined1 *)((longlong)param_1 + 0x511) = 0;
    (**(code **)(*param_1 + 0x108))();
  }
  FUN_181284dc0((longlong)param_1 + 0x514);
  (**(code **)(*param_1 + 0x228))(param_1);
  FUN_181184760(param_1 + 0xa8);
  FUN_181184760(param_1 + 0xb3);
  FUN_181184760(param_1 + 0xbe);
  FUN_18169c030(param_1);
  FUN_181692e40(param_1,0);
  FUN_18169a000(param_1,param_2);
  *(int *)(param_1 + 0xa4) = (int)param_1[0x9e];
  *(int *)((longlong)param_1 + 0x51c) = (int)param_1[0x9e];
  param_1[0x9f] = 0;
  return;
}


//==== 169F6C0  FUN_18169f6c0 entry=0x169F6C0

void FUN_18169f6c0(longlong *param_1,undefined1 param_2)

{
  (**(code **)(*param_1 + 0xf8))();
  *(undefined1 *)((longlong)param_1 + 0x4cc) = 0;
  FUN_181692e40(param_1,0);
  param_1[0x9f] = 0;
  *(undefined1 *)(param_1 + 0x9d) = 0;
  FUN_18169a000(param_1,param_2);
  return;
}


//==== 16A3530  FUN_1816a3530 entry=0x16A3530

void FUN_1816a3530(longlong param_1,undefined1 param_2)

{
  *(undefined1 *)(param_1 + 0x585) = 0;
  FUN_181692e40(param_1,0);
  FUN_18169a000(param_1,param_2);
  *(undefined4 *)(param_1 + 0x580) = *(undefined4 *)(param_1 + 0x504);
  *(undefined4 *)(param_1 + 0x57c) = *(undefined4 *)(param_1 + 0x504);
  *(undefined8 *)(param_1 + 0x510) = 0;
  return;
}


//==== 16A5EA0  FUN_1816a5ea0 entry=0x16A5EA0

void FUN_1816a5ea0(longlong *param_1,undefined1 param_2)

{
  char cVar1;
  undefined4 uVar2;
  longlong *plVar3;
  longlong lVar4;
  
  plVar3 = DAT_18224da38;
  lVar4 = *DAT_18224da38;
  uVar2 = (**(code **)(param_1[1] + 0x1d8))(param_1 + 1);
  plVar3 = (longlong *)(**(code **)(lVar4 + 0x70))(plVar3,uVar2);
  if (plVar3 != (longlong *)0x0) {
    cVar1 = (**(code **)(*plVar3 + 0x68))(plVar3);
    if ((cVar1 == '\0') && (param_1[0x56] != 0)) {
      cVar1 = (**(code **)(param_1[1] + 0x158))(param_1 + 1);
      if (cVar1 == '\0') {
        *(undefined1 *)((longlong)param_1 + 0x2c9) = 0;
      }
      (**(code **)(*param_1 + 0x1d8))(param_1);
      FUN_1816a5210(param_1);
      (**(code **)(*(longlong *)param_1[6] + 0x1b0))((longlong *)param_1[6],param_1 + 1,0);
      FUN_18169ca70(param_1,0,"deploy");
      *(undefined1 *)((longlong)param_1 + 0x521) = 0;
      *(undefined2 *)(param_1 + 0x9f) = 0;
      *(undefined1 *)((longlong)param_1 + 0x504) = 0;
      lVar4 = FUN_181665780();
      if (lVar4 != 0) {
        FUN_18118f690(lVar4,"ammoClose");
      }
    }
  }
  FUN_18169a000(param_1,param_2);
  return;
}


//==== 16AAB20  FUN_1816aab20 entry=0x16AAB20

void FUN_1816aab20(longlong param_1)

{
  longlong lVar1;
  
  FUN_18169a000();
  if (*(char *)(param_1 + 0x4ec) != '\0') {
    lVar1 = FUN_181665780();
    if (lVar1 != 0) {
      FUN_18118f690(lVar1,"ammoClose");
      return;
    }
  }
  return;
}


//==== 17392D0  FUN_1817392d0 entry=0x17392D0

void FUN_1817392d0(longlong param_1)

{
  FUN_18169a000();
  *(undefined4 *)(param_1 + 0x518) = 0;
  return;
}


// no function at 1692E60
//==== 1692E70  FUN_181692e70 entry=0x1692E70

void FUN_181692e70(longlong *param_1)

{
  longlong lVar1;
  longlong *plVar2;
  uint uVar3;
  uint uVar4;
  
  lVar1 = FUN_18157c9c0();
  if ((lVar1 != 0) && (plVar2 = (longlong *)FUN_181274c80(lVar1 + 0x14b8), plVar2 == param_1)) {
    *(undefined1 *)(lVar1 + 0x992) = 0;
  }
  *(undefined1 *)((longlong)param_1 + 0x48b) = 1;
  (**(code **)(*param_1 + 0xb0))(param_1);
  lVar1 = param_1[0x5b];
  if (lVar1 != 0) {
    *(uint *)(lVar1 + 0x2c) = *(uint *)(lVar1 + 0x2c) | 0x2001;
    uVar3 = 0;
    uVar4 = *(uint *)(*(longlong *)(lVar1 + 0x78) + -4) & 0x7fffffff;
    if (uVar4 != 0) {
      do {
        FUN_1803716f0(*(undefined8 *)(*(longlong *)(lVar1 + 0x78) + (longlong)(int)uVar3 * 8));
        uVar3 = uVar3 + 1;
      } while (uVar3 < uVar4);
    }
    plVar2 = (longlong *)param_1[0x5b];
    if ((plVar2 != (longlong *)0x0) &&
       (*(int *)(plVar2 + 10) = (int)plVar2[10] + -1, (int)plVar2[10] < 1)) {
      (**(code **)(*plVar2 + 0xd0))();
    }
    param_1[0x5b] = 0;
  }
  return;
}


//==== 1692F50  FUN_181692f50 entry=0x1692F50

void FUN_181692f50(longlong *param_1)

{
  char cVar1;
  longlong lVar2;
  undefined8 uVar3;
  longlong lVar4;
  undefined8 uVar5;
  code *local_48;
  undefined8 uStack_40;
  longlong *local_38;
  undefined4 uStack_30;
  undefined4 uStack_2c;
  undefined4 local_28;
  undefined4 uStack_24;
  undefined4 uStack_20;
  uint uStack_1c;
  longlong *local_18;
  undefined8 uStack_10;
  
  lVar2 = FUN_18157c990();
  uVar3 = (**(code **)(param_1[1] + 0x120))(param_1 + 1);
  lVar4 = FUN_18157c990();
  uVar5 = (**(code **)(param_1[1] + 0x120))(param_1 + 1);
  cVar1 = FUN_181275440(lVar4 + 0x14b8,uVar5);
  if (cVar1 == '\0') {
    if (param_1[0x6c] != 0) {
      FUN_18119a3a0();
      uVar5 = FUN_18119a460();
      if (param_1[0x6d] == 0) {
        local_48 = (code *)0x0;
        local_38 = (longlong *)0x0;
        local_28 = 0;
        uStack_24 = 0;
        uStack_20 = (undefined4)uStack_40;
        uStack_1c = uStack_40._4_4_;
        local_18 = (longlong *)0x0;
        uStack_10 = (undefined1 *)CONCAT44(uStack_2c,uStack_30);
        FUN_181405730(uVar5,param_1[0x6c],&local_28);
      }
      else {
        uStack_40 = (ulonglong)uStack_40._4_4_ << 0x20;
        uStack_10 = &LAB_18139e580;
        local_48 = FUN_181692a40;
        uStack_30 = 0x8139e580;
        uStack_2c = 1;
        local_38 = param_1;
        local_18 = param_1;
        FUN_181405730(uVar5,param_1[0x6c],&local_48);
        FUN_1814056b0(uVar5,param_1[0x6d]);
      }
    }
    FUN_181273e60(lVar2 + 0x14b8,uVar3);
  }
                    /* WARNING: Could not recover jumptable at 0x000181693090. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (**(code **)(*param_1 + 0x108))(param_1);
  return;
}


//==== 13B9E80  FUN_1813b9e80 entry=0x13B9E80

void FUN_1813b9e80(longlong param_1)

{
  FUN_181692e70();
  *(undefined1 *)(param_1 + 0x510) = 0;
  *(undefined1 *)(param_1 + 0x512) = 0;
  return;
}


//==== 16A5300  FUN_1816a5300 entry=0x16A5300

void FUN_1816a5300(longlong *param_1)

{
  int iVar1;
  longlong lVar2;
  longlong *plVar3;
  uint uVar4;
  uint uVar5;
  
  FUN_181692e70();
  *(undefined1 *)((longlong)param_1 + 0x2c9) = 0;
  if (*(int *)(DAT_182c16830 + 0xb34) == 0) {
    iVar1 = (**(code **)(*param_1 + 0x28))(param_1);
    if (iVar1 < 1) {
      lVar2 = FUN_18157c9c0();
      if (lVar2 != 0) {
        plVar3 = (longlong *)FUN_181274c80(lVar2 + 0x14b8);
        if (plVar3 == param_1) {
          FUN_1812773a0(lVar2 + 0x14b8,0,0);
        }
      }
      goto LAB_1816a5394;
    }
  }
  plVar3 = (longlong *)param_1[0x56];
  if (plVar3 != (longlong *)0x0) {
    (**(code **)(*plVar3 + 0x88))(plVar3,0);
    (**(code **)(*(longlong *)param_1[0x56] + 0xa8))((longlong *)param_1[0x56],1);
  }
LAB_1816a5394:
  lVar2 = param_1[0xa6];
  if (lVar2 != 0) {
    *(uint *)(lVar2 + 0x2c) = *(uint *)(lVar2 + 0x2c) | 0x2001;
    uVar4 = 0;
    uVar5 = *(uint *)(*(longlong *)(lVar2 + 0x78) + -4) & 0x7fffffff;
    if (uVar5 != 0) {
      do {
        FUN_1803716f0(*(undefined8 *)(*(longlong *)(lVar2 + 0x78) + (longlong)(int)uVar4 * 8));
        uVar4 = uVar4 + 1;
      } while (uVar4 < uVar5);
    }
    plVar3 = (longlong *)param_1[0xa6];
    if ((plVar3 != (longlong *)0x0) &&
       (*(int *)(plVar3 + 10) = (int)plVar3[10] + -1, (int)plVar3[10] < 1)) {
      (**(code **)(*plVar3 + 0xd0))();
    }
    param_1[0xa6] = 0;
  }
  return;
}


// no function at 16A07A0
//==== 169D8D0  FUN_18169d8d0 entry=0x169D8D0

void FUN_18169d8d0(longlong param_1)

{
  longlong *plVar1;
  
  if (*(char *)(param_1 + 0x88) != '\0') {
    plVar1 = (longlong *)FUN_1816a5650(*(undefined4 *)(param_1 + 0x80));
    if (plVar1 != (longlong *)0x0) {
      (**(code **)(*plVar1 + 0x100))(plVar1);
    }
    *(uint *)(param_1 + 0x2c) = *(uint *)(param_1 + 0x2c) | 0x20;
    return;
  }
  *(uint *)(param_1 + 0x2c) = *(uint *)(param_1 + 0x2c) | 0x20;
  return;
}


//==== 169DBD0  FUN_18169dbd0 entry=0x169DBD0

void FUN_18169dbd0(longlong param_1)

{
  longlong *plVar1;
  
  plVar1 = (longlong *)FUN_1816a5650(*(undefined4 *)(param_1 + 0x80));
  if (plVar1 != (longlong *)0x0) {
    (**(code **)(*plVar1 + 0x110))(plVar1);
  }
  *(undefined4 *)(param_1 + 0x28) = 0;
  *(uint *)(param_1 + 0x2c) = *(uint *)(param_1 + 0x2c) & 0xffffffdf;
  return;
}


//==== 169DCA0  FUN_18169dca0 entry=0x169DCA0

void FUN_18169dca0(longlong param_1)

{
  longlong lVar1;
  longlong lVar2;
  
  lVar1 = FUN_1816a5650(*(undefined4 *)(param_1 + 0x80));
  if (lVar1 != 0) {
    FUN_1816924e0(lVar1);
  }
  lVar1 = FUN_18157c990();
  lVar2 = FUN_181587c40(lVar1 + 0x678);
  if (((*(int *)(lVar1 + 0x1510) == 0) && (*(char *)(lVar2 + 0xd0) == '\0')) &&
     (*(char *)(lVar2 + 0xd1) == '\0')) {
    FUN_181583f50(lVar1,"empty",1);
  }
  *(uint *)(param_1 + 0x2c) = *(uint *)(param_1 + 0x2c) & 0xffffffdf;
  *(undefined4 *)(param_1 + 0x28) = 0;
  return;
}


//==== 169A039  FUN_18169a000 entry=0x169A000

void FUN_18169a000(longlong *param_1,char param_2)

{
  undefined4 uVar1;
  char cVar2;
  longlong *plVar3;
  undefined8 uVar4;
  longlong lVar5;
  longlong lVar6;
  uint uVar7;
  uint uVar8;
  longlong local_res8;
  
  plVar3 = (longlong *)FUN_18157c990();
  uVar1 = (undefined4)param_1[7];
  cVar2 = FUN_181275500(plVar3 + 0x297,uVar1);
  if (cVar2 != '\0') {
    (**(code **)(*param_1 + 0x228))(param_1);
    (**(code **)(*(longlong *)param_1[6] + 0x1b0))((longlong *)param_1[6],param_1 + 1,0);
    *(undefined2 *)((longlong)param_1 + 0x48c) = 0x100;
    *(undefined1 *)((longlong)param_1 + 0x2cb) = 0;
    if (param_1[0x8f] != 0) {
      FUN_18118f690(param_1[0x8f],"closeDurability");
      FUN_181284fb0(param_1 + 0x58);
    }
    FUN_18169d090(param_1,1);
    (**(code **)(*param_1 + 0x1b8))(param_1);
    *(undefined1 *)((longlong)param_1 + 0x2ca) = 0;
    FUN_18169cee0(param_1,0);
    uVar4 = FUN_18119a3a0();
    uVar4 = FUN_18119a440(uVar4);
    FUN_1816082d0(uVar4,uVar1,param_1[0x6b]);
    lVar6 = param_1[0x8e];
    if (lVar6 != 0) {
      if (((*(char *)((longlong)param_1 + 0x2c9) == '\0') ||
          (cVar2 = (**(code **)(*plVar3 + 0x118))(plVar3), cVar2 != '\0')) ||
         (lVar5 = FUN_181587c40(plVar3 + 0xcf), *(char *)(lVar5 + 0xd1) != '\0')) {
        if (((param_2 == '\0') || ((int)plVar3[0xb] == 0xb)) || ((int)plVar3[0xb] == 6)) {
          if (param_1[0x56] != 0) {
            FUN_1816924e0(param_1);
            FUN_181583f50(plVar3,"empty",1);
          }
        }
        else {
          lVar5 = FUN_181b77290(0x88);
          local_res8 = 0;
          if (lVar5 != 0) {
            local_res8 = FUN_18169d790(lVar5,uVar1,*(undefined4 *)(lVar6 + 200));
          }
          if (local_res8 != 0) {
            *(int *)(local_res8 + 0x50) = *(int *)(local_res8 + 0x50) + 1;
          }
          FUN_18169a5b0(param_1,&local_res8,DAT_181c7eb28);
        }
      }
      else if (param_2 == '\0') {
        FUN_1816924e0(param_1);
        FUN_181583f50(plVar3,"empty",1);
        *(undefined1 *)((longlong)param_1 + 0x2c9) = 0;
      }
      else {
        lVar5 = FUN_181b77290(0x90);
        local_res8 = 0;
        if (lVar5 != 0) {
          local_res8 = FUN_18169d5c0(lVar5,uVar1,*(undefined4 *)(lVar6 + 0xe8),
                                     *(undefined4 *)(lVar6 + 200));
        }
        if (local_res8 != 0) {
          *(int *)(local_res8 + 0x50) = *(int *)(local_res8 + 0x50) + 1;
        }
        FUN_18169a5b0(param_1,&local_res8,DAT_181c7eb28);
        *(undefined1 *)((longlong)param_1 + 0x2c9) = 0;
      }
      lVar6 = param_1[0x5a];
      if (lVar6 != 0) {
        *(uint *)(lVar6 + 0x2c) = *(uint *)(lVar6 + 0x2c) | 0x2001;
        uVar8 = 0;
        uVar7 = *(uint *)(*(longlong *)(lVar6 + 0x78) + -4) & 0x7fffffff;
        if (uVar7 != 0) {
          do {
            FUN_1803716f0(*(undefined8 *)(*(longlong *)(lVar6 + 0x78) + (longlong)(int)uVar8 * 8));
            uVar8 = uVar8 + 1;
          } while (uVar8 < uVar7);
        }
        plVar3 = (longlong *)param_1[0x5a];
        if ((plVar3 != (longlong *)0x0) &&
           (*(int *)(plVar3 + 10) = (int)plVar3[10] + -1, (int)plVar3[10] < 1)) {
          (**(code **)(*plVar3 + 0xd0))();
        }
        param_1[0x5a] = 0;
      }
      lVar6 = param_1[0x5b];
      if (lVar6 != 0) {
        *(uint *)(lVar6 + 0x2c) = *(uint *)(lVar6 + 0x2c) | 0x2001;
        uVar8 = 0;
        uVar7 = *(uint *)(*(longlong *)(lVar6 + 0x78) + -4) & 0x7fffffff;
        if (uVar7 != 0) {
          do {
            FUN_1803716f0(*(undefined8 *)(*(longlong *)(lVar6 + 0x78) + (longlong)(int)uVar8 * 8));
            uVar8 = uVar8 + 1;
          } while (uVar8 < uVar7);
        }
        plVar3 = (longlong *)param_1[0x5b];
        if ((plVar3 != (longlong *)0x0) &&
           (*(int *)(plVar3 + 10) = (int)plVar3[10] + -1, (int)plVar3[10] < 1)) {
          (**(code **)(*plVar3 + 0xd0))();
        }
        param_1[0x5b] = 0;
      }
    }
    lVar6 = FUN_18157c990();
    FUN_181490b70(lVar6 + 0x1898,(int)param_1[0x90],DAT_181c7eb40,0);
    *(undefined4 *)(param_1 + 0x90) = 0;
    *(undefined1 *)(param_1 + 0x91) = 1;
  }
  return;
}


