//==== 1637940  FUN_181637940 entry=0x1637940

void FUN_181637940(int *param_1,longlong param_2)

{
  int iVar1;
  int *piVar2;
  longlong lVar3;
  int iVar4;
  int iVar5;
  uint uVar6;
  
  iVar1 = FUN_180d58d50("Wpn_DualWrench_Attack_A");
  iVar4 = 0;
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181637997;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181637997:
  *param_1 = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_Attack_B");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_1816379da;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_1816379da:
  param_1[1] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_Attack_C");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181637a1e;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181637a1e:
  param_1[2] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_Attack_D");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181637a67;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181637a67:
  param_1[3] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_Attack_E");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181637aab;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181637aab:
  param_1[4] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_Attack_F");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181637aef;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181637aef:
  param_1[5] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_Attack_G");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181637b37;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181637b37:
  param_1[6] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_BigHit_A");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181637b7b;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181637b7b:
  param_1[7] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_BigHit_B");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181637bbf;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181637bbf:
  param_1[8] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_BigHit_C");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181637c07;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181637c07:
  param_1[9] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_BigHit_D");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181637c4b;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181637c4b:
  param_1[10] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_BigHit_E");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181637c8f;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181637c8f:
  param_1[0xb] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_BigHit_F");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181637cd7;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181637cd7:
  param_1[0xc] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_BigHit_G");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181637d1b;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181637d1b:
  param_1[0xd] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_BigHit_Enemy_A");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181637d5f;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181637d5f:
  param_1[0xe] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_BigHit_Enemy_B");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181637da7;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181637da7:
  param_1[0xf] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_BigHit_Enemy_C");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181637deb;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181637deb:
  param_1[0x10] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_BigHit_Enemy_D");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181637e2f;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181637e2f:
  param_1[0x11] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_BigHit_Enemy_E");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181637e77;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181637e77:
  param_1[0x12] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_BigHit_Enemy_F");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181637ebb;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181637ebb:
  param_1[0x13] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_BigHit_Enemy_G");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181637eff;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181637eff:
  param_1[0x14] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_BigHit_A2B");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181637f47;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181637f47:
  param_1[0x15] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_BigHit_B2C");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181637f8b;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181637f8b:
  param_1[0x16] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_BigHit_C2D");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181637fcf;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181637fcf:
  param_1[0x17] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_BigHit_D2E");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638017;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638017:
  param_1[0x18] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_BigHit_E2F");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_18163805b;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_18163805b:
  param_1[0x19] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_BigHit_F2G");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_18163809f;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_18163809f:
  param_1[0x1a] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_BigHit_G2A");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_1816380e7;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_1816380e7:
  param_1[0x1b] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_BigHit_Enemy_A2B");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_18163812b;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_18163812b:
  param_1[0x1c] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_BigHit_Enemy_B2C");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_18163816f;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_18163816f:
  param_1[0x1d] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_BigHit_Enemy_C2D");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_1816381b7;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_1816381b7:
  param_1[0x1e] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_BigHit_Enemy_D2E");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_1816381fb;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_1816381fb:
  param_1[0x1f] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_BigHit_Enemy_E2F");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_18163823f;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_18163823f:
  param_1[0x20] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_BigHit_Enemy_F2G");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638287;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638287:
  param_1[0x21] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_BigHit_Enemy_G2A");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_1816382ce;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_1816382ce:
  param_1[0x22] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_DualWrench_Fatality_Generic");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638317;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638317:
  param_1[0x23] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Melee_BigHit_LeftRight");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_18163835e;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_18163835e:
  param_1[0x24] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Melee_BigHit_RightLeft");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_1816383a7;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_1816383a7:
  param_1[0x25] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Melee_BigHit_Enemy_RightLeft");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_1816383ee;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_1816383ee:
  param_1[0x26] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Melee_BigHit_Enemy_LeftRight");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638437;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638437:
  param_1[0x27] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Melee_In_LeftRight");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_18163847e;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_18163847e:
  param_1[0x28] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Melee_In_RightLeft");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_1816384c7;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_1816384c7:
  param_1[0x29] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Melee_LeftRight");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_18163850e;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_18163850e:
  param_1[0x2a] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Melee_RightLeft");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638557;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638557:
  param_1[0x2b] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Melee_Out_LeftRight");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_18163859e;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_18163859e:
  param_1[0x2c] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Melee_Out_RightLeft");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_1816385e7;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_1816385e7:
  param_1[0x2d] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Melee_Fatality_Generic");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_18163862e;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_18163862e:
  param_1[0x2e] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_FireMode");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638677;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638677:
  param_1[0x2f] = iVar5;
  iVar1 = FUN_180d58d50("unholster_select");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_1816386be;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_1816386be:
  param_1[0x30] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Select");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638707;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638707:
  param_1[0x31] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Unselect");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_18163874e;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_18163874e:
  param_1[0x32] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Pickup");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638797;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638797:
  param_1[0x33] = iVar5;
  iVar1 = FUN_180d58d50("unprime");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_1816387de;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_1816387de:
  param_1[0x34] = iVar5;
  iVar1 = FUN_180d58d50(&DAT_181e65b70);
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638827;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638827:
  param_1[0x35] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_OutOfAmmo");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_18163886e;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_18163886e:
  param_1[0x36] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Fire_In");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_1816388b7;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_1816388b7:
  param_1[0x37] = iVar5;
  iVar1 = FUN_180d58d50("fire_cock");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_1816388fe;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_1816388fe:
  param_1[0x38] = iVar5;
  iVar1 = FUN_180d58d50("Sfx_Fire_LastBullet");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638947;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638947:
  param_1[0x39] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Fire_Out");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_18163898e;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_18163898e:
  param_1[0x3a] = iVar5;
  iVar1 = FUN_180d58d50("spin_up");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_1816389d7;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_1816389d7:
  param_1[0x3b] = iVar5;
  iVar1 = FUN_180d58d50("spin_down");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638a1e;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638a1e:
  param_1[0x3c] = iVar5;
  iVar1 = FUN_180d58d50("spin_down_tail");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638a67;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638a67:
  param_1[0x3d] = iVar5;
  iVar1 = FUN_180d58d50("charge");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638aae;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638aae:
  param_1[0x3e] = iVar5;
  iVar1 = FUN_180d58d50("uncharge");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638af7;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638af7:
  param_1[0x3f] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_ChargeUp");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638b3e;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638b3e:
  param_1[0x40] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Fire");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638b87;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638b87:
  param_1[0x41] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Malfunction");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638bce;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638bce:
  param_1[0x42] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Emp");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638c17;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638c17:
  param_1[0x43] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_ChargeDown");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638c5e;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638c5e:
  param_1[0x44] = iVar5;
  iVar1 = FUN_180d58d50(&DAT_181d635e8);
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638ca7;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638ca7:
  param_1[0x45] = iVar5;
  iVar1 = FUN_180d58d50("prime");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638cee;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638cee:
  param_1[0x46] = iVar5;
  iVar1 = FUN_180d58d50("primed_loop");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638d37;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638d37:
  param_1[0x47] = iVar5;
  iVar1 = FUN_180d58d50("Throw");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638d7e;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638d7e:
  param_1[0x48] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Idle");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638dc7;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638dc7:
  param_1[0x49] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Throw");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638e0e;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638e0e:
  param_1[0x4a] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Throw_Cancel");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638e57;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638e57:
  param_1[0x4b] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Throw_Idle");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638e9e;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638e9e:
  param_1[0x4c] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Throw_In");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638ee7;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638ee7:
  param_1[0x4d] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Throw_Out");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638f2e;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638f2e:
  param_1[0x4e] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Deploy");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638f77;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638f77:
  param_1[0x4f] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Deploy_Ready_Idle");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181638fbe;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181638fbe:
  param_1[0x50] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Deploy_Ready_In");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181639007;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181639007:
  param_1[0x51] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Deploy_Ready_Out");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_18163904e;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_18163904e:
  param_1[0x52] = iVar5;
  iVar1 = FUN_180d58d50("fromPlant");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181639097;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181639097:
  param_1[0x53] = iVar5;
  iVar1 = FUN_180d58d50("plant");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_1816390de;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_1816390de:
  param_1[0x54] = iVar5;
  iVar1 = FUN_180d58d50("intoPlant");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181639127;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181639127:
  param_1[0x55] = iVar5;
  iVar1 = FUN_180d58d50("rip_off");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_18163916e;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_18163916e:
  param_1[0x56] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Modify");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_1816391b7;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_1816391b7:
  param_1[0x57] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Modify_Out");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_1816391fe;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_1816391fe:
  param_1[0x58] = iVar5;
  iVar1 = FUN_180d58d50("destroy");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181639247;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181639247:
  param_1[0x59] = iVar5;
  iVar1 = FUN_180d58d50("Sfx_Pick");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_18163928e;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_18163928e:
  param_1[0x5a] = iVar5;
  iVar1 = FUN_180d58d50("Sfx_Pick_Ammo");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_1816392d7;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_1816392d7:
  param_1[0x5b] = iVar5;
  iVar1 = FUN_180d58d50("friendly_enter");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_18163931e;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_18163931e:
  param_1[0x5c] = iVar5;
  iVar1 = FUN_180d58d50("friendly_leave");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181639367;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181639367:
  param_1[0x5d] = iVar5;
  iVar1 = FUN_180d58d50("weapon_lower_enter");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_1816393ae;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_1816393ae:
  param_1[0x5e] = iVar5;
  iVar1 = FUN_180d58d50("weapon_lower_leave");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_1816393f7;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_1816393f7:
  param_1[0x5f] = iVar5;
  iVar1 = FUN_180d58d50("meleeReaction");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_18163943e;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_18163943e:
  param_1[0x60] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Reload_In");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181639487;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181639487:
  param_1[0x61] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Reload");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_1816394ce;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_1816394ce:
  param_1[0x62] = iVar5;
  iVar1 = FUN_180d58d50("idle_lastGrenade");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181639517;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181639517:
  param_1[99] = iVar5;
  iVar1 = FUN_180d58d50("use_light");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_18163955e;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_18163955e:
  param_1[100] = iVar5;
  iVar1 = FUN_180d58d50("turret");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_1816395a7;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_1816395a7:
  param_1[0x65] = iVar5;
  iVar1 = FUN_180d58d50("cannon");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_1816395ee;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_1816395ee:
  param_1[0x66] = iVar5;
  iVar1 = FUN_180d58d50(&DAT_181d60204);
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181639637;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181639637:
  param_1[0x67] = iVar5;
  iVar1 = FUN_180d58d50("activate");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_18163967e;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_18163967e:
  param_1[0x68] = iVar5;
  iVar1 = FUN_180d58d50("deactivate");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_1816396c7;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_1816396c7:
  param_1[0x69] = iVar5;
  iVar1 = FUN_180d58d50(&DAT_181e65e54);
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_18163970e;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_18163970e:
  param_1[0x6a] = iVar5;
  iVar1 = FUN_180d58d50("Sfx_Zoom_In");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_181639757;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_181639757:
  param_1[0x6b] = iVar5;
  iVar1 = FUN_180d58d50("Sfx_Zoom_Out");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_18163979e;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_18163979e:
  param_1[0x6c] = iVar5;
  iVar1 = FUN_180d58d50("Wpn_Fire_Cancel");
  iVar5 = 0;
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) goto LAB_1816397e7;
      iVar5 = iVar5 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  iVar5 = -1;
LAB_1816397e7:
  param_1[0x6d] = iVar5;
  iVar1 = FUN_180d58d50("barrel_spin");
  uVar6 = *(uint *)(*(longlong *)(param_2 + 8) + -4) & 0x7fffffff;
  if (uVar6 != 0) {
    lVar3 = 0;
    piVar2 = (int *)(*(longlong *)(param_2 + 8) + 0x10);
    do {
      if (*piVar2 == iVar1) {
        param_1[0x6e] = iVar4;
        return;
      }
      iVar4 = iVar4 + 1;
      lVar3 = lVar3 + 1;
      piVar2 = piVar2 + 8;
    } while (lVar3 < (longlong)(ulonglong)uVar6);
  }
  param_1[0x6e] = -1;
  return;
}


