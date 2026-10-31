//==== 169D6F0  FUN_18169d6f0 entry=0x169D6F0

undefined8 *
FUN_18169d6f0(undefined8 *param_1,undefined8 param_2,undefined8 param_3,undefined1 param_4)

{
  FUN_18169d420();
  *(undefined1 *)(param_1 + 0x11) = param_4;
  *param_1 = &PTR_FUN_181e6d698;
  return param_1;
}


//==== 169A5B0  FUN_18169a5b0 entry=0x169A5B0

void FUN_18169a5b0(undefined8 param_1,longlong *param_2,undefined4 param_3)

{
  longlong lVar1;
  longlong lVar2;
  undefined4 uVar3;
  longlong *plVar4;
  
  plVar4 = (longlong *)FUN_181694800();
  uVar3 = DAT_181c7eb40;
  if (plVar4 != (longlong *)0x0) {
    lVar1 = *param_2;
    if (lVar1 == 0) {
      return;
    }
    if (*(int *)(lVar1 + 0x38) != -1) {
      lVar2 = *plVar4;
      *(undefined4 *)(lVar1 + 0x54) = param_3;
      *(undefined4 *)(*param_2 + 0x58) = 0x3f800000;
      (**(code **)(lVar2 + 0x90))(plVar4,*param_2,uVar3);
    }
  }
  param_2 = (longlong *)*param_2;
  if ((param_2 != (longlong *)0x0) &&
     (*(int *)(param_2 + 10) = (int)param_2[10] + -1, (int)param_2[10] < 1)) {
    (**(code **)(*param_2 + 0xd0))();
  }
  return;
}


//==== 1275440  FUN_181275440 entry=0x1275440

undefined8 FUN_181275440(longlong param_1,longlong param_2)

{
  longlong *plVar1;
  longlong *plVar2;
  bool bVar3;
  
  plVar1 = *(longlong **)(param_1 + 0x88);
  plVar2 = *(longlong **)(param_1 + 0x80);
  bVar3 = plVar2 == plVar1;
  if (!bVar3) {
    do {
      if (*plVar2 == param_2) break;
      plVar2 = plVar2 + 1;
    } while (plVar2 != plVar1);
    bVar3 = plVar2 == plVar1;
  }
  return CONCAT71((int7)((ulonglong)plVar2 >> 8),!bVar3);
}


