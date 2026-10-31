//==== 169DD20 FUN_18169dd20 entry=0x169DD20 size=580

void FUN_18169dd20(longlong param_1,uint param_2)

{
  byte bVar1;
  longlong *plVar2;
  longlong lVar3;
  byte bVar4;
  char *pcVar5;
  ulonglong uVar6;
  uint uVar7;
  uint uVar8;
  
  uVar7 = 0xffffffff;
  pcVar5 = "Throw";
  uVar8 = 0xffffffff;
  do {
    bVar1 = *pcVar5;
    pcVar5 = pcVar5 + 1;
    bVar4 = bVar1 + 0x20;
    if (0x19 < (byte)(bVar1 + 0xbf)) {
      bVar4 = bVar1;
    }
    uVar8 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar4 ^ (ulonglong)(uVar8 & 0xff)) * 4) ^
            uVar8 >> 8;
  } while (*pcVar5 != 0);
  if (param_2 == ~uVar8) {
    plVar2 = (longlong *)FUN_1816a5650(*(undefined4 *)(param_1 + 0x80));
    if (plVar2 != (longlong *)0x0) {
      (**(code **)(*plVar2 + 0x118))(plVar2);
    }
  }
  else {
    uVar8 = 0xffffffff;
    pcVar5 = "Deploy";
    do {
      bVar1 = *pcVar5;
      pcVar5 = pcVar5 + 1;
      bVar4 = bVar1 + 0x20;
      if (0x19 < (byte)(bVar1 + 0xbf)) {
        bVar4 = bVar1;
      }
      uVar8 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar4 ^ (ulonglong)(uVar8 & 0xff)) * 4) ^
              uVar8 >> 8;
    } while (*pcVar5 != 0);
    if (param_2 == ~uVar8) {
      plVar2 = (longlong *)FUN_1816a5650(*(undefined4 *)(param_1 + 0x80));
      if (plVar2 != (longlong *)0x0) {
        (**(code **)(*plVar2 + 0x120))(plVar2);
      }
    }
    else {
      uVar6 = 0xffffffff;
      pcVar5 = "Reload";
      do {
        bVar1 = *pcVar5;
        pcVar5 = pcVar5 + 1;
        bVar4 = bVar1 + 0x20;
        if (0x19 < (byte)(bVar1 + 0xbf)) {
          bVar4 = bVar1;
        }
        uVar8 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar4 ^ uVar6 & 0xff) * 4) ^
                (uint)(uVar6 >> 8);
        uVar6 = (ulonglong)uVar8;
      } while (*pcVar5 != 0);
      if (param_2 == ~uVar8) {
        plVar2 = (longlong *)FUN_1816a5650(*(undefined4 *)(param_1 + 0x80));
        if (plVar2 != (longlong *)0x0) {
          (**(code **)(*plVar2 + 0x128))(plVar2);
        }
      }
      else {
        pcVar5 = "InterruptFire";
        do {
          bVar1 = *pcVar5;
          pcVar5 = pcVar5 + 1;
          bVar4 = bVar1 + 0x20;
          if (0x19 < (byte)(bVar1 + 0xbf)) {
            bVar4 = bVar1;
          }
          uVar7 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar4 ^ (ulonglong)(uVar7 & 0xff)) * 4) ^
                  uVar7 >> 8;
        } while (*pcVar5 != 0);
        if (param_2 == ~uVar7) {
          lVar3 = FUN_1816a5650(*(undefined4 *)(param_1 + 0x80));
          if (lVar3 != 0) {
            *(undefined1 *)(lVar3 + 0x48b) = 1;
          }
        }
        else {
          uVar7 = FUN_180d58d50("GlowEnd");
          if (param_2 == uVar7) {
            plVar2 = (longlong *)FUN_1816a5650(*(undefined4 *)(param_1 + 0x80));
            if (plVar2 != (longlong *)0x0) {
              (**(code **)(*plVar2 + 0x1c8))(plVar2);
            }
          }
          else {
            uVar7 = FUN_180d58d50("MuzzleFlash");
            if (param_2 == uVar7) {
              lVar3 = FUN_1816a5650(*(undefined4 *)(param_1 + 0x80));
              if (lVar3 != 0) {
                FUN_181699f50(lVar3);
              }
            }
          }
        }
      }
    }
  }
  return;
}


//==== 169BFA0 FUN_18169bfa0 entry=0x169BFA0 size=130

undefined8 FUN_18169bfa0(longlong param_1)

{
  undefined8 *puVar1;
  ulonglong uVar2;
  ulonglong uVar3;
  
  if (*(char *)(param_1 + 0x2c9) != '\0') {
    puVar1 = *(undefined8 **)(param_1 + 0x420);
    *(undefined2 *)(param_1 + 0x2c9) = 0;
    uVar3 = 0;
    uVar2 = (ulonglong)((longlong)*(undefined8 **)(param_1 + 0x428) + (7 - (longlong)puVar1)) >> 3;
    if (*(undefined8 **)(param_1 + 0x428) < puVar1) {
      uVar2 = uVar3;
    }
    if (uVar2 != 0) {
      do {
        (**(code **)(*(longlong *)*puVar1 + 0x18))();
        uVar3 = uVar3 + 1;
        puVar1 = puVar1 + 1;
      } while (uVar3 != uVar2);
    }
    return 1;
  }
  return 0;
}


//==== 16A02B0 FUN_1816a02b0 entry=0x16A02B0 size=503

undefined8 FUN_1816a02b0(longlong *param_1)

{
  char cVar1;
  longlong lVar2;
  undefined4 *puVar3;
  int iVar4;
  size_t _Size;
  float fVar6;
  char *local_res8;
  size_t sVar5;
  
  if ((((*(char *)((longlong)param_1 + 0x584) == '\0') &&
       (*(char *)((longlong)param_1 + 0x48d) == '\0')) && (cVar1 = FUN_181692370(), cVar1 != '\0'))
     && (cVar1 = FUN_181697ab0(param_1), cVar1 != '\0')) {
    fVar6 = (float)(**(code **)(*DAT_18224da18 + 0x38))(DAT_18224da18,3);
    *(float *)(param_1 + 0xad) = fVar6 + *(float *)(param_1 + 0xad);
    (**(code **)(*param_1 + 0x1f8))(param_1);
    FUN_181692490(param_1);
    cVar1 = FUN_18169b780(param_1);
    if (cVar1 != '\0') {
      lVar2 = FUN_181b77290(0x88);
      local_res8 = (char *)0x0;
      if (lVar2 != 0) {
        local_res8 = (char *)FUN_18169d530(lVar2,(int)param_1[7],
                                           *(undefined4 *)(param_1[0x8e] + 0x108));
      }
      if (local_res8 != (char *)0x0) {
        *(int *)(local_res8 + 0x50) = *(int *)(local_res8 + 0x50) + 1;
      }
      FUN_18169a5b0(param_1,&local_res8,DAT_181c7eb28);
      if (((*(longlong *)(DAT_182c16840 + 0x1a8) != 0) &&
          (lVar2 = FUN_18182c080(*(longlong *)(DAT_182c16840 + 0x1a8),"ArkUIHUD"), lVar2 != 0)) &&
         (lVar2 + -0x18 != 0)) {
        local_res8 = &DAT_18224d03c;
        _Size = 0xffffffffffffffff;
        do {
          sVar5 = _Size;
          _Size = sVar5 + 1;
        } while ("@ui_WeaponMalfunction"[sVar5 + 1] != '\0');
        if (_Size != 0) {
          puVar3 = (undefined4 *)FUN_1800a8b30(sVar5 + 0xe,0xf,1);
          *puVar3 = 1;
          local_res8 = (char *)(puVar3 + 3);
          puVar3[1] = (int)_Size;
          puVar3[2] = (int)_Size;
          local_res8[_Size] = '\0';
          if (local_res8 != "@ui_WeaponMalfunction") {
            memcpy(local_res8,"@ui_WeaponMalfunction",_Size);
          }
        }
        FUN_1816695e0(lVar2 + -0x18,&local_res8,DAT_181c82bd4,1);
        if ((-1 < *(int *)(local_res8 + -0xc)) &&
           (iVar4 = *(int *)(local_res8 + -0xc) + -1, *(int *)(local_res8 + -0xc) = iVar4, iVar4 < 1
           )) {
          FUN_1800a8b20();
        }
      }
      *(undefined1 *)((longlong)param_1 + 0x584) = 1;
      FUN_18169bfa0(param_1);
      *(undefined2 *)((longlong)param_1 + 0x584) = 0x100;
      FUN_1816a3d80(param_1);
    }
    return 1;
  }
  return 0;
}


//==== 16A3B40 FUN_1816a3b40 entry=0x16A3B40 size=55

void FUN_1816a3b40(longlong param_1)

{
  char cVar1;
  
  if ((*(char *)(param_1 + 0x584) == '\0') && (*(char *)(param_1 + 0x48d) == '\0')) {
    cVar1 = FUN_181697ab0();
    if (cVar1 != '\0') {
      FUN_18169be80(param_1);
      return;
    }
  }
  return;
}


//==== 16A3C40 FUN_1816a3c40 entry=0x16A3C40 size=313

void FUN_1816a3c40(longlong *param_1)

{
  longlong *plVar1;
  char cVar2;
  longlong lVar3;
  longlong local_res8;
  
  if (((*(char *)((longlong)param_1 + 0x584) == '\0') &&
      (*(char *)((longlong)param_1 + 0x48d) == '\0')) && (param_1[0x8e] != 0)) {
    cVar2 = FUN_18169bd80();
    if (cVar2 != '\0') {
      cVar2 = FUN_181697ab0(param_1);
      if (cVar2 != '\0') {
        cVar2 = FUN_18169b780(param_1);
        if (cVar2 == '\0') {
          lVar3 = FUN_181b77290(0x88);
          if (lVar3 == 0) {
            local_res8 = 0;
          }
          else {
            local_res8 = FUN_18169d500(lVar3,(int)param_1[7],*(undefined4 *)(param_1[0x8e] + 0x104))
            ;
          }
          if (local_res8 != 0) {
            *(int *)(local_res8 + 0x50) = *(int *)(local_res8 + 0x50) + 1;
          }
          plVar1 = (longlong *)param_1[0x5b];
          if ((plVar1 != (longlong *)0x0) &&
             (*(int *)(plVar1 + 10) = (int)plVar1[10] + -1, (int)plVar1[10] < 1)) {
            (**(code **)(*plVar1 + 0xd0))();
          }
          param_1[0x5b] = local_res8;
          if (local_res8 != 0) {
            *(int *)(local_res8 + 0x50) = *(int *)(local_res8 + 0x50) + 1;
          }
          FUN_18169a5b0(param_1,&local_res8,DAT_181c7eb28);
          FUN_181671640(param_1 + 0xa4);
          FUN_18169bb90(param_1,1);
          FUN_181184fc0(param_1 + 0xbd);
          (**(code **)(*param_1 + 0x1f8))(param_1);
          FUN_181692490(param_1);
        }
      }
    }
    *(undefined1 *)((longlong)param_1 + 0x584) = 0;
    return;
  }
  *(undefined1 *)((longlong)param_1 + 0x584) = 0;
  return;
}


//==== 16A3AC0 FUN_1816a3ac0 entry=0x16A3AC0 size=126

char FUN_1816a3ac0(longlong param_1)

{
  char cVar1;
  longlong lVar2;
  longlong local_res8;
  
  cVar1 = FUN_181691e90();
  if (cVar1 != '\0') {
    lVar2 = FUN_181b77290(0x88);
    local_res8 = 0;
    if (lVar2 != 0) {
      local_res8 = FUN_18169d560(lVar2,*(undefined4 *)(param_1 + 0x38),
                                 *(undefined4 *)(*(longlong *)(param_1 + 0x470) + 0xdc));
    }
    if (local_res8 != 0) {
      *(int *)(local_res8 + 0x50) = *(int *)(local_res8 + 0x50) + 1;
    }
    FUN_18169a5b0(param_1,&local_res8,DAT_181c7eb28);
  }
  *(undefined4 *)(param_1 + 0x57c) = *(undefined4 *)(param_1 + 0x508);
  return cVar1;
}


//==== 1692370 FUN_181692370 entry=0x1692370 size=105

undefined8 FUN_181692370(longlong *param_1)

{
  char cVar1;
  
  if (((*(char *)((longlong)param_1 + 0x2c9) != '\0') &&
      (cVar1 = (**(code **)(*param_1 + 0x208))(), cVar1 != '\0')) &&
     (*(char *)((longlong)param_1 + 0x48c) == '\0')) {
    if (*(char *)((longlong)param_1 + 0x48b) == '\0') {
      return 0;
    }
    return 1;
  }
  cVar1 = (**(code **)(*param_1 + 0x208))(param_1);
  if (cVar1 == '\0') {
    (**(code **)(*param_1 + 0xb8))(param_1);
  }
  (**(code **)(*param_1 + 0xf8))(param_1);
  return 0;
}


//==== 169D920 FUN_18169d920 entry=0x169D920 size=54

void FUN_18169d920(longlong param_1)

{
  longlong *plVar1;
  
  plVar1 = (longlong *)FUN_1816a5650(*(undefined4 *)(param_1 + 0x80));
  if (plVar1 != (longlong *)0x0) {
    (**(code **)(*plVar1 + 0x108))(plVar1);
  }
  *(undefined4 *)(param_1 + 0x28) = 0;
  *(uint *)(param_1 + 0x2c) = *(uint *)(param_1 + 0x2c) & 0xffffffdf;
  return;
}


//==== 169D960 FUN_18169d960 entry=0x169D960 size=78

void FUN_18169d960(longlong param_1)

{
  longlong *plVar1;
  
  plVar1 = (longlong *)FUN_1816a5650(*(undefined4 *)(param_1 + 0x80));
  if (plVar1 != (longlong *)0x0) {
    (**(code **)(*plVar1 + 0x1b8))(plVar1);
    (**(code **)(*plVar1 + 0x108))(plVar1);
  }
  *(uint *)(param_1 + 0x2c) = *(uint *)(param_1 + 0x2c) & 0xffffffdf;
  *(undefined4 *)(param_1 + 0x28) = 0;
  return;
}


//==== 169D9F0 FUN_18169d9f0 entry=0x169D9F0 size=185

void FUN_18169d9f0(longlong param_1)

{
  longlong *plVar1;
  longlong lVar2;
  undefined8 *local_res8;
  
  plVar1 = (longlong *)FUN_1816a5650(*(undefined4 *)(param_1 + 0x80));
  if (plVar1 != (longlong *)0x0) {
    (**(code **)(*plVar1 + 0x108))(plVar1);
  }
  *(uint *)(param_1 + 0x2c) = *(uint *)(param_1 + 0x2c) & 0xffffffdf;
  *(undefined4 *)(param_1 + 0x28) = 0;
  lVar2 = FUN_1816a5650(*(undefined4 *)(param_1 + 0x80));
  if (lVar2 != 0) {
    local_res8 = (undefined8 *)FUN_181b77290(0x88);
    if (local_res8 == (undefined8 *)0x0) {
      local_res8 = (undefined8 *)0x0;
    }
    else {
      FUN_18169d420(local_res8,*(undefined4 *)(param_1 + 0x80),*(undefined4 *)(param_1 + 0x88),1);
      *local_res8 = &PTR_FUN_181e6d770;
    }
    if (local_res8 != (undefined8 *)0x0) {
      *(int *)(local_res8 + 10) = *(int *)(local_res8 + 10) + 1;
    }
    FUN_18169a5b0(lVar2,&local_res8,DAT_181c7eb28);
  }
  return;
}


//==== 169DAB0 FUN_18169dab0 entry=0x169DAB0 size=83

void FUN_18169dab0(longlong param_1)

{
  longlong *plVar1;
  longlong lVar2;
  
  plVar1 = (longlong *)FUN_1816a5650(*(undefined4 *)(param_1 + 0x80));
  if (plVar1 != (longlong *)0x0) {
    (**(code **)(*plVar1 + 0x108))(plVar1);
  }
  *(uint *)(param_1 + 0x2c) = *(uint *)(param_1 + 0x2c) & 0xffffffdf;
  *(undefined4 *)(param_1 + 0x28) = 0;
  lVar2 = FUN_1816a5650(*(undefined4 *)(param_1 + 0x80));
  if (lVar2 != 0) {
    FUN_1816a5420(lVar2);
    return;
  }
  return;
}


//==== 169DC10 FUN_18169dc10 entry=0x169DC10 size=66

void FUN_18169dc10(longlong param_1)

{
  longlong *plVar1;
  
  plVar1 = (longlong *)FUN_1816a5650(*(undefined4 *)(param_1 + 0x80));
  if ((plVar1 != (longlong *)0x0) && (*(char *)((longlong)plVar1 + 0x2c9) == '\0')) {
    (**(code **)(*plVar1 + 0xa0))(plVar1,0);
  }
  *(undefined4 *)(param_1 + 0x28) = 0;
  *(uint *)(param_1 + 0x2c) = *(uint *)(param_1 + 0x2c) & 0xffffffdf;
  return;
}


