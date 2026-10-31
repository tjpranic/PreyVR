//==== 16924E0 FUN_1816924e0 entry=0x16924E0 size=1119

undefined8 FUN_1816924e0(longlong param_1)

{
  undefined8 *puVar1;
  char cVar2;
  uint uVar3;
  longlong lVar4;
  undefined8 uVar5;
  longlong *plVar6;
  longlong *plVar7;
  undefined8 *puVar8;
  ulonglong uVar9;
  ulonglong uVar10;
  longlong *local_res8;
  longlong *local_res10;
  longlong *local_res18 [2];
  int local_58 [2];
  longlong *local_50;
  int local_40 [2];
  longlong *local_38;
  
  lVar4 = FUN_18157c9c0();
  if (lVar4 != 0) {
    uVar5 = FUN_180865b00(lVar4 + 0x678);
    FUN_181264dc0(uVar5,param_1 + 0x438,0);
    FUN_181264e10(uVar5,param_1 + 0x438);
  }
  FUN_1812648b0(param_1 + 0x438);
  plVar7 = *(longlong **)(param_1 + 0x40);
  if ((plVar7 == (longlong *)0x0) || (*(longlong *)(param_1 + 0x2b0) == 0)) {
    return 0;
  }
  plVar6 = (longlong *)(**(code **)(*DAT_18224da38 + 0x70))(DAT_18224da38,&DAT_00007777);
  if (plVar6 == (longlong *)0x0) {
    return 0;
  }
  cVar2 = (**(code **)(*plVar6 + 0x68))(plVar6);
  if (cVar2 != '\0') {
    return 0;
  }
  uVar3 = (**(code **)(**(longlong **)(param_1 + 0x40) + 0x2c8))(*(longlong **)(param_1 + 0x40),0);
  (**(code **)(**(longlong **)(param_1 + 0x40) + 0x2c0))(*(longlong **)(param_1 + 0x40),0,uVar3 | 3)
  ;
  plVar6 = (longlong *)(**(code **)(*plVar7 + 0x2d8))(plVar7,0);
  if (plVar6 != (longlong *)0x0) {
    lVar4 = *plVar6;
    uVar3 = (**(code **)(lVar4 + 0xd0))(plVar6);
    (**(code **)(lVar4 + 200))(plVar6,uVar3 | 8);
  }
  (**(code **)(**(longlong **)(param_1 + 0x2b0) + 0x88))(*(longlong **)(param_1 + 0x2b0),1);
  (**(code **)(**(longlong **)(param_1 + 0x2b0) + 0xa8))(*(longlong **)(param_1 + 0x2b0),0);
  (**(code **)(**(longlong **)(param_1 + 0x2b0) + 0xe0))(*(longlong **)(param_1 + 0x2b0),0);
  (**(code **)(*plVar7 + 0x1a8))(plVar7,1);
  FUN_1816917f0(param_1,0);
  plVar7 = (longlong *)FUN_1810dfce0(param_1 + 8,local_res18);
  uVar9 = 0;
  local_res8 = (longlong *)0x0;
  plVar7 = (longlong *)*plVar7;
  if (plVar7 == (longlong *)0x0) goto LAB_1816926d4;
  local_58[0] = 6;
  local_50 = (longlong *)0x0;
  cVar2 = (**(code **)(*plVar7 + 0x38))(plVar7,"Weapon",local_58,0);
  plVar7 = local_50;
  if (cVar2 == '\0') {
LAB_18169269c:
    if (local_58[0] != 6) goto LAB_1816926b2;
    if (local_50 != (longlong *)0x0) {
      (**(code **)(*local_50 + 0x18))(local_50);
    }
  }
  else {
    if (local_58[0] == 6) {
      if (local_50 != (longlong *)0x0) {
        (**(code **)(*local_50 + 0x10))(local_50);
      }
      if (local_res8 != (longlong *)0x0) {
        (**(code **)(*local_res8 + 0x18))();
      }
      local_res8 = plVar7;
      goto LAB_18169269c;
    }
LAB_1816926b2:
    if ((local_58[0] == 7) && (local_50 != (longlong *)0x0)) {
      (**(code **)(*DAT_18224d9c0 + 0xc0))();
    }
  }
  local_50 = (longlong *)0x0;
  local_58[0] = 0;
LAB_1816926d4:
  FUN_1814acef0(&local_res10,&local_res8);
  if (local_res8 != (longlong *)0x0) {
    (**(code **)(*local_res8 + 0x18))();
  }
  plVar7 = (longlong *)&DAT_181c7ce7b;
  if (local_res10 != (longlong *)0x0) {
    local_40[0] = 5;
    cVar2 = (**(code **)(*local_res10 + 0x38))(local_res10,"sAnimTag",local_40,0);
    if ((cVar2 == '\0') || (plVar7 = local_38, local_40[0] != 5)) {
      if (local_40[0] == 6) {
        plVar7 = (longlong *)&DAT_181c7ce7b;
        if (local_38 != (longlong *)0x0) {
          (**(code **)(*local_38 + 0x18))();
        }
      }
      else {
        plVar7 = (longlong *)&DAT_181c7ce7b;
        if ((local_40[0] == 7) && (local_38 != (longlong *)0x0)) {
          (**(code **)(*DAT_18224d9c0 + 0xc0))();
        }
      }
    }
    local_38 = (longlong *)0x0;
    local_40[0] = 0;
  }
  FUN_18169ca70(param_1,0,plVar7);
  if (local_res10 != (longlong *)0x0) {
    (**(code **)(*local_res10 + 0x18))();
  }
  if (local_res18[0] != (longlong *)0x0) {
    (**(code **)(*local_res18[0] + 0x18))();
  }
  FUN_18169ca70(param_1,0,"broken");
  puVar8 = *(undefined8 **)(param_1 + 0x250);
  for (puVar1 = (undefined8 *)*puVar8; puVar1 != puVar8; puVar1 = (undefined8 *)*puVar1) {
    uVar5 = FUN_181695bf0();
    lVar4 = FUN_1810b4950(uVar5,puVar1[2]);
    if (lVar4 != 0) {
      lVar4 = FUN_1810b48f0(lVar4,*(undefined4 *)(puVar1 + 3));
      FUN_18169ca70(param_1,0,*(undefined8 *)(lVar4 + 0x18));
    }
  }
  plVar7 = (longlong *)FUN_181694800(param_1);
  if (plVar7 != (longlong *)0x0) {
    plVar6 = (longlong *)(**(code **)(*DAT_18224da28 + 0x70))();
    plVar6 = (longlong *)(**(code **)(*plVar6 + 0xa8))(plVar6);
    uVar5 = (**(code **)(*plVar6 + 0x38))(plVar6);
    puVar8 = (undefined8 *)(**(code **)(*plVar7 + 0xa8))(plVar7);
    lVar4 = FUN_1816345d0(uVar5,*puVar8);
    if (*(int *)(lVar4 + 0x1e0) != -1) {
      (**(code **)(*plVar7 + 0x30))(plVar7,*(int *)(lVar4 + 0x1e0),1);
    }
  }
  *(undefined8 *)(param_1 + 0x2b0) = 0;
  *(undefined8 *)(param_1 + 0x470) = 0;
  *(undefined1 *)(param_1 + 0x48d) = 0;
  lVar4 = FUN_18157c990();
  if (*(int *)(lVar4 + 0x1510) != *(int *)(lVar4 + 0x1514)) {
    *(int *)(lVar4 + 0x1518) = *(int *)(lVar4 + 0x1514);
  }
  *(int *)(lVar4 + 0x1514) = *(int *)(lVar4 + 0x1510);
  *(undefined4 *)(lVar4 + 0x1510) = 0;
  *(undefined1 *)(lVar4 + 0x1530) = 0;
  FUN_1811c1d20(*(undefined8 *)(DAT_182c16840 + 0x308),&DAT_00007777,param_1 + 0x170);
  (**(code **)(*DAT_18224da38 + 0x168))(DAT_18224da38,&DAT_00007777,0x33,param_1 + 0x180);
  plVar7 = *(longlong **)(param_1 + 0x478);
  if (plVar7 != (longlong *)0x0) {
    (**(code **)(*plVar7 + 0xe0))(plVar7,0);
  }
  puVar8 = *(undefined8 **)(param_1 + 0x420);
  *(undefined8 *)(param_1 + 0x478) = 0;
  uVar10 = (ulonglong)((longlong)*(undefined8 **)(param_1 + 0x428) + (7 - (longlong)puVar8)) >> 3;
  if (*(undefined8 **)(param_1 + 0x428) < puVar8) {
    uVar10 = uVar9;
  }
  if (uVar10 != 0) {
    do {
      (**(code **)(*(longlong *)*puVar8 + 0x38))();
      uVar9 = uVar9 + 1;
      puVar8 = puVar8 + 1;
    } while (uVar9 != uVar10);
  }
  return 1;
}


//==== 12748D0 FUN_1812748d0 entry=0x12748D0 size=444

undefined8 FUN_1812748d0(longlong param_1,int param_2)

{
  char cVar1;
  int iVar2;
  longlong *plVar3;
  longlong lVar4;
  longlong *plVar5;
  longlong lVar6;
  longlong lVar7;
  
  if (*(int *)(param_1 + 0x58) == param_2) {
    return 0;
  }
  plVar3 = (longlong *)FUN_1816a5650(param_2);
  if (((plVar3 != (longlong *)0x0) && (*(char *)((longlong)plVar3 + 0x48d) == '\0')) &&
     (cVar1 = (**(code **)(plVar3[1] + 0x158))(plVar3 + 1), cVar1 != '\0')) {
    lVar4 = FUN_1816a5650(*(undefined4 *)(param_1 + 0x58));
    if (((lVar4 != 0) && (*(char *)(param_1 + 0x78) == '\0')) &&
       (*(undefined4 *)(param_1 + 100) = 0, *(int *)(param_1 + 0x58) != 0)) {
      FUN_18150cbc0();
      plVar5 = (longlong *)FUN_1816a5650(*(undefined4 *)(param_1 + 0x58));
      if (plVar5 != (longlong *)0x0) {
        *(undefined1 *)(param_1 + 0x78) = 1;
        (**(code **)(*plVar5 + 0x10))(plVar5,1);
      }
    }
    if ((*(int *)(param_1 + 0x58) == 0) && (cVar1 = FUN_181273eb0(param_1), cVar1 != '\0')) {
      cVar1 = FUN_18157d260(*(undefined8 *)(param_1 + 0x48),0x2bb4a6995540f6fc);
      if (cVar1 != '\0') {
        lVar6 = (**(code **)(plVar3[1] + 0x120))(plVar3 + 1);
        lVar4 = FUN_1813bd5a0();
        lVar7 = FUN_1813be8f0();
        if ((lVar6 == lVar7) || (lVar6 == lVar4)) {
          plVar5 = (longlong *)(*(longlong *)(*(longlong *)(param_1 + 0x48) + 0x1810) + 0x50);
          iVar2 = (**(code **)(*plVar5 + 0x20))(plVar5,lVar7);
          if ((iVar2 < 2) && (lVar4 = lVar7, iVar2 < 1)) {
            FUN_181583f50(*(undefined8 *)(param_1 + 0x48),"empty",1);
            return 0;
          }
          iVar2 = FUN_181274a90(param_1,lVar4);
          if (iVar2 != 0) {
            plVar3 = (longlong *)FUN_1816a5650(iVar2);
            param_2 = iVar2;
          }
        }
      }
      (**(code **)(*plVar3 + 8))(plVar3);
      *(int *)(param_1 + 0x58) = param_2;
      FUN_181583f50(*(undefined8 *)(param_1 + 0x48),"empty",param_2 == 0);
      FUN_18150cae0(param_2);
    }
    else {
      *(int *)(param_1 + 100) = param_2;
    }
    return 1;
  }
  return 0;
}


//==== 12773A0 FUN_1812773a0 entry=0x12773A0 size=108

void FUN_1812773a0(longlong param_1,undefined1 param_2,char param_3)

{
  longlong *plVar1;
  
  if (*(char *)(param_1 + 0x78) == '\0') {
    *(undefined4 *)(param_1 + 100) = 0;
    if (*(int *)(param_1 + 0x58) != 0) {
      FUN_18150cbc0();
      plVar1 = (longlong *)FUN_1816a5650(*(undefined4 *)(param_1 + 0x58));
      if (plVar1 != (longlong *)0x0) {
        *(undefined1 *)(param_1 + 0x78) = 1;
        if (param_3 != '\0') {
          *(undefined4 *)(param_1 + 100) = *(undefined4 *)(param_1 + 0x58);
        }
        (**(code **)(*plVar1 + 0x10))(plVar1,param_2);
      }
    }
  }
  return;
}


