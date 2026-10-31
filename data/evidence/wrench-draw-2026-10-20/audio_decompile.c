//==== 3AE010  FUN_1803ae010 entry=0x3AE010

void FUN_1803ae010(longlong param_1,longlong *param_2)

{
  int *piVar1;
  int local_res8 [2];
  int *local_res10;
  int *local_res18 [2];
  undefined **local_28;
  int *local_20;
  
  local_20 = (int *)(param_1 + 8);
  local_28 = &PTR_FUN_181ca4858;
  (**(code **)(*param_2 + 0x48))(param_2,&local_28,"StartTrigger","Start Trigger");
  local_20 = (int *)(param_1 + 0x10);
  local_28 = &PTR_FUN_181ca4858;
  (**(code **)(*param_2 + 0x48))(param_2,&local_28,"StopTrigger","Stop Trigger");
  (**(code **)(*param_2 + 0xa8))(param_2,param_1 + 0x20,"FireAndForget","Fire and Forget");
  piVar1 = (int *)(param_1 + 0x18);
  if ((*(byte *)(param_2 + 1) & 1) == 0) {
    if ((*(uint *)(param_2 + 1) >> 1 & 1) != 0) {
      local_res18[0] = piVar1;
      FUN_1803a3ac0(param_2,local_res18,"AttachmentJoint","Attachment Joint");
      goto LAB_1803ae132;
    }
  }
  else {
    local_res10 = local_res8;
    local_res8[0] = 0;
    FUN_1803a3ac0(param_2,&local_res10,"AttachmentJoint","Attachment Joint");
    if (local_res8[0] != 0) {
      *piVar1 = local_res8[0];
      goto LAB_1803ae132;
    }
  }
  local_28 = &PTR_FUN_181cb94f0;
  local_20 = piVar1;
  (**(code **)(*param_2 + 0x48))(param_2,&local_28,"AttachmentJoint","Attachment Joint");
LAB_1803ae132:
  (**(code **)(*param_2 + 0x58))(param_2,param_1 + 0x1c,"Radius","AI Radius");
  (**(code **)(*param_2 + 0xa8))(param_2,param_1 + 0x23,"IsVoice","Is Voice");
  (**(code **)(*param_2 + 0xa8))(param_2,param_1 + 0x24,"PlayFacial","Play Facial");
                    /* WARNING: Could not recover jumptable at 0x0001803ae1a8. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (**(code **)(*param_2 + 0x70))(param_2,param_1 + 0x28,"SoundFlags","Sound Flags");
  return;
}


//==== 3AE460  FUN_1803ae460 entry=0x3AE460

void FUN_1803ae460(longlong param_1,undefined8 param_2,undefined8 param_3,longlong param_4)

{
  undefined1 uVar1;
  longlong *plVar2;
  bool bVar3;
  int iVar4;
  int iVar5;
  undefined4 uVar6;
  longlong *plVar7;
  undefined1 local_28 [32];
  
  if (*(int *)(param_4 + 0x28) != 0) {
    plVar7 = (longlong *)(**(code **)(*DAT_18224da28 + 0x70))();
    bVar3 = false;
    if (plVar7 != (longlong *)0x0) {
      plVar2 = *(longlong **)(param_1 + 8);
      iVar4 = (**(code **)(*plVar7 + 0x230))(plVar7);
      iVar5 = (**(code **)(*plVar2 + 8))(plVar2);
      bVar3 = false;
      if (iVar4 == iVar5) {
        bVar3 = true;
      }
    }
    if (((*(uint *)(param_4 + 0x28) & 1) != 0) && (!bVar3)) {
      return;
    }
    if (((*(uint *)(param_4 + 0x28) & 2) != 0) && (bVar3)) {
      return;
    }
  }
  *(undefined4 *)(param_1 + 0x78) = 0xffffffff;
  uVar1 = *(undefined1 *)(param_4 + 0x24);
  plVar7 = (longlong *)(**(code **)(**(longlong **)(param_1 + 0x18) + 0x18))();
  if (plVar7 != (longlong *)0x0) {
    plVar7 = (longlong *)(**(code **)(*plVar7 + 0x50))(plVar7);
    uVar6 = (**(code **)(*plVar7 + 0x20))(plVar7,*(undefined4 *)(param_4 + 0x18));
    *(undefined4 *)(param_1 + 0x78) = uVar6;
  }
  FUN_1803aeca0(param_1,local_28,*(undefined4 *)(param_1 + 0x78));
  if ((*(char **)(param_4 + 8) != (char *)0x0) && (**(char **)(param_4 + 8) != '\0')) {
    (**(code **)(*DAT_18224da50 + 0x40))(DAT_18224da50,*(undefined8 *)(param_4 + 8),param_1 + 0x7c);
    if (*(int *)(param_1 + 0x7c) != 0) {
      FUN_1803aebe0(*(undefined8 *)(param_1 + 0x58),*(int *)(param_1 + 0x7c),uVar1);
    }
  }
  if (*(char *)(param_4 + 0x20) == '\0') {
    if ((*(char **)(param_4 + 0x10) != (char *)0x0) && (**(char **)(param_4 + 0x10) != '\0')) {
      (**(code **)(*DAT_18224da50 + 0x40))
                (DAT_18224da50,*(undefined8 *)(param_4 + 0x10),param_1 + 0x80);
    }
  }
  else {
    *(undefined8 *)(param_1 + 0x7c) = 0;
  }
  return;
}


//==== 3AE5B0  FUN_1803ae5b0 entry=0x3AE5B0

void FUN_1803ae5b0(longlong param_1)

{
  longlong *plVar1;
  
  if (*(int *)(param_1 + 0x80) == 0) {
    if (*(int *)(param_1 + 0x7c) == 0) {
      *(undefined8 *)(param_1 + 0x7c) = 0;
      return;
    }
    plVar1 = *(longlong **)(*(longlong *)(param_1 + 0x58) + 0x18);
    if (plVar1 != (longlong *)0x0) {
      (**(code **)(*plVar1 + 0xe0))(plVar1,*(int *)(param_1 + 0x7c),1);
      *(undefined8 *)(param_1 + 0x7c) = 0;
      return;
    }
  }
  else {
    FUN_1803aebe0(*(undefined8 *)(param_1 + 0x58),*(int *)(param_1 + 0x80),0);
  }
  *(undefined8 *)(param_1 + 0x7c) = 0;
  return;
}


//==== 3AEBE0  FUN_1803aebe0 entry=0x3AEBE0

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void FUN_1803aebe0(longlong param_1,undefined4 param_2,undefined1 param_3)

{
  longlong *plVar1;
  
  plVar1 = *(longlong **)(param_1 + 0x18);
  if (plVar1 != (longlong *)0x0) {
    if (*(int *)(*(longlong *)((longlong)ThreadLocalStoragePointer + (ulonglong)_tls_index * 8) +
                0x10) < DAT_182423d98) {
      _Init_thread_header(&DAT_182423d98);
      if (DAT_182423d98 == -1) {
        _DAT_182423d88 = 0;
        _DAT_182423d90 = 1;
        _DAT_182423d78 = 0;
        uRam0000000182423d80 = 0;
        _Init_thread_footer(&DAT_182423d98);
      }
    }
    (**(code **)(*plVar1 + 0xd8))(plVar1,param_2,param_3,1,&DAT_182423d78);
  }
  return;
}


//==== 940B90  FUN_180940b90 entry=0x940B90

void FUN_180940b90(longlong param_1,longlong *param_2,int param_3,undefined4 param_4)

{
  longlong *plVar1;
  int *piVar2;
  int iVar3;
  longlong lVar4;
  uint uVar5;
  longlong *plVar6;
  undefined8 uVar7;
  undefined8 in_stack_ffffffffffffff88;
  undefined4 uVar8;
  longlong *local_68;
  longlong *local_60;
  undefined8 local_58;
  longlong *local_50;
  undefined8 local_48;
  longlong *local_40;
  longlong local_38;
  ulonglong local_30;
  longlong local_28;
  undefined4 local_20;
  
  uVar8 = (undefined4)((ulonglong)in_stack_ffffffffffffff88 >> 0x20);
  plVar6 = (longlong *)FUN_1809435b0();
  if (plVar6 != (longlong *)0x0) {
    if (param_3 != 0) {
      (**(code **)(*plVar6 + 0x210))(plVar6,&local_58,3);
      if (local_50 != (longlong *)0x0) {
        LOCK();
        *(int *)(local_50 + 1) = (int)local_50[1] + 1;
        UNLOCK();
      }
      local_48 = local_58;
      local_40 = local_50;
      FUN_18097f0b0(&local_68,&local_48);
      if (local_50 != (longlong *)0x0) {
        LOCK();
        plVar1 = local_50 + 1;
        lVar4 = *plVar1;
        *(int *)plVar1 = (int)*plVar1 + -1;
        UNLOCK();
        if ((int)lVar4 == 1) {
          (**(code **)*local_50)(local_50);
          LOCK();
          piVar2 = (int *)((longlong)local_50 + 0xc);
          iVar3 = *piVar2;
          *piVar2 = *piVar2 + -1;
          UNLOCK();
          if (iVar3 == 1) {
            (**(code **)(*local_50 + 8))(local_50);
          }
        }
      }
      if (local_68 != (longlong *)0x0) {
        if (*(char *)(param_1 + 0x370) == '\0') {
          (**(code **)(*DAT_18224da50 + 0x20))
                    (DAT_18224da50,FUN_18094faa0,param_1,2,CONCAT44(uVar8,4));
          *(undefined1 *)(param_1 + 0x370) = 1;
        }
        uVar5 = (**(code **)(*plVar6 + 8))(plVar6);
        local_30 = (ulonglong)uVar5;
        local_20 = 0x11;
        local_38 = param_1;
        local_28 = param_1;
        (**(code **)(*local_68 + 0xd8))(local_68,param_3,0,param_4,&local_38);
      }
      plVar6 = local_60;
      if (local_60 != (longlong *)0x0) {
        LOCK();
        plVar1 = local_60 + 1;
        lVar4 = *plVar1;
        *(int *)plVar1 = (int)*plVar1 + -1;
        UNLOCK();
        if ((int)lVar4 == 1) {
          (**(code **)*local_60)(local_60);
          LOCK();
          piVar2 = (int *)((longlong)plVar6 + 0xc);
          iVar3 = *piVar2;
          *piVar2 = *piVar2 + -1;
          UNLOCK();
          if (iVar3 == 1) {
            (**(code **)(*local_60 + 8))();
          }
        }
      }
      (**(code **)(*param_2 + 0x58))(param_2);
      return;
    }
    uVar7 = (**(code **)(*plVar6 + 0x78))();
    FUN_180903900("Invalid Trigger ID, ignoring. Entity: \'%s\'",uVar7);
  }
  (**(code **)(*param_2 + 0x58))(param_2);
  return;
}


//==== 960F80  FUN_180960f80 entry=0x960F80

void FUN_180960f80(undefined8 param_1,longlong *param_2,int param_3,undefined4 param_4)

{
  longlong *plVar1;
  undefined8 uVar2;
  longlong *plVar3;
  
  plVar1 = (longlong *)FUN_1809435b0();
  if (plVar1 != (longlong *)0x0) {
    if (param_3 == 0) {
      uVar2 = (**(code **)(*plVar1 + 0x78))();
      FUN_180903900("Invalid Trigger ID, ignoring. Entity: \'%s\'",uVar2);
    }
    else {
      plVar3 = (longlong *)(**(code **)(*plVar1 + 0x200))(plVar1,3);
      if (plVar3 == (longlong *)0x0) {
        uVar2 = (**(code **)(*plVar1 + 0x78))(plVar1);
        FUN_180116120("StopAudioTrigger called with no Audio Proxy! Don\'t do this! Entity: %s",
                      uVar2);
      }
      else {
        (**(code **)(*plVar3 + 0xe0))(plVar3,param_3,param_4);
      }
    }
  }
                    /* WARNING: Could not recover jumptable at 0x000180961020. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (**(code **)(*param_2 + 0x58))(param_2);
  return;
}


//==== 16D0B00  FUN_1816d0b00 entry=0x16D0B00

void FUN_1816d0b00(uint *param_1)

{
  byte bVar1;
  byte bVar2;
  char *pcVar3;
  byte *pbVar4;
  uint uVar5;
  uint uVar6;
  ulonglong uVar7;
  
  if ((char)param_1[0x1b] == '\0') {
    uVar5 = 0xffffffff;
    pcVar3 = "sound";
    uVar6 = 0xffffffff;
    do {
      bVar1 = *pcVar3;
      pcVar3 = pcVar3 + 1;
      bVar2 = bVar1 + 0x20;
      if (0x19 < (byte)(bVar1 + 0xbf)) {
        bVar2 = bVar1;
      }
      uVar6 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar2 ^ (ulonglong)(uVar6 & 0xff)) * 4) ^
              uVar6 >> 8;
    } while (*pcVar3 != 0);
    pcVar3 = "PluginTrigger";
    *param_1 = ~uVar6;
    uVar6 = 0xffffffff;
    do {
      bVar1 = *pcVar3;
      pcVar3 = pcVar3 + 1;
      bVar2 = bVar1 + 0x20;
      if (0x19 < (byte)(bVar1 + 0xbf)) {
        bVar2 = bVar1;
      }
      uVar6 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar2 ^ (ulonglong)(uVar6 & 0xff)) * 4) ^
              uVar6 >> 8;
    } while (*pcVar3 != 0);
    pcVar3 = "footstep";
    param_1[1] = ~uVar6;
    uVar7 = 0xffffffff;
    do {
      bVar1 = *pcVar3;
      pcVar3 = pcVar3 + 1;
      bVar2 = bVar1 + 0x20;
      if (0x19 < (byte)(bVar1 + 0xbf)) {
        bVar2 = bVar1;
      }
      uVar6 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar2 ^ uVar7 & 0xff) * 4) ^ (uint)(uVar7 >> 8)
      ;
      uVar7 = (ulonglong)uVar6;
    } while (*pcVar3 != 0);
    pcVar3 = "foley";
    param_1[2] = ~uVar6;
    uVar6 = 0xffffffff;
    do {
      bVar1 = *pcVar3;
      pcVar3 = pcVar3 + 1;
      bVar2 = bVar1 + 0x20;
      if (0x19 < (byte)(bVar1 + 0xbf)) {
        bVar2 = bVar1;
      }
      uVar6 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar2 ^ (ulonglong)(uVar6 & 0xff)) * 4) ^
              uVar6 >> 8;
    } while (*pcVar3 != 0);
    pcVar3 = "groundEffect";
    param_1[3] = ~uVar6;
    uVar7 = 0xffffffff;
    do {
      bVar1 = *pcVar3;
      pcVar3 = pcVar3 + 1;
      bVar2 = bVar1 + 0x20;
      if (0x19 < (byte)(bVar1 + 0xbf)) {
        bVar2 = bVar1;
      }
      uVar6 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar2 ^ uVar7 & 0xff) * 4) ^ (uint)(uVar7 >> 8)
      ;
      uVar7 = (ulonglong)uVar6;
    } while (*pcVar3 != 0);
    pcVar3 = "swimmingStroke";
    param_1[4] = ~uVar6;
    uVar6 = 0xffffffff;
    do {
      bVar1 = *pcVar3;
      pcVar3 = pcVar3 + 1;
      bVar2 = bVar1 + 0x20;
      if (0x19 < (byte)(bVar1 + 0xbf)) {
        bVar2 = bVar1;
      }
      uVar6 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar2 ^ (ulonglong)(uVar6 & 0xff)) * 4) ^
              uVar6 >> 8;
    } while (*pcVar3 != 0);
    pcVar3 = "footstep_impulse";
    param_1[5] = ~uVar6;
    uVar7 = 0xffffffff;
    do {
      bVar1 = *pcVar3;
      pcVar3 = pcVar3 + 1;
      bVar2 = bVar1 + 0x20;
      if (0x19 < (byte)(bVar1 + 0xbf)) {
        bVar2 = bVar1;
      }
      uVar6 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar2 ^ uVar7 & 0xff) * 4) ^ (uint)(uVar7 >> 8)
      ;
      uVar7 = (ulonglong)uVar6;
    } while (*pcVar3 != 0);
    pcVar3 = "ForceFeedback";
    param_1[6] = ~uVar6;
    uVar6 = 0xffffffff;
    do {
      bVar1 = *pcVar3;
      pcVar3 = pcVar3 + 1;
      bVar2 = bVar1 + 0x20;
      if (0x19 < (byte)(bVar1 + 0xbf)) {
        bVar2 = bVar1;
      }
      uVar6 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar2 ^ (ulonglong)(uVar6 & 0xff)) * 4) ^
              uVar6 >> 8;
    } while (*pcVar3 != 0);
    pcVar3 = "FlagGrab";
    param_1[7] = ~uVar6;
    uVar7 = 0xffffffff;
    do {
      bVar1 = *pcVar3;
      pcVar3 = pcVar3 + 1;
      bVar2 = bVar1 + 0x20;
      if (0x19 < (byte)(bVar1 + 0xbf)) {
        bVar2 = bVar1;
      }
      uVar6 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar2 ^ uVar7 & 0xff) * 4) ^ (uint)(uVar7 >> 8)
      ;
      uVar7 = (ulonglong)uVar6;
    } while (*pcVar3 != 0);
    pbVar4 = &DAT_181e72f5c;
    param_1[8] = ~uVar6;
    uVar6 = 0xffffffff;
    do {
      bVar1 = *pbVar4;
      pbVar4 = pbVar4 + 1;
      bVar2 = bVar1 + 0x20;
      if (0x19 < (byte)(bVar1 + 0xbf)) {
        bVar2 = bVar1;
      }
      uVar6 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar2 ^ (ulonglong)(uVar6 & 0xff)) * 4) ^
              uVar6 >> 8;
    } while (*pbVar4 != 0);
    pcVar3 = "leftHand";
    param_1[9] = ~uVar6;
    uVar7 = 0xffffffff;
    do {
      bVar1 = *pcVar3;
      pcVar3 = pcVar3 + 1;
      bVar2 = bVar1 + 0x20;
      if (0x19 < (byte)(bVar1 + 0xbf)) {
        bVar2 = bVar1;
      }
      uVar6 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar2 ^ uVar7 & 0xff) * 4) ^ (uint)(uVar7 >> 8)
      ;
      uVar7 = (ulonglong)uVar6;
    } while (*pcVar3 != 0);
    pcVar3 = "rightHand";
    param_1[10] = ~uVar6;
    uVar6 = 0xffffffff;
    do {
      bVar1 = *pcVar3;
      pcVar3 = pcVar3 + 1;
      bVar2 = bVar1 + 0x20;
      if (0x19 < (byte)(bVar1 + 0xbf)) {
        bVar2 = bVar1;
      }
      uVar6 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar2 ^ (ulonglong)(uVar6 & 0xff)) * 4) ^
              uVar6 >> 8;
    } while (*pcVar3 != 0);
    pcVar3 = "DeathReactionEnd";
    param_1[0xb] = ~uVar6;
    uVar7 = 0xffffffff;
    do {
      bVar1 = *pcVar3;
      pcVar3 = pcVar3 + 1;
      bVar2 = bVar1 + 0x20;
      if (0x19 < (byte)(bVar1 + 0xbf)) {
        bVar2 = bVar1;
      }
      uVar6 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar2 ^ uVar7 & 0xff) * 4) ^ (uint)(uVar7 >> 8)
      ;
      uVar7 = (ulonglong)uVar6;
    } while (*pcVar3 != 0);
    pcVar3 = "ReactionOnCollision";
    param_1[0xc] = ~uVar6;
    uVar6 = 0xffffffff;
    do {
      bVar1 = *pcVar3;
      pcVar3 = pcVar3 + 1;
      bVar2 = bVar1 + 0x20;
      if (0x19 < (byte)(bVar1 + 0xbf)) {
        bVar2 = bVar1;
      }
      uVar6 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar2 ^ (ulonglong)(uVar6 & 0xff)) * 4) ^
              uVar6 >> 8;
    } while (*pcVar3 != 0);
    pcVar3 = "ForbidReactions";
    param_1[0xd] = ~uVar6;
    uVar7 = 0xffffffff;
    do {
      bVar1 = *pcVar3;
      pcVar3 = pcVar3 + 1;
      bVar2 = bVar1 + 0x20;
      if (0x19 < (byte)(bVar1 + 0xbf)) {
        bVar2 = bVar1;
      }
      uVar6 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar2 ^ uVar7 & 0xff) * 4) ^ (uint)(uVar7 >> 8)
      ;
      uVar7 = (ulonglong)uVar6;
    } while (*pcVar3 != 0);
    pcVar3 = "RagdollStart";
    param_1[0xe] = ~uVar6;
    uVar6 = 0xffffffff;
    do {
      bVar1 = *pcVar3;
      pcVar3 = pcVar3 + 1;
      bVar2 = bVar1 + 0x20;
      if (0x19 < (byte)(bVar1 + 0xbf)) {
        bVar2 = bVar1;
      }
      uVar6 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar2 ^ (ulonglong)(uVar6 & 0xff)) * 4) ^
              uVar6 >> 8;
    } while (*pcVar3 != 0);
    pcVar3 = "DeathBlow";
    param_1[0xf] = ~uVar6;
    uVar7 = 0xffffffff;
    do {
      bVar1 = *pcVar3;
      pcVar3 = pcVar3 + 1;
      bVar2 = bVar1 + 0x20;
      if (0x19 < (byte)(bVar1 + 0xbf)) {
        bVar2 = bVar1;
      }
      uVar6 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar2 ^ uVar7 & 0xff) * 4) ^ (uint)(uVar7 >> 8)
      ;
      uVar7 = (ulonglong)uVar6;
    } while (*pcVar3 != 0);
    pbVar4 = &DAT_181cfd080;
    param_1[0x11] = ~uVar6;
    uVar6 = 0xffffffff;
    do {
      bVar1 = *pbVar4;
      pbVar4 = pbVar4 + 1;
      bVar2 = bVar1 + 0x20;
      if (0x19 < (byte)(bVar1 + 0xbf)) {
        bVar2 = bVar1;
      }
      uVar6 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar2 ^ (ulonglong)(uVar6 & 0xff)) * 4) ^
              uVar6 >> 8;
    } while (*pbVar4 != 0);
    pcVar3 = "StartFire";
    param_1[0x10] = ~uVar6;
    uVar7 = 0xffffffff;
    do {
      bVar1 = *pcVar3;
      pcVar3 = pcVar3 + 1;
      bVar2 = bVar1 + 0x20;
      if (0x19 < (byte)(bVar1 + 0xbf)) {
        bVar2 = bVar1;
      }
      uVar6 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar2 ^ uVar7 & 0xff) * 4) ^ (uint)(uVar7 >> 8)
      ;
      uVar7 = (ulonglong)uVar6;
    } while (*pcVar3 != 0);
    pcVar3 = "StopFire";
    param_1[0x12] = ~uVar6;
    uVar6 = 0xffffffff;
    do {
      bVar1 = *pcVar3;
      pcVar3 = pcVar3 + 1;
      bVar2 = bVar1 + 0x20;
      if (0x19 < (byte)(bVar1 + 0xbf)) {
        bVar2 = bVar1;
      }
      uVar6 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar2 ^ (ulonglong)(uVar6 & 0xff)) * 4) ^
              uVar6 >> 8;
    } while (*pcVar3 != 0);
    pcVar3 = "ShootGrenade";
    param_1[0x13] = ~uVar6;
    uVar7 = 0xffffffff;
    do {
      bVar1 = *pcVar3;
      pcVar3 = pcVar3 + 1;
      bVar2 = bVar1 + 0x20;
      if (0x19 < (byte)(bVar1 + 0xbf)) {
        bVar2 = bVar1;
      }
      uVar6 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar2 ^ uVar7 & 0xff) * 4) ^ (uint)(uVar7 >> 8)
      ;
      uVar7 = (ulonglong)uVar6;
    } while (*pcVar3 != 0);
    pcVar3 = "MeleeHit";
    param_1[0x14] = ~uVar6;
    uVar6 = 0xffffffff;
    do {
      bVar1 = *pcVar3;
      pcVar3 = pcVar3 + 1;
      bVar2 = bVar1 + 0x20;
      if (0x19 < (byte)(bVar1 + 0xbf)) {
        bVar2 = bVar1;
      }
      uVar6 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar2 ^ (ulonglong)(uVar6 & 0xff)) * 4) ^
              uVar6 >> 8;
    } while (*pcVar3 != 0);
    pcVar3 = "MeleeStartDamagePhase";
    param_1[0x15] = ~uVar6;
    uVar7 = 0xffffffff;
    do {
      bVar1 = *pcVar3;
      pcVar3 = pcVar3 + 1;
      bVar2 = bVar1 + 0x20;
      if (0x19 < (byte)(bVar1 + 0xbf)) {
        bVar2 = bVar1;
      }
      uVar6 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar2 ^ uVar7 & 0xff) * 4) ^ (uint)(uVar7 >> 8)
      ;
      uVar7 = (ulonglong)uVar6;
    } while (*pcVar3 != 0);
    pcVar3 = "MeleeEndDamagePhase";
    param_1[0x16] = ~uVar6;
    uVar6 = 0xffffffff;
    do {
      bVar1 = *pcVar3;
      pcVar3 = pcVar3 + 1;
      bVar2 = bVar1 + 0x20;
      if (0x19 < (byte)(bVar1 + 0xbf)) {
        bVar2 = bVar1;
      }
      uVar6 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar2 ^ (ulonglong)(uVar6 & 0xff)) * 4) ^
              uVar6 >> 8;
    } while (*pcVar3 != 0);
    pcVar3 = "DetachEnvironmentalWeapon";
    param_1[0x17] = ~uVar6;
    uVar7 = 0xffffffff;
    do {
      bVar1 = *pcVar3;
      pcVar3 = pcVar3 + 1;
      bVar2 = bVar1 + 0x20;
      if (0x19 < (byte)(bVar1 + 0xbf)) {
        bVar2 = bVar1;
      }
      uVar6 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar2 ^ uVar7 & 0xff) * 4) ^ (uint)(uVar7 >> 8)
      ;
      uVar7 = (ulonglong)uVar6;
    } while (*pcVar3 != 0);
    pcVar3 = "StealthMeleeDeath";
    param_1[0x19] = ~uVar6;
    uVar6 = 0xffffffff;
    do {
      bVar1 = *pcVar3;
      pcVar3 = pcVar3 + 1;
      bVar2 = bVar1 + 0x20;
      if (0x19 < (byte)(bVar1 + 0xbf)) {
        bVar2 = bVar1;
      }
      uVar6 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar2 ^ (ulonglong)(uVar6 & 0xff)) * 4) ^
              uVar6 >> 8;
    } while (*pcVar3 != 0);
    param_1[0x1a] = ~uVar6;
    pcVar3 = "EndReboundAnim";
    do {
      bVar1 = *pcVar3;
      pcVar3 = pcVar3 + 1;
      bVar2 = bVar1 + 0x20;
      if (0x19 < (byte)(bVar1 + 0xbf)) {
        bVar2 = bVar1;
      }
      uVar5 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar2 ^ (ulonglong)(uVar5 & 0xff)) * 4) ^
              uVar5 >> 8;
    } while (*pcVar3 != 0);
    param_1[0x18] = ~uVar5;
    *(undefined1 *)(param_1 + 0x1b) = 1;
    return;
  }
  *(undefined1 *)(param_1 + 0x1b) = 1;
  return;
}


