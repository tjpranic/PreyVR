//==== 1699800  FUN_181699800 entry=0x1699800

void FUN_181699800(longlong *param_1)

{
  undefined4 uVar1;
  undefined8 *puVar2;
  char cVar3;
  undefined1 uVar4;
  int iVar5;
  longlong lVar6;
  undefined8 uVar7;
  longlong *plVar8;
  longlong *plVar9;
  undefined8 *puVar10;
  longlong *plVar11;
  uint local_res8 [2];
  longlong *local_res10;
  longlong *local_res18;
  longlong *local_res20;
  int local_68 [2];
  longlong *local_60;
  int local_50 [2];
  longlong *local_48;
  
  FUN_18169cd70();
  iVar5 = (**(code **)(param_1[1] + 0x1d8))(param_1 + 1);
  local_res8[0] = *(uint *)(param_1 + 7);
  cVar3 = FUN_1814b0580(local_res8);
  if (cVar3 != '\0') {
    FUN_1814b04c0(local_res8);
    (**(code **)(*(longlong *)param_1[8] + 0x1a8))((longlong *)param_1[8],1);
  }
  lVar6 = FUN_18157c990();
  if (iVar5 != 0x7777) {
    return;
  }
  cVar3 = FUN_181275500(lVar6 + 0x14b8,local_res8[0]);
  if (cVar3 != '\0') {
    return;
  }
  FUN_1811c0c90(DAT_182c16840[0x61],&DAT_00007777,param_1 + 0x2e);
  uVar7 = FUN_18119a3a0();
  uVar7 = FUN_18119a440(uVar7);
  FUN_1816065f0(uVar7,local_res8[0],param_1[0x6b]);
  uVar7 = FUN_18119a3a0();
  uVar7 = FUN_181587c50(uVar7);
  FUN_1813eedc0(uVar7,0x10);
  plVar8 = (longlong *)(**(code **)(*DAT_18224dad0 + 0x60))(DAT_18224dad0,param_1[0x5f]);
  param_1[0x8f] = (longlong)plVar8;
  if (plVar8 != (longlong *)0x0) {
    (**(code **)(*plVar8 + 0xe0))(plVar8,1);
    if ((int)param_1[0x60] != 0) {
      FUN_181377560(param_1[0x8f],"setWeaponType");
    }
    (**(code **)(*DAT_18224da38 + 0x160))(DAT_18224da38,&DAT_00007777,0x33,param_1 + 0x30);
    FUN_18118f690(param_1[0x8f],"reloadEnd");
    uVar7 = FUN_181587ca0(lVar6 + 0x678);
    uVar4 = FUN_18148f9b0(uVar7,6);
    FUN_18169cee0(param_1,uVar4);
  }
  (**(code **)(*param_1 + 0xd8))(param_1);
  (**(code **)(*param_1 + 0x218))(param_1);
  plVar8 = (longlong *)(**(code **)(*DAT_182c16840 + 0x70))();
  plVar8 = (longlong *)(**(code **)(*plVar8 + 0xa8))(plVar8);
  plVar9 = (longlong *)FUN_181694800(param_1);
  uVar7 = (**(code **)(*plVar8 + 0x38))(plVar8);
  plVar11 = (longlong *)0x0;
  plVar8 = plVar11;
  if (plVar9 != (longlong *)0x0) {
    puVar10 = (undefined8 *)(**(code **)(*plVar9 + 0xa8))(plVar9);
    plVar8 = (longlong *)FUN_1816345d0(uVar7,*puVar10);
  }
  param_1[0x8e] = (longlong)(plVar8 + 1);
  FUN_18169b450(param_1);
  *(undefined1 *)((longlong)param_1 + 0x48c) = 0;
  *(undefined1 *)((longlong)param_1 + 0x2cb) = 0;
  FUN_18169cd10(param_1);
  plVar8 = (longlong *)FUN_1810dfce0(param_1 + 1,&local_res20);
  local_res10 = (longlong *)0x0;
  plVar8 = (longlong *)*plVar8;
  if (plVar8 != (longlong *)0x0) {
    local_68[0] = 6;
    local_60 = (longlong *)0x0;
    cVar3 = (**(code **)(*plVar8 + 0x38))(plVar8,"Weapon",local_68,0);
    plVar8 = local_60;
    if (cVar3 == '\0') {
LAB_181699a90:
      if (local_68[0] != 6) goto LAB_181699aa6;
      if (local_60 != (longlong *)0x0) {
        (**(code **)(*local_60 + 0x18))(local_60);
      }
    }
    else {
      if (local_68[0] == 6) {
        if (local_60 != (longlong *)0x0) {
          (**(code **)(*local_60 + 0x10))(local_60);
        }
        if (local_res10 != (longlong *)0x0) {
          (**(code **)(*local_res10 + 0x18))();
        }
        local_res10 = plVar8;
        goto LAB_181699a90;
      }
LAB_181699aa6:
      if ((local_68[0] == 7) && (local_60 != (longlong *)0x0)) {
        (**(code **)(*DAT_18224d9c0 + 0xc0))();
      }
    }
    local_60 = (longlong *)0x0;
    local_68[0] = 0;
  }
  FUN_1814acef0(&local_res18,&local_res10);
  if (local_res10 != (longlong *)0x0) {
    (**(code **)(*local_res10 + 0x18))();
  }
  plVar8 = (longlong *)&DAT_181c7ce7b;
  if (local_res18 != (longlong *)0x0) {
    local_50[0] = 5;
    cVar3 = (**(code **)(*local_res18 + 0x38))(local_res18,"sAnimTag",local_50,0);
    if ((cVar3 == '\0') || (plVar8 = local_48, local_50[0] != 5)) {
      if (local_50[0] == 6) {
        plVar8 = (longlong *)&DAT_181c7ce7b;
        if (local_48 != (longlong *)0x0) {
          (**(code **)(*local_48 + 0x18))();
        }
      }
      else {
        plVar8 = (longlong *)&DAT_181c7ce7b;
        if ((local_50[0] == 7) && (local_48 != (longlong *)0x0)) {
          (**(code **)(*DAT_18224d9c0 + 0xc0))();
        }
      }
    }
    local_48 = (longlong *)0x0;
    local_50[0] = 0;
  }
  FUN_18169ca70(param_1,1,plVar8);
  if (local_res18 != (longlong *)0x0) {
    (**(code **)(*local_res18 + 0x18))();
  }
  if (local_res20 != (longlong *)0x0) {
    (**(code **)(*local_res20 + 0x18))();
  }
  puVar10 = (undefined8 *)param_1[0x4a];
  for (puVar2 = (undefined8 *)*puVar10; puVar2 != puVar10; puVar2 = (undefined8 *)*puVar2) {
    uVar7 = FUN_181695bf0();
    lVar6 = FUN_1810b4950(uVar7,puVar2[2]);
    if (lVar6 != 0) {
      lVar6 = FUN_1810b48f0(lVar6,*(undefined4 *)(puVar2 + 3));
      FUN_18169ca70(param_1,1,*(undefined8 *)(lVar6 + 0x18));
    }
  }
  if (param_1[0x8e] == 0) goto LAB_181699c86;
  lVar6 = FUN_18157c990();
  uVar7 = (**(code **)(param_1[1] + 0x120))(param_1 + 1);
  cVar3 = FUN_181275440(lVar6 + 0x14b8,uVar7);
  if ((cVar3 == '\0') && (0 < *(int *)(DAT_182c16830 + 0xe5c))) {
    lVar6 = FUN_181b77290(0x90);
    if (lVar6 != 0) {
      uVar7 = 0;
      uVar1 = *(undefined4 *)(param_1[0x8e] + 0xcc);
LAB_181699c57:
      plVar11 = (longlong *)FUN_18169d6f0(lVar6,(int)param_1[7],uVar1,uVar7);
    }
  }
  else {
    lVar6 = FUN_181b77290(0x90);
    if (lVar6 != 0) {
      uVar7 = 1;
      uVar1 = *(undefined4 *)(param_1[0x8e] + 0xc4);
      goto LAB_181699c57;
    }
  }
  if (plVar11 != (longlong *)0x0) {
    *(int *)(plVar11 + 10) = (int)plVar11[10] + 1;
  }
  local_res10 = plVar11;
  FUN_18169a5b0(param_1,&local_res10,DAT_181c7eb28);
LAB_181699c86:
  (**(code **)(*param_1 + 0xf8))(param_1);
  (**(code **)(*(longlong *)param_1[6] + 0x1a8))((longlong *)param_1[6],param_1 + 1,0);
  return;
}


//==== 169B450  FUN_18169b450 entry=0x169B450

void FUN_18169b450(longlong param_1)

{
  char cVar1;
  int iVar2;
  int iVar3;
  longlong *plVar4;
  longlong *plVar5;
  undefined4 *puVar6;
  undefined8 uVar7;
  longlong lVar8;
  longlong *_Dst;
  size_t _Size;
  longlong *local_res8;
  longlong *local_res10;
  longlong *local_res18 [2];
  int local_68 [2];
  longlong *local_60;
  int local_50 [2];
  longlong *local_48;
  size_t sVar9;
  
  iVar2 = (**(code **)(*(longlong *)(param_1 + 8) + 0x1d8))(param_1 + 8);
  plVar4 = (longlong *)(**(code **)(*DAT_18224da38 + 0x70))(DAT_18224da38,iVar2);
  if (plVar4 == (longlong *)0x0) goto LAB_18169b6f8;
  plVar4 = (longlong *)(**(code **)(*plVar4 + 0x2d8))(plVar4,0);
  if ((plVar4 == (longlong *)0x0) ||
     (plVar4 = (longlong *)(**(code **)(*plVar4 + 0x48))(plVar4), plVar4 == (longlong *)0x0))
  goto LAB_18169b6f8;
  plVar5 = (longlong *)FUN_1810dfce0(param_1 + 8,local_res18);
  local_res8 = (longlong *)0x0;
  plVar5 = (longlong *)*plVar5;
  if (plVar5 != (longlong *)0x0) {
    local_68[0] = 6;
    local_60 = (longlong *)0x0;
    cVar1 = (**(code **)(*plVar5 + 0x38))(plVar5,"Weapon",local_68,0);
    plVar5 = local_60;
    if (cVar1 == '\0') {
LAB_18169b53e:
      if (local_68[0] != 6) goto LAB_18169b554;
      if (local_60 != (longlong *)0x0) {
        (**(code **)(*local_60 + 0x18))(local_60);
      }
    }
    else {
      if (local_68[0] == 6) {
        if (local_60 != (longlong *)0x0) {
          (**(code **)(*local_60 + 0x10))(local_60);
        }
        if (local_res8 != (longlong *)0x0) {
          (**(code **)(*local_res8 + 0x18))();
        }
        local_res8 = plVar5;
        goto LAB_18169b53e;
      }
LAB_18169b554:
      if ((local_68[0] == 7) && (local_60 != (longlong *)0x0)) {
        (**(code **)(*DAT_18224d9c0 + 0xc0))();
      }
    }
    local_60 = (longlong *)0x0;
    local_68[0] = 0;
  }
  FUN_1814acef0(&local_res10,&local_res8);
  if (local_res8 != (longlong *)0x0) {
    (**(code **)(*local_res8 + 0x18))();
  }
  plVar5 = (longlong *)&DAT_181c7ce7b;
  if (local_res10 != (longlong *)0x0) {
    local_50[0] = 5;
    cVar1 = (**(code **)(*local_res10 + 0x38))(local_res10,"sAttachmentName",local_50,0);
    if ((cVar1 == '\0') || (plVar5 = local_48, local_50[0] != 5)) {
      if (local_50[0] == 6) {
        plVar5 = (longlong *)&DAT_181c7ce7b;
        if (local_48 != (longlong *)0x0) {
          (**(code **)(*local_48 + 0x18))();
        }
      }
      else {
        plVar5 = (longlong *)&DAT_181c7ce7b;
        if ((local_50[0] == 7) && (local_48 != (longlong *)0x0)) {
          (**(code **)(*DAT_18224d9c0 + 0xc0))();
        }
      }
    }
    local_48 = (longlong *)0x0;
    local_50[0] = 0;
  }
  _Dst = (longlong *)&DAT_18224d03c;
  if (plVar5 != (longlong *)0x0) {
    _Size = 0xffffffffffffffff;
    do {
      sVar9 = _Size;
      _Size = sVar9 + 1;
    } while (*(char *)((longlong)plVar5 + _Size) != '\0');
    if (_Size != 0) {
      puVar6 = (undefined4 *)FUN_1800a8b30(sVar9 + 0xe,0xf,1);
      *puVar6 = 1;
      _Dst = (longlong *)(puVar6 + 3);
      puVar6[1] = (int)_Size;
      puVar6[2] = (int)_Size;
      *(undefined1 *)(_Size + (longlong)_Dst) = 0;
      if (_Dst != plVar5) {
        memcpy(_Dst,plVar5,_Size);
      }
    }
  }
  if (local_res10 != (longlong *)0x0) {
    (**(code **)(*local_res10 + 0x18))();
  }
  if (local_res18[0] != (longlong *)0x0) {
    (**(code **)(*local_res18[0] + 0x18))();
  }
  if (iVar2 == 0x7777) {
    uVar7 = (**(code **)(*plVar4 + 0x38))(plVar4,_Dst);
    *(undefined8 *)(param_1 + 0x2b0) = uVar7;
  }
  if (*(longlong *)(param_1 + 0x2b0) == 0) {
    plVar4 = (longlong *)(**(code **)(*DAT_18224da38 + 0x70))(DAT_18224da38,iVar2);
    uVar7 = (**(code **)(*plVar4 + 0x78))(plVar4);
    FUN_1802b17f0("Item owner \'%s\' doesn\'t have item attachment point \'%s\'!",uVar7,_Dst);
  }
  if ((-1 < *(int *)((longlong)_Dst + -0xc)) &&
     (iVar3 = *(int *)((longlong)_Dst + -0xc) + -1, *(int *)((longlong)_Dst + -0xc) = iVar3,
     iVar3 < 1)) {
    FUN_1800a8b20();
  }
LAB_18169b6f8:
  FUN_1816914f0(param_1);
  FUN_18169c6f0(param_1);
  if (iVar2 == 0x7777) {
    (**(code **)(*(longlong *)(param_1 + 0x438) + 0x10))(param_1 + 0x438);
    lVar8 = FUN_18157c990();
    uVar7 = FUN_180865b00(lVar8 + 0x678);
    FUN_181264d80(uVar7,param_1 + 0x438);
    FUN_181264dc0(uVar7,param_1 + 0x438,1);
    FUN_18169cba0(param_1,3);
  }
  return;
}


