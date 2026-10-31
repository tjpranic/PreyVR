//==== 169AE60  FUN_18169ae60 entry=0x169AE60

void FUN_18169ae60(longlong *param_1,char param_2)

{
  int iVar1;
  char cVar2;
  int iVar3;
  int iVar4;
  float fVar5;
  
  if (((*(char *)((longlong)param_1 + 0x48c) != '\0') &&
      (*(char *)((longlong)param_1 + 0x48b) != '\0')) ||
     (*(char *)((longlong)param_1 + 0x489) != '\0')) {
    fVar5 = (float)FUN_1814ae9e0(param_1 + 0x35,&DAT_182bd7128);
    iVar3 = (**(code **)(*param_1 + 0x20))(param_1);
    iVar4 = (**(code **)(*param_1 + 0x30))(param_1);
    iVar1 = (int)fVar5 + iVar3;
    if (iVar4 < (int)fVar5 + iVar3) {
      iVar1 = iVar4;
    }
    iVar3 = (**(code **)(*param_1 + 0x28))(param_1);
    if (iVar3 < iVar1) {
      iVar1 = iVar3;
    }
    (**(code **)(*param_1 + 0x18))(param_1,iVar1);
    (**(code **)(*param_1 + 0x218))(param_1);
    if (*(char *)((longlong)param_1 + 0x489) != '\0') {
      if (*(char *)((longlong)param_1 + 0x48c) == '\0') {
LAB_18169af1a:
        *(undefined1 *)((longlong)param_1 + 0x489) = 0;
        *(undefined1 *)((longlong)param_1 + 0x2cb) = 0;
        return;
      }
      cVar2 = (**(code **)(*param_1 + 0x200))(param_1);
      if (cVar2 == '\0') goto LAB_18169af1a;
    }
    if (param_2 != '\0') {
      (**(code **)(*param_1 + 0xc0))(param_1);
    }
  }
  return;
}


//==== 169BD80  FUN_18169bd80 entry=0x169BD80

char FUN_18169bd80(longlong *param_1)

{
  char cVar1;
  longlong lVar2;
  longlong *plVar3;
  undefined8 *puVar4;
  ulonglong uVar5;
  ulonglong uVar6;
  float fVar7;
  
  cVar1 = (**(code **)(*param_1 + 0x88))();
  if (cVar1 != '\0') {
    FUN_181690b50(param_1);
    lVar2 = FUN_18157c9c0();
    if (lVar2 != 0) {
      plVar3 = (longlong *)FUN_181274c80(lVar2 + 0x14b8);
      if (plVar3 == param_1) {
        *(undefined1 *)(lVar2 + 0x992) = 1;
      }
    }
    *(undefined2 *)((longlong)param_1 + 0x2c9) = 1;
    fVar7 = (float)FUN_1814ae9e0(param_1 + 0x35,&DAT_182bd7130);
    if (0.0 < fVar7) {
      FUN_181284f80(param_1 + 0x57,DAT_181c7eb28 / fVar7,1);
    }
    puVar4 = (undefined8 *)param_1[0x84];
    uVar6 = 0;
    uVar5 = (ulonglong)(param_1[0x85] + (7 - (longlong)puVar4)) >> 3;
    if ((undefined8 *)param_1[0x85] < puVar4) {
      uVar5 = uVar6;
    }
    if (uVar5 != 0) {
      do {
        (**(code **)(*(longlong *)*puVar4 + 0x10))();
        uVar6 = uVar6 + 1;
        puVar4 = puVar4 + 1;
      } while (uVar6 != uVar5);
    }
    return cVar1;
  }
  return '\0';
}


//==== 16995B0  FUN_1816995b0 entry=0x16995B0

ulonglong FUN_1816995b0(longlong *param_1,undefined8 param_2,undefined8 param_3,int param_4)

{
  longlong lVar1;
  ulonglong in_RAX;
  ulonglong uVar2;
  longlong lVar3;
  longlong local_18 [2];
  
  if (param_4 == 1) {
    *(undefined1 *)((longlong)param_1 + 0x2ca) = 1;
    in_RAX = (**(code **)(*param_1 + 0xe8))();
  }
  else {
    if (param_4 == 2) {
      uVar2 = (**(code **)(*param_1 + 0xf8))();
      if ((char)uVar2 != '\0') {
        uVar2 = (**(code **)(*param_1 + 0x1b8))(param_1);
      }
      *(undefined1 *)((longlong)param_1 + 0x2ca) = 0;
      return uVar2 & 0xffffffffffffff00;
    }
    if (param_4 == 4) {
      in_RAX = FUN_181697a60();
      if ((char)in_RAX == '\0') {
        uVar2 = (**(code **)(*param_1 + 0xf0))(param_1);
        return uVar2 & 0xffffffffffffff00;
      }
      if (*(char *)((longlong)param_1 + 0x2c9) != '\0') {
        uVar2 = (**(code **)(*param_1 + 0xf8))(param_1);
        lVar1 = param_1[0x8e];
        if (lVar1 != 0) {
          lVar3 = FUN_181b77290(0x88);
          local_18[0] = 0;
          if (lVar3 != 0) {
            local_18[0] = FUN_18169d4d0(lVar3,(int)param_1[7],*(undefined4 *)(lVar1 + 0x10c));
          }
          if (local_18[0] != 0) {
            *(int *)(local_18[0] + 0x50) = *(int *)(local_18[0] + 0x50) + 1;
          }
          uVar2 = FUN_18169a5b0(param_1,local_18,DAT_181c7eb28);
        }
        *(undefined1 *)((longlong)param_1 + 0x48b) = 0;
        *(undefined1 *)((longlong)param_1 + 0x2ca) = 0;
        return uVar2 & 0xffffffffffffff00;
      }
    }
  }
  return in_RAX & 0xffffffffffffff00;
}


//==== 1262B60  FUN_181262b60 entry=0x1262B60

undefined1 FUN_181262b60(undefined8 param_1)

{
  undefined8 uVar1;
  char cVar2;
  longlong lVar3;
  uint *puVar4;
  undefined1 local_res10 [8];
  
  lVar3 = FUN_18157c990();
  uVar1 = FUN_181587ca0(lVar3 + 0x678);
  cVar2 = FUN_18148f9b0(uVar1,5);
  if (cVar2 == '\0') {
    cVar2 = FUN_18125b600(lVar3 + 0xaf0);
    if (cVar2 == '\0') {
      uVar1 = FUN_181587c20(lVar3 + 0x678);
      cVar2 = FUN_181261890(uVar1);
      if (cVar2 == '\0') {
        puVar4 = (uint *)FUN_18159d120(param_1,local_res10);
        if ((((0.0 < (float)(*puVar4 & DAT_181c7eb60)) || (0.0 < (float)(puVar4[1] & DAT_181c7eb60))
             ) && (*(char *)(lVar3 + 0x98c) != '\0')) && (*(char *)(lVar3 + 0x992) == '\0')) {
          return 1;
        }
      }
    }
  }
  return 0;
}


//==== 16A7FA0  FUN_1816a7fa0 entry=0x16A7FA0

float FUN_1816a7fa0(longlong param_1)

{
  char cVar1;
  int iVar2;
  longlong *plVar3;
  undefined8 uVar4;
  undefined8 *puVar5;
  float fVar6;
  float fVar7;
  
  fVar6 = (float)FUN_181695020(param_1,&DAT_182bd7018);
  fVar7 = (float)FUN_181695030(param_1,&DAT_182bd6f10);
  iVar2 = (**(code **)(*(longlong *)(param_1 + 8) + 0x1d8))(param_1 + 8);
  if ((iVar2 == 0x7777) && (plVar3 = (longlong *)FUN_18157c9c0(), plVar3 != (longlong *)0x0)) {
    uVar4 = FUN_18119a470(plVar3 + 0xcf);
    cVar1 = FUN_181272950(uVar4);
    if (cVar1 == '\0') {
      if ((*(char *)((longlong)plVar3 + 0x98c) == '\0') ||
         (*(char *)((longlong)plVar3 + 0x992) != '\0')) {
        iVar2 = (**(code **)(*plVar3 + 0x140))(plVar3);
        if (iVar2 == 1) {
          puVar5 = &DAT_182bd7030;
        }
        else {
          if (iVar2 != 2) goto LAB_1816a8077;
          puVar5 = &DAT_182bd7020;
        }
      }
      else {
        if ((int)plVar3[0xb] == 0xb) goto LAB_1816a8077;
        puVar5 = &DAT_182bd7038;
      }
    }
    else {
      puVar5 = &DAT_182bd7028;
    }
    fVar6 = (float)FUN_181695020(param_1,puVar5);
  }
LAB_1816a8077:
  *(float *)(param_1 + 0x4e8) = fVar7 * fVar6;
  return fVar7 * fVar6;
}


//==== 169BE80  FUN_18169be80 entry=0x169BE80

void FUN_18169be80(longlong *param_1)

{
  longlong lVar1;
  char cVar2;
  undefined1 uVar3;
  int iVar4;
  longlong lVar5;
  undefined4 uVar6;
  longlong local_res8;
  
  if (*(char *)((longlong)param_1 + 0x48b) != '\0') {
    if ((((*(char *)((longlong)param_1 + 0x48c) == '\0') &&
         (cVar2 = (**(code **)(*param_1 + 0x200))(), cVar2 != '\0')) &&
        (*(char *)((longlong)param_1 + 0x2ca) == '\0')) &&
       (*(char *)((longlong)param_1 + 0x2cb) == '\0')) {
      (**(code **)(*param_1 + 0xf8))(param_1);
      if ((*(char *)((longlong)param_1 + 0x329) == '\0') ||
         (iVar4 = (**(code **)(*param_1 + 0x20))(param_1), iVar4 == 0)) {
        uVar3 = 1;
      }
      else {
        uVar3 = 0;
      }
      lVar1 = param_1[0x8e];
      *(undefined1 *)((longlong)param_1 + 0x489) = uVar3;
      *(undefined1 *)((longlong)param_1 + 0x2cb) = 1;
      if (lVar1 != 0) {
        lVar5 = FUN_181b77290(0x88);
        if (lVar5 == 0) {
          local_res8 = 0;
        }
        else {
          local_res8 = FUN_18169d690(lVar5,(int)param_1[7],*(undefined4 *)(lVar1 + 0x184));
        }
        uVar6 = FUN_1814ae9e0(param_1 + 0x35,&DAT_182bd7140);
        if (local_res8 != 0) {
          *(int *)(local_res8 + 0x50) = *(int *)(local_res8 + 0x50) + 1;
        }
        FUN_18169a5b0(param_1,&local_res8,uVar6);
      }
    }
    return;
  }
  *(undefined1 *)((longlong)param_1 + 0x2cb) = 1;
  return;
}


