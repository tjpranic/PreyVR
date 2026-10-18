//==== 16B1FD0  FUN_1816b1fd0 entry=0x16B1FD0

ulonglong FUN_1816b1fd0(longlong param_1,undefined8 param_2,undefined8 param_3,int param_4)

{
  int iVar1;
  undefined4 uVar2;
  ulonglong uVar3;
  undefined8 uVar4;
  ulonglong uVar5;
  float *pfVar6;
  longlong lVar7;
  undefined4 uVar8;
  undefined8 local_res8;
  longlong local_18;
  ulonglong local_10;
  
  *(bool *)(param_1 + 0x530) = param_4 == 4;
  FUN_181692e40();
  if (((*(char *)(param_1 + 0x530) == '\0') &&
      (FUN_1816b0700(param_1), *(char *)(param_1 + 0x530) == '\0')) && (param_4 != 1)) {
    uVar4 = 0;
  }
  else {
    uVar4 = 1;
  }
  (**(code **)(**(longlong **)(DAT_182c16838 + 0xd10) + 0x38))
            (*(longlong **)(DAT_182c16838 + 0xd10),uVar4);
  uVar3 = FUN_18157c990();
  if (param_4 == 1) {
    uVar4 = FUN_181587c20(uVar3 + 0x678);
    uVar5 = FUN_1812618e0(uVar4);
    if ((char)uVar5 != '\0') {
      iVar1 = *(int *)(param_1 + 0x520);
      uVar5 = (ulonglong)iVar1;
      local_18 = param_1;
      local_10 = uVar3;
      switch(iVar1) {
      case 0:
        pfVar6 = (float *)FUN_18159cb00(uVar3 + 0x58,&local_res8);
        uVar3 = FUN_1816b2e20(param_1,(0.0 < *pfVar6 || *pfVar6 == 0.0) + '\x01',0,0);
        return uVar3 & 0xffffffffffffff00;
      case 3:
      case 4:
        if (*(char *)(param_1 + 0x506) != '\0') {
          *(undefined1 *)(param_1 + 0x504) = 1;
          return (ulonglong)(uint7)(int7)(int3)((uint)iVar1 >> 8) << 8;
        }
        break;
      case 5:
      case 6:
        uVar3 = FUN_1816b0290(&local_18,4);
        return uVar3 & 0xffffffffffffff00;
      case 7:
      case 8:
        uVar3 = FUN_1816b0290(&local_18,3);
        return uVar3 & 0xffffffffffffff00;
      case 9:
        local_res8 = 0;
        lVar7 = FUN_18157c990();
        if (*(char *)(param_1 + 0x530) != '\0') {
          uVar4 = FUN_181587c20(lVar7 + 0x678);
          FUN_181261890(uVar4);
        }
        uVar2 = *(undefined4 *)(*(longlong *)(param_1 + 0x470) + 0xa4);
        uVar8 = FUN_181695030(param_1,&DAT_182bd7198);
        FUN_1816b0090(uVar2,param_1,uVar8);
        uVar3 = FUN_181284dc0(param_1 + 0x4c0);
        *(undefined4 *)(param_1 + 0x520) = 2;
        *(undefined1 *)(param_1 + 0x505) = 1;
        return uVar3 & 0xffffffffffffff00;
      case 10:
        local_res8 = 0;
        lVar7 = FUN_18157c990();
        if (*(char *)(param_1 + 0x530) != '\0') {
          uVar4 = FUN_181587c20(lVar7 + 0x678);
          FUN_181261890(uVar4);
        }
        uVar2 = *(undefined4 *)(*(longlong *)(param_1 + 0x470) + 0xa0);
        uVar8 = FUN_181695030(param_1,&DAT_182bd7198);
        FUN_1816b0090(uVar2,param_1,uVar8);
        uVar3 = FUN_181284dc0(param_1 + 0x4c0);
        *(undefined4 *)(param_1 + 0x520) = 1;
        *(undefined1 *)(param_1 + 0x505) = 1;
        return uVar3 & 0xffffffffffffff00;
      }
    }
  }
  else {
    uVar5 = uVar3;
    if ((param_4 == 2) && (*(char *)(param_1 + 0x531) != '\0')) {
      uVar5 = FUN_1816b0810(param_1);
    }
  }
  return uVar5 & 0xffffffffffffff00;
}


//==== 16B2BC0  FUN_1816b2bc0 entry=0x16B2BC0

void FUN_1816b2bc0(longlong param_1,undefined8 param_2)

{
  float fVar1;
  undefined4 uVar2;
  int iVar3;
  longlong lVar4;
  undefined8 uVar5;
  float fVar6;
  undefined4 uVar7;
  
  fVar1 = *(float *)(param_1 + 0x528);
  fVar6 = *(float *)(param_1 + 0x524);
  if (fVar1 <= *(float *)(param_1 + 0x524)) {
    fVar6 = fVar1;
  }
  iVar3 = FUN_1813bf730(param_1 + 0x4c8,param_2,param_1,
                        (DAT_181c7eb28 - (fVar1 - fVar6) / fVar1) * *(float *)(param_1 + 0x52c) +
                        DAT_181c7eb28,*(undefined1 *)(param_1 + 0x531));
  if (iVar3 == 1) {
    if (*(int *)(param_1 + 0x520) == 3) {
      lVar4 = FUN_18157c990();
      if (*(char *)(param_1 + 0x530) != '\0') {
        uVar5 = FUN_181587c20(lVar4 + 0x678);
        FUN_181261890(uVar5);
      }
      uVar2 = *(undefined4 *)(*(longlong *)(param_1 + 0x470) + 0x90);
      FUN_1816b0700(param_1);
      uVar7 = FUN_181695030(param_1,&DAT_182bd7198);
      FUN_1816b0090(uVar2,param_1,uVar7);
      *(undefined4 *)(param_1 + 0x520) = 5;
    }
    else {
      if (*(int *)(param_1 + 0x520) != 4) goto LAB_1816b2dfc;
      lVar4 = FUN_18157c990();
      if (*(char *)(param_1 + 0x530) != '\0') {
        uVar5 = FUN_181587c20(lVar4 + 0x678);
        FUN_181261890(uVar5);
      }
      uVar2 = *(undefined4 *)(*(longlong *)(param_1 + 0x470) + 0x94);
      FUN_1816b0700(param_1);
      uVar7 = FUN_181695030(param_1,&DAT_182bd7198);
      FUN_1816b0090(uVar2,param_1,uVar7);
      *(undefined4 *)(param_1 + 0x520) = 7;
    }
  }
  else {
    if (iVar3 != 2) goto LAB_1816b2dfc;
    if (*(int *)(param_1 + 0x520) == 3) {
      lVar4 = FUN_18157c990();
      if (*(char *)(param_1 + 0x530) != '\0') {
        uVar5 = FUN_181587c20(lVar4 + 0x678);
        FUN_181261890(uVar5);
      }
      uVar2 = *(undefined4 *)(*(longlong *)(param_1 + 0x470) + 0x9c);
      FUN_1816b0700(param_1);
      uVar7 = FUN_181695030(param_1,&DAT_182bd7198);
      FUN_1816b0090(uVar2,param_1,uVar7);
      *(undefined4 *)(param_1 + 0x520) = 6;
    }
    else {
      if (*(int *)(param_1 + 0x520) != 4) goto LAB_1816b2dfc;
      lVar4 = FUN_18157c990();
      if (*(char *)(param_1 + 0x530) != '\0') {
        uVar5 = FUN_181587c20(lVar4 + 0x678);
        FUN_181261890(uVar5);
      }
      uVar2 = *(undefined4 *)(*(longlong *)(param_1 + 0x470) + 0x98);
      FUN_1816b0700(param_1);
      uVar7 = FUN_181695030(param_1,&DAT_182bd7198);
      FUN_1816b0090(uVar2,param_1,uVar7);
      *(undefined4 *)(param_1 + 0x520) = 8;
    }
  }
  *(undefined1 *)(param_1 + 0x505) = 1;
LAB_1816b2dfc:
  *(undefined1 *)(param_1 + 0x531) = 0;
  *(undefined4 *)(param_1 + 0x524) = 0;
  return;
}


//==== 13BF730  FUN_1813bf730 entry=0x13BF730

int FUN_1813bf730(undefined8 *param_1,float param_2,longlong param_3,float param_4,char param_5)

{
  float *pfVar1;
  undefined4 uVar2;
  undefined4 uVar3;
  ulonglong uVar4;
  uint uVar5;
  longlong *plVar6;
  char cVar7;
  char cVar8;
  undefined2 uVar9;
  undefined4 uVar10;
  undefined4 uVar11;
  int iVar12;
  longlong *plVar13;
  undefined8 uVar14;
  longlong lVar15;
  float *pfVar16;
  longlong *plVar17;
  longlong lVar18;
  float *pfVar19;
  int iVar20;
  float fVar21;
  undefined4 uVar22;
  undefined4 uVar23;
  float fVar24;
  float fVar25;
  float fVar26;
  float fVar27;
  float fVar28;
  undefined8 in_stack_fffffffffffffcc0;
  undefined4 uVar29;
  undefined4 in_stack_fffffffffffffcd8;
  undefined2 uVar30;
  undefined8 local_318;
  float local_2f0;
  float local_2ec;
  float local_2e8;
  float local_2e0;
  float local_2dc;
  float local_2d8;
  undefined4 local_2c8;
  undefined4 local_2c4;
  undefined4 local_2c0;
  undefined8 local_2bc;
  undefined4 local_2b4;
  undefined4 local_2b0;
  undefined8 local_2ac;
  float local_2a4;
  int local_2a0;
  undefined4 local_29c;
  undefined4 local_298;
  undefined4 local_294;
  float local_290;
  float local_28c;
  float local_288;
  float local_284;
  undefined4 local_280;
  undefined4 local_27c;
  undefined4 local_278;
  float local_274;
  float local_270;
  float local_26c;
  undefined4 local_268;
  undefined4 local_264;
  undefined8 local_260;
  uint local_258;
  uint local_254;
  uint local_250;
  undefined8 local_248;
  undefined4 local_240;
  longlong local_238;
  undefined8 local_230;
  undefined4 local_228;
  undefined4 uStack_224;
  undefined4 uStack_220;
  float fStack_21c;
  float local_218;
  int local_214;
  float local_210;
  float local_208;
  float local_204;
  float local_200;
  float local_1fc;
  float local_1f8;
  float local_1f4;
  float local_1f0;
  float local_1ec;
  float local_1e8;
  undefined4 local_1e4;
  undefined4 local_1e0;
  undefined4 local_1dc;
  undefined8 local_1d8;
  undefined8 local_1d0;
  undefined8 local_1c8;
  longlong *local_1c0;
  longlong *local_1b8;
  float *local_1b0;
  float *local_1a8;
  longlong local_1a0;
  undefined1 local_198 [8];
  undefined1 local_190 [24];
  undefined1 local_178 [8];
  undefined4 local_170;
  undefined2 local_f8;
  undefined2 local_f6;
  undefined8 local_e8;
  undefined1 local_e0;
  undefined4 local_dc;
  
  plVar6 = DAT_18224da38;
  if (*(char *)((longlong)param_1 + 0x3f) == '\0') {
    iVar20 = 0;
    plVar13 = (longlong *)(**(code **)(*DAT_18224d988 + 0x460))();
    local_1b8 = (longlong *)(**(code **)(*plVar13 + 0xa0))(plVar13);
    uVar14 = FUN_18119a3a0();
    local_1d8 = FUN_18119a440(uVar14);
    lVar15 = FUN_18157c990();
    plVar13 = *(longlong **)(lVar15 + 0x38);
    local_1c8 = (**(code **)(*plVar13 + 0x228))(plVar13);
    fVar25 = param_2 * DAT_181c8b724;
    fVar21 = cosf(fVar25);
    fVar25 = sinf(fVar25);
    pfVar16 = (float *)(**(code **)(*plVar13 + 0x150))(plVar13,&local_258);
    fVar27 = pfVar16[3] * fVar21 - pfVar16[2] * fVar25;
    fVar26 = pfVar16[1] * fVar21 - *pfVar16 * fVar25;
    fVar24 = *pfVar16 * fVar21 + pfVar16[1] * fVar25;
    fVar21 = pfVar16[2] * fVar21 + pfVar16[3] * fVar25;
    fVar28 = fVar26 * fVar24 - fVar27 * fVar21;
    fVar28 = fVar28 + fVar28;
    fVar25 = (fVar26 * fVar26 + fVar26 * fVar26 + fVar27 * fVar27 + fVar27 * fVar27) - DAT_181c7eb28
    ;
    fVar27 = fVar27 * fVar24;
    fVar26 = fVar26 * fVar21;
    fVar21 = fVar27 + fVar27 + fVar26 + fVar26;
    (*(code *)**(undefined8 **)(lVar15 + 0x40))(lVar15 + 0x40,&local_1f0);
    uVar22 = FUN_181695020(param_3,&DAT_182bd7180);
    cVar7 = FUN_181083490(param_3 + 0x1a0);
    plVar13 = *(longlong **)(param_3 + 0x40);
    local_1c0 = plVar13;
    uVar10 = (**(code **)(*plVar13 + 8))(plVar13);
    plVar17 = (longlong *)(**(code **)(*plVar13 + 0x260))(plVar13);
    plVar17 = (longlong *)(**(code **)(*plVar17 + 0xe0))(plVar17,0);
    uVar9 = (**(code **)(*plVar17 + 0x58))(plVar17);
    uVar23 = FUN_181695020(param_3,&DAT_182bd7188);
    uVar3 = *(undefined4 *)(param_1 + 4);
    uVar2 = *(undefined4 *)(param_1 + 5);
    (**(code **)(*plVar13 + 0x100))(plVar13,&local_208);
    local_1fc = local_1fc - local_208;
    if (0.0 <= local_1fc) {
      fVar24 = SQRT((local_1f8 - local_204) * (local_1f8 - local_204) + local_1fc * local_1fc +
                    (local_1f4 - local_200) * (local_1f4 - local_200)) * DAT_181c7eb24;
    }
    else {
      fVar24 = 0.0;
    }
    local_318 = *param_1;
    if (param_5 == '\0') {
      if (cVar7 != '\0') {
        local_318 = param_1[1];
      }
    }
    else if (cVar7 == '\0') {
      local_318 = param_1[2];
    }
    else {
      local_318 = param_1[3];
    }
    FUN_1813bd620(param_1,&local_1b0,param_3,param_2);
    pfVar19 = local_1a8;
    pfVar16 = local_1b0;
    if (local_1b0 != local_1a8) {
      local_1d0 = *(undefined8 *)(DAT_182c16840 + 0x2f8);
      FUN_18141d5f0(local_1d0,uVar10,&DAT_00007777,0);
      uVar5 = DAT_181c7eb70;
      pfVar16 = pfVar16 + 0xb;
      do {
        lVar15 = *(longlong *)(pfVar16 + -9);
        if (lVar15 != 0) {
          lVar18 = FUN_1816af300(lVar15);
          uVar30 = (undefined2)((uint)in_stack_fffffffffffffcd8 >> 0x10);
          uVar29 = (undefined4)((ulonglong)in_stack_fffffffffffffcc0 >> 0x20);
          if (lVar18 == 0) {
            plVar13 = (longlong *)(**(code **)(*plVar6 + 0xe0))(plVar6,lVar15);
            uVar14 = CONCAT44(uVar29,uVar23);
            local_2f0 = fVar28;
            local_2ec = fVar25;
            local_2e8 = fVar21;
            FUN_1816ae410(plVar13,lVar15,&local_2f0,pfVar16[-6],param_3,uVar14,0,uVar2);
            local_2dc = pfVar16[-1] - local_1ec;
            local_228 = 0;
            uStack_224 = 0;
            uStack_220 = 0;
            fStack_21c = 0.0;
            local_2e0 = pfVar16[-2] - local_1f0;
            local_248 = 0;
            local_238 = 0;
            local_230 = 0;
            local_2d8 = *pfVar16 - local_1e8;
            uVar14 = CONCAT44((int)((ulonglong)uVar14 >> 0x20),(int)*(short *)(pfVar16 + -5));
            local_240 = 0xffffffff;
            local_218 = -NAN;
            local_214 = -1;
            local_210 = -NAN;
            FUN_1816aeaf0(*(undefined8 *)(pfVar16 + -9),pfVar16[-7],pfVar16 + -2,&local_2e0,
                          &local_248,uVar14);
            uVar29 = (undefined4)((ulonglong)uVar14 >> 0x20);
            lVar18 = FUN_1812dc290(plVar13);
            if (lVar18 == 0) {
              FUN_18167ccb0(plVar13);
            }
            if (plVar13 != (longlong *)0x0) {
              uVar11 = (**(code **)(*plVar13 + 8))();
              local_28c = pfVar16[-2];
              local_288 = pfVar16[-1];
              local_284 = *pfVar16;
              local_2a0 = (int)*(short *)((longlong)pfVar16 + -0x12);
              local_27c = local_1e0;
              local_274 = pfVar16[1];
              local_280 = local_1e4;
              local_26c = pfVar16[3];
              local_278 = local_1dc;
              local_270 = pfVar16[2];
              local_2bc = 0;
              local_2b4 = 0;
              local_2ac = 0;
              local_290 = pfVar16[-6];
              local_294 = 0;
              local_264 = 0;
              local_260 = 0;
              local_2c8 = 0x7777;
              local_298 = 0xffffffff;
              local_268 = 0xffffffff;
              local_2c4 = uVar11;
              local_2c0 = uVar10;
              local_2b0 = uVar22;
              local_2a4 = param_2;
              local_29c = uVar3;
              pfVar19 = (float *)FUN_1816af0c0(local_198,uVar11,param_1 + 8);
              fVar26 = *pfVar19;
              local_260._0_4_ = CONCAT13(*(undefined1 *)(pfVar19 + 1),(undefined3)local_260);
              FUN_1816af980(cVar7,uVar11);
              uVar14 = FUN_1813a2b60(local_190,&local_2c8);
              uVar30 = 0;
              FUN_181608860(local_1d8,uVar11,&DAT_00007777,&DAT_00007777,local_318,uVar14,
                            fVar26 * param_4,0,0,0);
              uVar29 = (undefined4)((ulonglong)uVar14 >> 0x20);
              FUN_18141e020(local_1d0,uVar11,uVar10);
              pfVar19 = local_1a8;
            }
            cVar8 = FUN_18147d2f0();
            local_170 = 2;
            local_e8 = 0;
            local_e0 = 0;
            local_dc = 0x447a0000;
            local_f6 = 0xffff;
            iVar20 = (cVar8 != '\0') + 1;
            local_f8 = 0xffff;
            fStack_21c = pfVar16[3];
            local_218 = pfVar16[-6];
            local_210 = pfVar16[5];
            uStack_224 = (undefined4)*(undefined8 *)(pfVar16 + 1);
            uStack_220 = (undefined4)((ulonglong)*(undefined8 *)(pfVar16 + 1) >> 0x20);
            if (local_214 < 1) {
              local_214 = (int)*(short *)(pfVar16 + -5);
            }
            in_stack_fffffffffffffcd8 = CONCAT22(uVar30,uVar9);
            local_258 = (uint)fVar28 ^ uVar5;
            local_250 = (uint)fVar21 ^ uVar5;
            in_stack_fffffffffffffcc0 = CONCAT44(uVar29,uVar22);
            local_254 = (uint)fVar25 ^ uVar5;
            local_238 = lVar15;
            FUN_1816aef20(local_178,&local_248,&local_258,local_1c0,local_1c8,
                          in_stack_fffffffffffffcc0,fVar24,1,in_stack_fffffffffffffcd8);
            plVar13 = (longlong *)(**(code **)(*local_1b8 + 0x18))(local_1b8,local_214,0);
            if ((plVar13 != (longlong *)0x0) &&
               (iVar12 = (**(code **)(*plVar13 + 0x38))(plVar13), 0 < iVar12)) {
              iVar20 = 0;
            }
          }
        }
        pfVar1 = pfVar16 + 9;
        pfVar16 = pfVar16 + 0x14;
      } while (pfVar1 != pfVar19);
      if (iVar20 - 1U < 2) {
        lVar15 = FUN_18157c990();
        uVar14 = FUN_181587c20(lVar15 + 0x678);
        FUN_181260d10(uVar14,param_4 * *(float *)(param_1 + 7));
        FUN_181261320(uVar14,param_4 * *(float *)(param_1 + 7));
      }
    }
    if (local_1b0 != (float *)0x0) {
      uVar4 = (local_1a0 - (longlong)local_1b0) / 0x50;
      if ((0x333333333333333 < uVar4) ||
         ((pfVar16 = local_1b0, 0xfff < uVar4 * 0x50 &&
          (((((ulonglong)local_1b0 & 0x1f) != 0 ||
            (pfVar16 = *(float **)(local_1b0 + -2), local_1b0 <= pfVar16)) ||
           (0x1f < (ulonglong)((longlong)local_1b0 + (-8 - (longlong)pfVar16)))))))) {
                    /* WARNING: Subroutine does not return */
        _invalid_parameter_noinfo_noreturn();
      }
      free(pfVar16);
    }
  }
  else {
    *(undefined1 *)((longlong)param_1 + 0x3f) = 0;
    iVar20 = 0;
  }
  return iVar20;
}


//==== 16AE410  FUN_1816ae410 entry=0x16AE410

void FUN_1816ae410(longlong param_1,undefined8 param_2,uint *param_3,undefined4 param_4,
                  longlong param_5,float param_6,undefined4 param_7,undefined4 param_8)

{
  float fVar1;
  uint local_18;
  uint local_14;
  uint local_10;
  
  if ((param_5 != 0) && (param_1 != 0)) {
    fVar1 = (float)FUN_181694210(param_5,param_1);
    if (0.0 < fVar1 * param_6) {
      local_18 = *param_3 ^ DAT_181c7eb70;
      local_10 = param_3[2] ^ DAT_181c7eb70;
      local_14 = param_3[1] ^ DAT_181c7eb70;
      FUN_1816adff0(param_1,param_2,&local_18,param_4,fVar1 * param_6,param_7,param_8);
    }
  }
  return;
}


//==== 16AF300  FUN_1816af300 entry=0x16AF300

undefined8 FUN_1816af300(longlong param_1)

{
  longlong lVar1;
  undefined4 uVar2;
  longlong *plVar3;
  longlong *plVar4;
  undefined8 uVar5;
  
  if (param_1 != 0) {
    plVar3 = (longlong *)(**(code **)(*DAT_18224da38 + 0xe0))(DAT_18224da38,param_1);
    if (plVar3 != (longlong *)0x0) {
      plVar4 = (longlong *)(**(code **)(*DAT_182c16840 + 0x70))();
      lVar1 = *plVar4;
      uVar2 = (**(code **)(*plVar3 + 8))(plVar3);
      plVar3 = (longlong *)(**(code **)(lVar1 + 0x278))(plVar4,uVar2);
      if (plVar3 != (longlong *)0x0) {
        if (*(int *)(*(longlong *)((longlong)ThreadLocalStoragePointer + (ulonglong)_tls_index * 8)
                    + 0x10) < DAT_182c15868) {
          _Init_thread_header(&DAT_182c15868);
          if (DAT_182c15868 == -1) {
            DAT_182c15864 = (**(code **)(*plVar3 + 0x48))(plVar3,"ArkProjectileGooBall");
            _Init_thread_footer(&DAT_182c15868);
          }
        }
                    /* WARNING: Could not recover jumptable at 0x0001816af3a1. Too many branches */
                    /* WARNING: Treating indirect jump as call */
        uVar5 = (**(code **)(*plVar3 + 0x50))(plVar3,DAT_182c15864);
        return uVar5;
      }
    }
  }
  return 0;
}


//==== 16AEAF0  FUN_1816aeaf0 entry=0x16AEAF0

char FUN_1816aeaf0(longlong *param_1,int param_2,undefined8 *param_3,float *param_4,
                  undefined8 *param_5,undefined4 param_6)

{
  float fVar1;
  float fVar2;
  float fVar3;
  int iVar4;
  undefined4 uVar5;
  uint uVar6;
  longlong *plVar7;
  longlong lVar8;
  longlong *plVar9;
  undefined8 uVar10;
  char cVar11;
  float fVar12;
  undefined1 auVar13 [16];
  float fVar14;
  longlong *local_res8;
  char local_res10 [8];
  undefined8 *local_138;
  char *local_130;
  longlong **local_128;
  float *local_120;
  undefined8 *local_118;
  uint local_108;
  undefined1 local_f8 [8];
  int local_f0;
  undefined1 *local_c8;
  longlong *local_98;
  undefined1 local_58 [64];
  
  local_res10[0] = '\0';
  param_5[3] = *param_3;
  *(undefined4 *)(param_5 + 4) = *(undefined4 *)(param_3 + 1);
  fVar1 = param_4[1];
  fVar2 = *param_4;
  fVar3 = param_4[2];
  fVar14 = fVar2 * fVar2 + fVar1 * fVar1 + fVar3 * fVar3 + DAT_181c7eb20;
  auVar13 = rsqrtss(ZEXT416((uint)fVar14),ZEXT416((uint)fVar14));
  fVar12 = auVar13._0_4_;
  fVar12 = (DAT_181c7f7a0 - fVar12 * fVar14 * fVar12 * DAT_181c7eb24) * fVar12;
  local_108 = (uint)(fVar3 * fVar12) ^ DAT_181c7eb70;
  *(ulonglong *)((longlong)param_5 + 0x24) =
       CONCAT44((uint)(fVar1 * fVar12) ^ DAT_181c7eb70,(uint)(fVar2 * fVar12) ^ DAT_181c7eb70);
  *(uint *)((longlong)param_5 + 0x2c) = local_108;
  cVar11 = '\0';
  if ((-1 < param_2) && (param_1 != (longlong *)0x0)) {
    local_130 = local_res10;
    local_128 = &local_res8;
    local_138 = param_5;
    local_res8 = param_1;
    local_120 = param_4;
    local_118 = param_3;
    plVar7 = (longlong *)(**(code **)(*DAT_18224da38 + 0xe0))(DAT_18224da38,param_1);
    if (plVar7 == (longlong *)0x0) {
      iVar4 = (**(code **)(*local_res8 + 0x70))();
      if (iVar4 == 1) {
        FUN_18013a2a0(local_f8);
        local_f0 = param_2;
        local_c8 = local_58;
        iVar4 = (**(code **)(*local_res8 + 0x28))(local_res8,local_f8);
        cVar11 = local_res10[0];
        if (((iVar4 != 0) && (local_98 != (longlong *)0x0)) &&
           (local_98 = (longlong *)*local_98, local_98 != (longlong *)0x0)) {
          plVar7 = (longlong *)(**(code **)(*local_98 + 0x118))(local_98,0);
          if (plVar7 == (longlong *)0x0) {
            plVar7 = (longlong *)(**(code **)(*local_res8 + 0x68))(local_res8,1);
            cVar11 = local_res10[0];
            if (plVar7 != (longlong *)0x0) {
              uVar10 = (**(code **)(*plVar7 + 0x128))(plVar7,0);
              *param_5 = uVar10;
              *(undefined4 *)((longlong)param_5 + 0x34) = param_6;
              cVar11 = '\x01';
            }
          }
          else {
            FUN_1816ac590(&local_138,plVar7,local_f8);
            cVar11 = local_res10[0];
            if (((local_res10[0] == '\0') &&
                (lVar8 = (**(code **)(*plVar7 + 0x50))(plVar7), cVar11 = local_res10[0], lVar8 == 0)
                ) && (iVar4 = (**(code **)(*plVar7 + 0x1a8))(plVar7), cVar11 = local_res10[0],
                     iVar4 == 0)) {
              plVar9 = (longlong *)(**(code **)(*local_res8 + 0x68))(local_res8,1);
              if (plVar9 == (longlong *)0x0) {
                plVar7 = (longlong *)(**(code **)(*plVar7 + 0x48))(plVar7);
              }
              else {
                plVar7 = (longlong *)(**(code **)(*plVar9 + 0x128))(plVar9,0);
              }
              cVar11 = local_res10[0];
              if ((plVar7 != (longlong *)0x0) &&
                 (uVar6 = (**(code **)(*plVar7 + 0x48))(plVar7), cVar11 = local_res10[0],
                 (uVar6 >> 10 & 1) == 0)) {
                *(undefined4 *)((longlong)param_5 + 0x34) = param_6;
                *param_5 = plVar7;
                cVar11 = '\x01';
              }
            }
          }
        }
      }
      else {
        iVar4 = (**(code **)(*local_res8 + 0x70))();
        cVar11 = local_res10[0];
        if (iVar4 == 0) {
          FUN_18013a2a0(local_f8);
          local_f0 = param_2;
          local_c8 = local_58;
          iVar4 = (**(code **)(*local_res8 + 0x28))(local_res8,local_f8);
          cVar11 = local_res10[0];
          if (((iVar4 != 0) && (local_98 != (longlong *)0x0)) &&
             (local_98 = (longlong *)*local_98, local_98 != (longlong *)0x0)) {
            lVar8 = (**(code **)(*local_98 + 0x118))(local_98,0);
            if (lVar8 == 0) {
              plVar7 = (longlong *)(**(code **)(*local_res8 + 0x68))(local_res8,0);
              cVar11 = local_res10[0];
              if (plVar7 != (longlong *)0x0) {
                uVar10 = (**(code **)(*plVar7 + 0x128))(plVar7,0);
                *param_5 = uVar10;
                cVar11 = '\x01';
              }
            }
            else {
              FUN_1816ac590(&local_138,lVar8,local_f8);
              cVar11 = local_res10[0];
            }
          }
        }
      }
    }
    else {
      lVar8 = (**(code **)(*plVar7 + 0x2e8))(plVar7,0x80000000);
      if (lVar8 == 0) {
        plVar9 = (longlong *)(**(code **)(*plVar7 + 0x2d8))(plVar7,0x80000000);
        if (plVar9 != (longlong *)0x0) {
          uVar10 = (**(code **)(*plVar9 + 0x108))(plVar9);
          *param_5 = uVar10;
          *(undefined4 *)((longlong)param_5 + 0x34) = param_6;
          return '\x01';
        }
      }
      else {
        FUN_18013a2a0(local_f8);
        local_f0 = param_2;
        local_c8 = local_58;
        iVar4 = (**(code **)(*local_res8 + 0x28))(local_res8,local_f8);
        if (iVar4 != 0) {
          FUN_1816ac590(&local_138,lVar8,local_f8);
        }
      }
      cVar11 = local_res10[0];
      if (local_res10[0] == '\0') {
        plVar9 = (longlong *)(**(code **)(*plVar7 + 0x200))(plVar7,0);
        uVar5 = 0xffffffff;
        if (plVar9 == (longlong *)0x0) {
          plVar7 = (longlong *)(**(code **)(*plVar7 + 0x260))(plVar7);
        }
        else {
          plVar7 = (longlong *)(**(code **)(*plVar9 + 0x98))(plVar9,0xffffffff);
        }
        *param_5 = plVar7;
        if (plVar7 != (longlong *)0x0) {
          uVar5 = (**(code **)(*plVar7 + 0x58))(plVar7);
        }
        *(undefined4 *)((longlong)param_5 + 0x34) = uVar5;
        cVar11 = '\x01';
      }
    }
  }
  return cVar11;
}


//==== 16AEF20  FUN_1816aef20 entry=0x16AEF20

void FUN_1816aef20(longlong param_1,undefined8 *param_2,float *param_3,undefined8 param_4,
                  undefined8 param_5,undefined4 param_6,undefined4 param_7,char param_8,
                  undefined2 param_9)

{
  float fVar1;
  float fVar2;
  float fVar3;
  longlong *plVar4;
  float fVar5;
  undefined8 uVar6;
  undefined4 uVar7;
  
  plVar4 = (longlong *)param_2[2];
  if (plVar4 != (longlong *)0x0) {
    *(undefined8 *)(param_1 + 0x90) = *param_2;
    uVar7 = FUN_1813e0c80(plVar4,0xffffffff);
    *(undefined4 *)(param_1 + 0x6c) = param_6;
    *(undefined4 *)(param_1 + 0x70) = uVar7;
    uVar7 = (**(code **)(*DAT_18224d9c8 + 0xa8))(DAT_18224d9c8,plVar4);
    *(undefined4 *)(param_1 + 0x38) = uVar7;
    *(undefined8 *)(param_1 + 0x3c) = param_2[3];
    *(undefined4 *)(param_1 + 0x44) = *(undefined4 *)(param_2 + 4);
    *(undefined8 *)(param_1 + 0x48) = *(undefined8 *)((longlong)param_2 + 0x24);
    fVar5 = DAT_181c7f7a8;
    *(undefined4 *)(param_1 + 0x50) = *(undefined4 *)((longlong)param_2 + 0x2c);
    fVar1 = *param_3;
    fVar2 = param_3[2];
    fVar3 = param_3[1];
    *(undefined4 *)(param_1 + 0x74) = 0xffffffff;
    *(ulonglong *)(param_1 + 0x54) = CONCAT44(fVar3 * fVar5,fVar1 * fVar5);
    *(float *)(param_1 + 0x5c) = fVar2 * fVar5;
    *(undefined8 *)(param_1 + 0x60) = 0;
    *(undefined4 *)(param_1 + 0x68) = 0;
    *(undefined4 *)(param_1 + 0x78) = *(undefined4 *)(param_2 + 6);
    *(undefined2 *)(param_1 + 0x7c) = param_9;
    *(undefined2 *)(param_1 + 0x7e) = *(undefined2 *)((longlong)param_2 + 0x34);
    *(undefined2 *)(param_1 + 0x80) = 0xffff;
    *(undefined2 *)(param_1 + 0x82) = *(undefined2 *)(param_2 + 7);
    *(undefined8 *)(param_1 + 0x10) = param_5;
    *(undefined4 *)(param_1 + 0x30) = 2;
    *(undefined8 *)(param_1 + 0x20) = param_4;
    *(longlong **)(param_1 + 0x18) = plVar4;
    uVar7 = (**(code **)(*plVar4 + 0x70))(plVar4);
    *(undefined4 *)(param_1 + 0x34) = uVar7;
    uVar6 = (**(code **)(*plVar4 + 0x68))(plVar4,uVar7);
    *(undefined8 *)(param_1 + 0x28) = uVar6;
    *(undefined4 *)(param_1 + 0x8c) = param_7;
    *(undefined8 *)(param_1 + 0x84) = 0;
    if (param_8 != '\0') {
      (**(code **)(*DAT_18224d9c8 + 0x2e8))(DAT_18224d9c8,2,param_1);
    }
  }
  return;
}


//==== 16ADFF0  FUN_1816adff0 entry=0x16ADFF0

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void FUN_1816adff0(longlong *param_1,longlong *param_2,float *param_3,undefined4 param_4,
                  float param_5,float param_6,float param_7)

{
  int iVar1;
  longlong *plVar2;
  longlong lVar3;
  longlong lVar4;
  bool bVar5;
  float fVar6;
  float local_98 [3];
  float local_8c;
  float local_88;
  undefined8 local_84;
  undefined8 uStack_7c;
  undefined4 local_74;
  undefined4 local_70;
  undefined8 local_6c;
  undefined8 uStack_64;
  undefined4 local_44;
  undefined4 uStack_40;
  undefined4 uStack_3c;
  undefined4 uStack_38;
  undefined4 local_34;
  undefined4 local_30;
  
  if ((param_2 != (longlong *)0x0) && (param_1 != (longlong *)0x0)) {
    lVar3 = 0;
    plVar2 = (longlong *)(**(code **)(*param_1 + 0x228))();
    bVar5 = false;
    if (plVar2 != (longlong *)0x0) {
      if (param_2 == plVar2) {
        lVar4 = FUN_1815397d0(param_1);
        if ((lVar4 != 0) && (*(char *)(lVar4 + 0x238b) != '\0')) {
          bVar5 = true;
          lVar3 = lVar4;
        }
      }
      else {
        iVar1 = (**(code **)(*plVar2 + 8))(plVar2);
        param_2 = plVar2;
        if (iVar1 == 4) {
          lVar3 = FUN_1815397d0(param_1);
          bVar5 = lVar3 != 0;
        }
      }
    }
    if (bVar5) {
      if (lVar3 != 0) {
        FUN_181537960(lVar3,param_3,param_5);
      }
    }
    else {
      fVar6 = (float)FUN_1813e0c80(param_2,param_4);
      if (0.0 < fVar6) {
        uStack_38 = 0xffbfffff;
        local_44 = 0xffbfffff;
        local_98[0] = param_6;
        if ((param_6 <= fVar6) && (local_98[0] = fVar6, param_7 <= fVar6)) {
          local_98[0] = param_7;
        }
        local_98[2] = *param_3 * param_5;
        local_98[1] = 2.8026e-45;
        local_8c = param_3[1] * param_5;
        local_88 = param_3[2] * param_5;
        local_84 = CONCAT44(uStack_40,0xffbfffff);
        uStack_7c = CONCAT44(0xffbfffff,uStack_3c);
        local_74 = local_34;
        local_6c = _DAT_181c93740;
        uStack_64 = _UNK_181c93748;
        local_70 = local_30;
        FUN_181231940(param_2,local_98,1,param_4,1);
      }
    }
  }
  return;
}


//==== 1694210  FUN_181694210 entry=0x1694210

ulonglong FUN_181694210(longlong param_1,longlong *param_2)

{
  bool bVar1;
  char cVar2;
  int iVar3;
  longlong *plVar4;
  ulonglong uVar5;
  longlong *local_res10;
  longlong *local_res18;
  longlong local_res20;
  int local_d8 [2];
  longlong *local_d0;
  int local_c0 [2];
  longlong *local_b8;
  longlong *local_a8 [2];
  undefined4 local_98;
  undefined4 local_94;
  undefined4 local_90;
  undefined4 local_8c;
  undefined4 local_88;
  uint local_84;
  uint local_80;
  uint local_7c;
  undefined4 local_78;
  undefined4 local_74;
  undefined4 local_70;
  undefined4 local_64;
  undefined4 local_60;
  undefined4 local_5c;
  undefined4 local_58;
  undefined4 local_54;
  undefined4 local_50;
  undefined4 local_4c;
  undefined4 local_48;
  
  if (param_2 == (longlong *)0x0) {
    return 0;
  }
  plVar4 = (longlong *)FUN_1814ace70(local_a8);
  local_res10 = (longlong *)0x0;
  plVar4 = (longlong *)*plVar4;
  if (plVar4 == (longlong *)0x0) goto LAB_1816942ed;
  local_d8[0] = 6;
  local_d0 = (longlong *)0x0;
  cVar2 = (**(code **)(*plVar4 + 0x38))(plVar4,"Properties",local_d8,0);
  plVar4 = local_d0;
  if (cVar2 == '\0') {
LAB_1816942b4:
    if (local_d8[0] != 6) goto LAB_1816942ca;
    if (local_d0 != (longlong *)0x0) {
      (**(code **)(*local_d0 + 0x18))(local_d0);
    }
  }
  else {
    if (local_d8[0] == 6) {
      if (local_d0 != (longlong *)0x0) {
        (**(code **)(*local_d0 + 0x10))(local_d0);
      }
      if (local_res10 != (longlong *)0x0) {
        (**(code **)(*local_res10 + 0x18))();
      }
      local_res10 = plVar4;
      goto LAB_1816942b4;
    }
LAB_1816942ca:
    if ((local_d8[0] == 7) && (local_d0 != (longlong *)0x0)) {
      (**(code **)(*DAT_18224d9c0 + 0xc0))();
    }
  }
  local_d0 = (longlong *)0x0;
  local_d8[0] = 0;
LAB_1816942ed:
  FUN_1814acef0(&local_res18,&local_res10);
  if (local_res10 != (longlong *)0x0) {
    (**(code **)(*local_res10 + 0x18))();
  }
  local_res20 = 0;
  if (local_res18 != (longlong *)0x0) {
    local_c0[0] = 5;
    cVar2 = (**(code **)(*local_res18 + 0x38))(local_res18,"ability_CarryRequirement",local_c0,0);
    if ((cVar2 == '\0') || (plVar4 = local_b8, local_c0[0] != 5)) {
      plVar4 = (longlong *)&DAT_181c7ce7b;
      if (local_c0[0] == 6) {
        if (local_b8 != (longlong *)0x0) {
          (**(code **)(*local_b8 + 0x18))();
        }
      }
      else if ((local_c0[0] == 7) && (local_b8 != (longlong *)0x0)) {
        (**(code **)(*DAT_18224d9c0 + 0xc0))();
      }
    }
    local_b8 = (longlong *)0x0;
    local_c0[0] = 0;
    if ((char)*plVar4 != '\0') {
      FUN_1800c69f0(plVar4,"%I64u",&local_res20);
    }
  }
  if (local_res18 != (longlong *)0x0) {
    (**(code **)(*local_res18 + 0x18))();
  }
  if (local_a8[0] != (longlong *)0x0) {
    (**(code **)(*local_a8[0] + 0x18))();
  }
  bVar1 = false;
  plVar4 = (longlong *)(**(code **)(*param_2 + 0x228))(param_2);
  if (plVar4 != (longlong *)0x0) {
    local_98 = 10;
    local_90 = 0xffbfffff;
    local_84 = 0xffbfffff;
    local_8c = 0xffbfffff;
    local_88 = 0xffbfffff;
    local_94 = 0x80000000;
    local_78 = 0x80000000;
    local_74 = 0xffbfffff;
    local_70 = 0xffbfffff;
    local_60 = 0xffbfffff;
    local_5c = 0xffbfffff;
    local_58 = 0x80000000;
    local_64 = 0xffbfffff;
    local_54 = 0x80000000;
    local_50 = 0xffbfffff;
    local_4c = 0x80000000;
    local_48 = 0x80000000;
    iVar3 = (**(code **)(*plVar4 + 0x28))(plVar4,&local_98);
    if (((iVar3 != 0) && ((float)(local_84 & DAT_181c7eb60) <= 0.0)) &&
       ((float)(local_80 & DAT_181c7eb60) <= 0.0)) {
      bVar1 = false;
      if ((float)(local_7c & DAT_181c7eb60) <= 0.0) {
        bVar1 = true;
      }
    }
  }
  if ((local_res20 == 0) || (bVar1)) {
    uVar5 = FUN_1814ae9e0(param_1 + 0x1a8,param_1 + 0x408);
    if (((float)uVar5 == 0.0) && (bVar1)) {
      uVar5 = (ulonglong)DAT_181c7eb28;
    }
  }
  else {
    uVar5 = 0;
  }
  return uVar5;
}


//==== 1608860  FUN_181608860 entry=0x1608860

void FUN_181608860(longlong param_1,undefined4 param_2,int param_3,int param_4,undefined8 param_5,
                  uint *param_6,float param_7,undefined8 param_8,undefined8 param_9,char param_10)

{
  undefined8 *puVar1;
  uint uVar2;
  undefined8 *puVar3;
  uint uVar4;
  ulonglong uVar5;
  uint *local_res8;
  undefined4 local_res10 [2];
  undefined8 local_90;
  float local_88;
  undefined1 local_80 [16];
  longlong local_70;
  undefined8 uStack_68;
  longlong local_60;
  undefined8 local_58;
  int local_50;
  uint local_48 [2];
  undefined1 local_40 [8];
  
  local_res10[0] = param_2;
  puVar3 = (undefined8 *)FUN_1816072e0(param_1 + 0x38,param_5);
  if (puVar3 == (undefined8 *)0x0) {
    FUN_1802b17f0("Signal System: %llu is not a valid package id.",param_5);
  }
  else {
    local_res8 = (uint *)FUN_1815a20e0(local_80,param_6);
    local_58 = *puVar3;
    local_70 = 0;
    uStack_68 = 0;
    local_60 = 0;
    local_50 = param_3;
    FUN_1815a20e0(local_48,local_res8);
    uVar5 = (longlong)(puVar3[2] - puVar3[1]) >> 4;
    if ((ulonglong)(local_60 - local_70 >> 4) < uVar5) {
      if (0xfffffffffffffff < uVar5) {
                    /* WARNING: Subroutine does not return */
        FUN_1802a3770();
      }
      FUN_18160c060(&local_70);
    }
    puVar1 = (undefined8 *)puVar3[2];
    for (puVar3 = (undefined8 *)puVar3[1]; puVar3 != puVar1; puVar3 = puVar3 + 2) {
      local_88 = param_7 * *(float *)(puVar3 + 1);
      local_90 = *puVar3;
      FUN_181603f10(&local_70,&local_90);
    }
    uVar4 = *local_res8;
    uVar2 = ~uVar4;
    if (-1 < (int)uVar4) {
      uVar2 = uVar4;
    }
    FUN_181331dc0(uVar4,uVar2,&local_res8,local_res8 + 2,(ulonglong)local_res8 & 0xff,
                  local_res8._0_1_,0,0);
    FUN_1816066c0(param_1,param_1 + 0x20,param_3,&local_70);
    if (param_3 != param_4) {
      FUN_1816066c0(param_1,param_1 + 0x20,param_4,&local_70);
    }
    FUN_1816066c0(param_1,param_1 + 8,param_2,&local_70);
    if (param_10 == '\0') {
      FUN_181609390(param_1,param_2,&local_70);
    }
    if (0 < (int)param_8) {
      FUN_181603d90(param_1 + 0xb0,&local_70,local_res10,&param_8,&param_9);
    }
    uVar4 = local_48[0];
    if ((int)local_48[0] < 0) {
      uVar4 = ~local_48[0];
    }
    FUN_181331dc0(local_48[0],uVar4,&local_res8,local_40,(ulonglong)local_res8 & 0xff,
                  local_res8._0_1_,0,0);
    FUN_1803e4920(&local_70);
  }
  uVar4 = *param_6;
  param_8 = 0;
  uVar2 = ~uVar4;
  if (-1 < (int)uVar4) {
    uVar2 = uVar4;
  }
  FUN_181331dc0(uVar4,uVar2,&local_res8,param_6 + 2);
  return;
}


//==== 13A2B60  FUN_1813a2b60 entry=0x13A2B60

undefined4 * FUN_1813a2b60(undefined4 *param_1,undefined8 param_2)

{
  *(undefined8 *)(param_1 + 2) = param_2;
  *param_1 = 1;
  return param_1;
}


//==== 16AF0C0  FUN_1816af0c0 entry=0x16AF0C0

float * FUN_1816af0c0(float *param_1,undefined4 param_2,longlong *param_3)

{
  float fVar1;
  char cVar2;
  longlong lVar3;
  longlong lVar4;
  longlong lVar5;
  undefined8 uVar6;
  undefined1 uVar7;
  undefined1 uVar8;
  float fVar9;
  float fVar10;
  
  fVar1 = DAT_181c7eb28;
  uVar7 = 0;
  uVar8 = 0;
  lVar3 = FUN_18157c9c0();
  fVar9 = fVar1;
  if (lVar3 != 0) {
    lVar4 = FUN_1812dc2e0(param_2);
    if ((lVar4 != 0) && (param_3 != (longlong *)0x0)) {
      if ((*param_3 != param_3[1]) &&
         ((cVar2 = FUN_181525590(lVar4), cVar2 != '\0' &&
          (fVar9 = (float)FUN_1814ae9e0(lVar3 + 0x7c0,&DAT_182bd6d88), fVar1 < fVar9)))) {
        FUN_181275620(lVar3 + 0x14b8,param_2);
        uVar7 = 1;
        uVar8 = 1;
      }
      lVar5 = (**(code **)(*DAT_18224da38 + 0x70))(DAT_18224da38,param_2);
      if ((lVar5 != 0) &&
         (cVar2 = FUN_18147f930(*(undefined8 *)(DAT_182c16840 + 0x318),lVar5,param_3), cVar2 != '\0'
         )) {
        uVar6 = FUN_18024dd50(lVar3 + 0x678);
        cVar2 = FUN_1812592d0(uVar6,param_2);
        if ((cVar2 != '\0') && (cVar2 = FUN_181524ee0(lVar4), cVar2 == '\0')) {
          fVar10 = (float)FUN_1814ae9e0(lVar3 + 0x7c0,&DAT_182bd6e88);
          if (fVar1 < fVar10) {
            FUN_181276fb0(lVar3 + 0x14b8,param_2,fVar10);
            uVar7 = 1;
            uVar8 = 1;
          }
          fVar9 = fVar9 * fVar10;
        }
      }
    }
    uVar6 = FUN_1808332a0(lVar3 + 0x678);
    cVar2 = FUN_181254010(uVar6,param_2);
    if (cVar2 != '\0') {
      fVar10 = (float)FUN_1814ae9e0(lVar3 + 0x7c0,&DAT_182bd6d28);
      fVar9 = fVar9 * fVar10;
      uVar8 = uVar7;
      if (fVar1 < fVar10) {
        uVar8 = 1;
      }
    }
  }
  *param_1 = fVar9;
  *(undefined1 *)(param_1 + 1) = uVar8;
  return param_1;
}


//==== 16AF980  FUN_1816af980 entry=0x16AF980

void FUN_1816af980(char param_1,undefined4 param_2)

{
  char cVar1;
  longlong lVar2;
  longlong *plVar3;
  longlong lVar4;
  longlong local_res18;
  longlong *local_res20;
  
  if (param_1 != '\0') {
    lVar2 = FUN_1813f60d0(param_2);
    if ((lVar2 == 0) || (0.0 < *(float *)(lVar2 + 0x40))) {
      plVar3 = (longlong *)(**(code **)(*DAT_18224da38 + 0x70))(DAT_18224da38,param_2);
      if (plVar3 != (longlong *)0x0) {
        lVar2 = (**(code **)(*plVar3 + 0x200))(plVar3,0x10);
        if ((lVar2 != 0) && (local_res18 = lVar2 + 0x28, local_res18 != 0)) {
          local_res20 = &local_res18;
          lVar2 = FUN_18156f560(&local_res20);
          if (lVar2 != 0) goto LAB_1816af9fc;
        }
      }
    }
    else {
LAB_1816af9fc:
      param_1 = '\0';
    }
  }
  lVar2 = FUN_18157c9c0();
  if ((lVar2 != 0) && (param_1 != '\0')) {
    lVar4 = (**(code **)(*DAT_18224da38 + 0x70))(DAT_18224da38,param_2);
    if (lVar4 != 0) {
      cVar1 = FUN_18147f930(*(undefined8 *)(DAT_182c16840 + 0x318),lVar4,lVar2 + 0x15e0);
      if (cVar1 == '\0') {
        param_1 = '\0';
        goto LAB_1816afa57;
      }
    }
    FUN_181273fd0(lVar2 + 0x14b8,param_2);
  }
LAB_1816afa57:
  FUN_181668b80(param_2,(param_1 != '\0') + '\x01');
  return;
}


//==== 141D5F0  FUN_18141d5f0 entry=0x141D5F0

void FUN_18141d5f0(undefined8 param_1,undefined4 param_2,undefined4 param_3,float param_4)

{
  if (param_4 <= 0.0) {
    param_4 = DAT_181c7f7a4;
  }
  FUN_18141d880(0,param_3,param_2,0,param_4);
  return;
}


//==== 141E020  FUN_18141e020 entry=0x141E020

undefined4 FUN_18141e020(longlong param_1,undefined4 param_2,ulonglong param_3)

{
  undefined4 uVar1;
  undefined8 *puVar2;
  longlong lVar3;
  char cVar4;
  undefined8 *puVar5;
  undefined8 *puVar6;
  ulonglong uVar7;
  undefined8 *puVar8;
  
  puVar2 = *(undefined8 **)(param_1 + 0x10);
  lVar3 = *(longlong *)(param_1 + 0x20);
  uVar7 = *(ulonglong *)(param_1 + 0x38) &
          ((((param_3 & 0xff ^ 0xcbf29ce484222325) * 0x100000001b3 ^ param_3 >> 8 & 0xff) *
            0x100000001b3 ^ param_3 >> 0x10 & 0xff) * 0x100000001b3 ^ param_3 >> 0x18 & 0xff) *
          0x100000001b3;
  puVar5 = *(undefined8 **)(lVar3 + uVar7 * 0x10);
  while( true ) {
    puVar8 = puVar2;
    if (*(undefined8 **)(lVar3 + uVar7 * 0x10) != puVar2) {
      puVar8 = (undefined8 *)**(undefined8 **)(lVar3 + uVar7 * 0x10 + 8);
    }
    puVar6 = puVar2;
    if ((puVar5 == puVar8) || (puVar6 = puVar5, *(int *)(puVar5 + 2) == (int)param_3)) break;
    puVar5 = (undefined8 *)*puVar5;
  }
  if (puVar6 != puVar2) {
    uVar1 = *(undefined4 *)(puVar6 + 3);
    cVar4 = FUN_18141e710(param_1,uVar1,param_2);
    if (cVar4 != '\0') {
      FUN_18141e120(param_1,uVar1,param_2);
      return uVar1;
    }
  }
  return 0;
}


//==== 147D2F0  FUN_18147d2f0 entry=0x147D2F0

undefined8 FUN_18147d2f0(longlong *param_1)

{
  longlong *plVar1;
  char cVar2;
  uint uVar3;
  undefined4 uVar4;
  int iVar5;
  longlong lVar6;
  undefined8 uVar7;
  
  if (param_1 == (longlong *)0x0) {
    return 0;
  }
  lVar6 = FUN_1813f6180();
  if ((lVar6 != 0) && (*(float *)(lVar6 + 0x40) <= 0.0)) {
    return 0;
  }
  uVar3 = (**(code **)(*param_1 + 0x2c8))(param_1,0);
  lVar6 = FUN_1812dc290(param_1);
  if ((lVar6 != 0) && (cVar2 = FUN_181525590(lVar6), cVar2 != '\0')) {
    lVar6 = FUN_18157c9c0();
    if (lVar6 == 0) {
      return 0;
    }
    uVar4 = (**(code **)(*param_1 + 8))(param_1);
    uVar7 = FUN_1808332a0(lVar6 + 0x678);
    cVar2 = FUN_181254010(uVar7,uVar4);
    if (cVar2 == '\0') {
      return 0;
    }
  }
  if ((uVar3 & 1) != 0) {
    plVar1 = *(longlong **)(DAT_182c16840 + 0x2d8);
    lVar6 = *plVar1;
    uVar4 = (**(code **)(*param_1 + 8))(param_1);
    iVar5 = (**(code **)(lVar6 + 0x58))(plVar1,uVar4,&DAT_00007777);
    if (iVar5 == 3) {
      return 1;
    }
  }
  return 0;
}


//==== 12DC290  FUN_1812dc290 entry=0x12DC290

undefined8 FUN_1812dc290(longlong *param_1)

{
  longlong lVar1;
  undefined8 uVar2;
  
  if (param_1 != (longlong *)0x0) {
    lVar1 = (**(code **)(*param_1 + 0x200))(param_1,0x10);
    if ((lVar1 != 0) && (lVar1 != -0x28)) {
      uVar2 = FUN_1812dc200();
      return uVar2;
    }
  }
  return 0;
}


//==== 167CCB0  FUN_18167ccb0 entry=0x167CCB0

undefined8 FUN_18167ccb0(longlong *param_1)

{
  undefined4 uVar1;
  undefined8 uVar2;
  
  if (param_1 != (longlong *)0x0) {
    uVar1 = (**(code **)(*param_1 + 8))();
    uVar2 = FUN_18167cce0(uVar1);
    return uVar2;
  }
  return 0;
}


//==== 1260D10  FUN_181260d10 entry=0x1260D10

void FUN_181260d10(longlong param_1,float param_2)

{
  int *piVar1;
  int iVar2;
  int iVar3;
  longlong *plVar4;
  float fVar5;
  float fVar6;
  longlong lVar7;
  undefined8 uVar8;
  undefined8 uVar9;
  longlong *plVar10;
  float fVar11;
  float fVar12;
  longlong *local_d8;
  longlong *local_d0;
  undefined8 local_c8;
  longlong *local_c0;
  undefined8 local_b8;
  longlong *local_b0;
  undefined **local_a8;
  undefined4 local_a0;
  undefined4 local_98;
  int local_90;
  float local_8c;
  undefined8 local_88;
  undefined8 local_80;
  undefined8 uStack_78;
  undefined8 local_70;
  undefined ***local_68;
  
  if ((*(int *)(param_1 + 0xc) == 0) || (param_2 <= 0.0)) {
    FUN_1811db9d0(param_1 + 0x10);
    fVar11 = (float)FUN_1811dba30(param_1 + 0x10);
    lVar7 = FUN_18157c990();
    plVar4 = *(longlong **)(lVar7 + 0x38);
    uVar8 = FUN_181665780();
    if ((*(char *)(param_1 + 10) == '\0') &&
       ((*(float *)(param_1 + 0x10) != 0.0 && (*(float *)(param_1 + 0x20) <= fVar11)))) {
      if (0 < *(int *)(DAT_182c16830 + 0x938)) {
        FUN_18118f690(uVar8,"staminaOpen");
      }
      *(undefined1 *)(param_1 + 10) = 1;
    }
    fVar6 = DAT_181c7f7a8;
    if (param_2 <= 0.0) {
      if (param_2 < 0.0) {
        if (*(float *)(param_1 + 0x10) == 0.0) {
          if (*(char *)(param_1 + 8) != '\0') {
            *(undefined1 *)(param_1 + 8) = 0;
            lVar7 = FUN_18157c990();
            uVar9 = FUN_181587ca0(lVar7 + 0x678);
            plVar10 = (longlong *)FUN_18148f000(uVar9,7);
            if ((plVar10 != (longlong *)0x0) && (-1 < (int)plVar10[4])) {
              (**(code **)(*plVar10 + 0x30))(plVar10,0);
            }
          }
          FUN_18118f690(uVar8,"staminaClose");
          *(undefined1 *)(param_1 + 10) = 0;
          FUN_1812609f0(uVar8,"staminaUpdate",fVar6,fVar6,*(undefined1 *)(param_1 + 8));
        }
        if ((fVar11 < *(float *)(param_1 + 0x1c) || fVar11 == *(float *)(param_1 + 0x1c)) &&
           (*(char *)(param_1 + 9) != '\0')) {
          FUN_1810de6d0(param_1 + 0x34,plVar4);
          *(undefined1 *)(param_1 + 9) = 0;
        }
      }
    }
    else {
      if (((*(float *)(param_1 + 0x14) == *(float *)(param_1 + 0x10)) &&
          (*(char *)(param_1 + 8) != '\x01')) && (*(int *)(param_1 + 0xc) == 0)) {
        *(undefined1 *)(param_1 + 8) = 1;
      }
      FUN_181284dc0(param_1 + 0x24);
      if ((*(char *)(param_1 + 9) == '\0') && (*(float *)(param_1 + 0x18) <= fVar11)) {
        FUN_1810de6d0(param_1 + 0x30,plVar4);
        *(undefined1 *)(param_1 + 9) = 1;
      }
    }
    fVar5 = DAT_181c7eb28;
    fVar12 = fVar11;
    if (*(char *)(param_1 + 8) != '\0') {
      fVar12 = DAT_181c7eb28;
    }
    iVar3 = *(int *)(param_1 + 0x2c);
    if (iVar3 != 0) {
      if (plVar4 == (longlong *)0x0) {
        local_a8 = &PTR_FUN_181c8c7e8;
        local_70 = 0;
        local_68 = &local_a8;
        local_a0 = 3;
        local_98 = 0x100;
        local_88 = 1;
        local_80 = 0;
        uStack_78 = 0;
        local_90 = iVar3;
        local_8c = fVar12;
        (**(code **)(*DAT_18224da50 + 0x18))(DAT_18224da50,&local_88);
      }
      else {
        (**(code **)(*plVar4 + 0x210))(plVar4,&local_c8,3);
        if (local_c0 != (longlong *)0x0) {
          LOCK();
          *(int *)(local_c0 + 1) = (int)local_c0[1] + 1;
          UNLOCK();
        }
        local_b8 = local_c8;
        local_b0 = local_c0;
        FUN_18097f0b0(&local_d8,&local_b8);
        if (local_c0 != (longlong *)0x0) {
          LOCK();
          plVar4 = local_c0 + 1;
          lVar7 = *plVar4;
          *(int *)plVar4 = (int)*plVar4 + -1;
          UNLOCK();
          if ((int)lVar7 == 1) {
            (**(code **)*local_c0)(local_c0);
            LOCK();
            piVar1 = (int *)((longlong)local_c0 + 0xc);
            iVar2 = *piVar1;
            *piVar1 = *piVar1 + -1;
            UNLOCK();
            if (iVar2 == 1) {
              (**(code **)(*local_c0 + 8))(local_c0);
            }
          }
        }
        (**(code **)(*local_d8 + 0xf0))(local_d8,iVar3,fVar12,1);
        plVar4 = local_d0;
        if (local_d0 != (longlong *)0x0) {
          LOCK();
          plVar10 = local_d0 + 1;
          lVar7 = *plVar10;
          *(int *)plVar10 = (int)*plVar10 + -1;
          UNLOCK();
          if ((int)lVar7 == 1) {
            (**(code **)*local_d0)(local_d0);
            LOCK();
            piVar1 = (int *)((longlong)plVar4 + 0xc);
            iVar3 = *piVar1;
            *piVar1 = *piVar1 + -1;
            UNLOCK();
            if (iVar3 == 1) {
              (**(code **)(*local_d0 + 8))();
            }
          }
        }
      }
    }
    if (*(char *)(param_1 + 10) != '\0') {
      FUN_1812609f0(uVar8,"staminaUpdate",(fVar5 - fVar11) * fVar6,fVar6,
                    *(undefined1 *)(param_1 + 8));
    }
  }
  return;
}


//==== 1261320  FUN_181261320 entry=0x1261320

void FUN_181261320(longlong param_1,float param_2)

{
  char *pcVar1;
  int iVar2;
  longlong *plVar3;
  size_t _Size;
  float local_res8 [2];
  undefined1 local_48 [8];
  undefined1 *local_40;
  undefined2 *local_38;
  undefined1 local_30 [8];
  undefined1 *local_28;
  undefined4 local_20;
  size_t sVar4;
  
  plVar3 = (longlong *)FUN_181665780();
  if (*(char *)(param_1 + 10) == '\0') {
    if (*(float *)(param_1 + 0x20) <=
        (*(float *)(param_1 + 0x10) - param_2) / *(float *)(param_1 + 0x14)) {
      if (0 < *(int *)(DAT_182c16830 + 0x938)) {
        FUN_18118f690(plVar3,"staminaOpen");
      }
      *(undefined1 *)(param_1 + 10) = 1;
    }
    if (*(char *)(param_1 + 10) == '\0') {
      return;
    }
  }
  local_res8[0] = (param_2 / *(float *)(param_1 + 0x14)) * DAT_181c7f7a8;
  FUN_1802b2870(local_48);
  local_38 = &DAT_18224df7c;
  local_40 = &DAT_18224d03c;
  FUN_1802b27f0(local_30);
  local_28 = &DAT_18224d03c;
  sVar4 = 0xffffffffffffffff;
  do {
    _Size = sVar4 + 1;
    pcVar1 = &DAT_181ca2d79 + sVar4;
    sVar4 = _Size;
  } while (*pcVar1 != '\0');
  if ((_Size != 0) && (FUN_180095610(&local_28,_Size), local_28 != &DAT_181ca2d78)) {
    memcpy(local_28,&DAT_181ca2d78,_Size);
  }
  local_20 = 8;
  FUN_1802e7bb0(local_48,local_res8);
  (**(code **)(*plVar3 + 0x210))(plVar3,"staminaPreviewUpdate",local_48,0,0);
  iVar2 = *(int *)(local_28 + -0xc);
  if ((-1 < iVar2) && (iVar2 = iVar2 + -1, *(int *)(local_28 + -0xc) = iVar2, iVar2 < 1)) {
    FUN_1800a8b20();
  }
  FUN_1802b2650(local_30,0,0);
  iVar2 = *(int *)(local_38 + -6);
  if ((-1 < iVar2) && (iVar2 = iVar2 + -1, *(int *)(local_38 + -6) = iVar2, iVar2 < 1)) {
    FUN_1800a8b20();
  }
  iVar2 = *(int *)(local_40 + -0xc);
  if ((-1 < iVar2) && (iVar2 = iVar2 + -1, *(int *)(local_40 + -0xc) = iVar2, iVar2 < 1)) {
    FUN_1800a8b20();
  }
  FUN_1802b24b0(local_48);
  FUN_1802b2720(local_48,0,0);
  return;
}


//==== 12618E0  FUN_1812618e0 entry=0x12618E0

undefined1 FUN_1812618e0(longlong param_1)

{
  undefined8 uVar1;
  
  if (*(float *)(param_1 + 0x14) == *(float *)(param_1 + 0x10)) {
    uVar1 = FUN_181665780();
    FUN_18118f690(uVar1,"staminaDrained");
  }
  if (*(float *)(param_1 + 0x14) == *(float *)(param_1 + 0x10)) {
    return 0;
  }
  return 1;
}


//==== 1695020  FUN_181695020 entry=0x1695020

void FUN_181695020(longlong param_1)

{
  FUN_1814ae9e0(param_1 + 0x1a8);
  return;
}


//==== 1083490  FUN_181083490 entry=0x1083490

bool FUN_181083490(uint *param_1)

{
  uint uVar1;
  ulonglong uVar2;
  uint uVar3;
  uint uVar4;
  
  uVar1 = *param_1;
  uVar3 = uVar1 >> 0xe & 0x3f;
  if (uVar3 == 0) {
    return (bool)((byte)(uVar1 >> 0x14) & 1);
  }
  uVar4 = uVar1 & 0x1ff;
  if ((ulonglong)uVar4 < 399) {
    *param_1 = uVar1 & 0xfffffe00 | uVar4 + 1;
  }
  else {
    FUN_181083890();
  }
  uVar2 = (((ulonglong)uVar3 - 1) * 0x10 + (ulonglong)(uVar1 >> 9 & 0x1f)) * 400 + (ulonglong)uVar4;
  if (uVar2 < 320000) {
    uVar2 = 1L << ((byte)uVar2 & 0x3f) & *(ulonglong *)(&DAT_182bb5610 + (uVar2 >> 6) * 8);
    if ((uVar1 >> 0x14 & 1) != 0) {
      return uVar2 == 0;
    }
    return uVar2 != 0;
  }
                    /* WARNING: Subroutine does not return */
  FUN_180f00fb0(&DAT_182bb5610);
}


//==== 13B93E0  FUN_1813b93e0 entry=0x13B93E0

void FUN_1813b93e0(longlong param_1,undefined8 param_2,undefined4 param_3)

{
  int iVar1;
  
  iVar1 = FUN_1813bf730(param_1 + 0x4c0,param_2,param_1,param_3,0);
  if (iVar1 == 1) {
    switch(*(undefined4 *)(param_1 + 0x518)) {
    case 1:
      FUN_1813b8680(*(undefined4 *)(*(longlong *)(param_1 + 0x470) + 0x1c),param_1,DAT_181c7eb28);
      *(undefined4 *)(param_1 + 0x518) = 8;
      *(undefined1 *)(param_1 + 0x4fd) = 1;
      return;
    case 2:
      FUN_1813b8680(*(undefined4 *)(*(longlong *)(param_1 + 0x470) + 0x20),param_1,DAT_181c7eb28);
      *(undefined4 *)(param_1 + 0x518) = 9;
      *(undefined1 *)(param_1 + 0x4fd) = 1;
      return;
    case 3:
      FUN_1813b8680(*(undefined4 *)(*(longlong *)(param_1 + 0x470) + 0x24),param_1,DAT_181c7eb28);
      *(undefined4 *)(param_1 + 0x518) = 10;
      *(undefined1 *)(param_1 + 0x4fd) = 1;
      return;
    case 4:
      FUN_1813b8680(*(undefined4 *)(*(longlong *)(param_1 + 0x470) + 0x28),param_1,DAT_181c7eb28);
      *(undefined4 *)(param_1 + 0x518) = 0xb;
      *(undefined1 *)(param_1 + 0x4fd) = 1;
      return;
    case 5:
      FUN_1813b8680(*(undefined4 *)(*(longlong *)(param_1 + 0x470) + 0x2c),param_1,DAT_181c7eb28);
      *(undefined4 *)(param_1 + 0x518) = 0xc;
      *(undefined1 *)(param_1 + 0x4fd) = 1;
      return;
    case 6:
      FUN_1813b8680(*(undefined4 *)(*(longlong *)(param_1 + 0x470) + 0x30),param_1,DAT_181c7eb28);
      *(undefined4 *)(param_1 + 0x518) = 0xd;
      *(undefined1 *)(param_1 + 0x4fd) = 1;
      return;
    case 7:
      FUN_1813b8680(*(undefined4 *)(*(longlong *)(param_1 + 0x470) + 0x34),param_1,DAT_181c7eb28);
      *(undefined4 *)(param_1 + 0x518) = 0xe;
      *(undefined1 *)(param_1 + 0x4fd) = 1;
    }
  }
  else if (iVar1 == 2) {
    switch(*(undefined4 *)(param_1 + 0x518)) {
    case 1:
      FUN_1813b8680(*(undefined4 *)(*(longlong *)(param_1 + 0x470) + 0x38),param_1,DAT_181c7eb28);
      *(undefined4 *)(param_1 + 0x518) = 0xf;
      *(undefined1 *)(param_1 + 0x4fd) = 1;
      return;
    case 2:
      FUN_1813b8680(*(undefined4 *)(*(longlong *)(param_1 + 0x470) + 0x3c),param_1,DAT_181c7eb28);
      *(undefined4 *)(param_1 + 0x518) = 0x10;
      *(undefined1 *)(param_1 + 0x4fd) = 1;
      return;
    case 3:
      FUN_1813b8680(*(undefined4 *)(*(longlong *)(param_1 + 0x470) + 0x40),param_1,DAT_181c7eb28);
      *(undefined4 *)(param_1 + 0x518) = 0x11;
      *(undefined1 *)(param_1 + 0x4fd) = 1;
      return;
    case 4:
      FUN_1813b8680(*(undefined4 *)(*(longlong *)(param_1 + 0x470) + 0x44),param_1,DAT_181c7eb28);
      *(undefined4 *)(param_1 + 0x518) = 0x12;
      *(undefined1 *)(param_1 + 0x4fd) = 1;
      return;
    case 5:
      FUN_1813b8680(*(undefined4 *)(*(longlong *)(param_1 + 0x470) + 0x48),param_1,DAT_181c7eb28);
      *(undefined4 *)(param_1 + 0x518) = 0x13;
      *(undefined1 *)(param_1 + 0x4fd) = 1;
      return;
    case 6:
      FUN_1813b8680(*(undefined4 *)(*(longlong *)(param_1 + 0x470) + 0x4c),param_1,DAT_181c7eb28);
      *(undefined4 *)(param_1 + 0x518) = 0x14;
      *(undefined1 *)(param_1 + 0x4fd) = 1;
      return;
    case 7:
      FUN_1813b8680(*(undefined4 *)(*(longlong *)(param_1 + 0x470) + 0x50),param_1,DAT_181c7eb28);
      *(undefined4 *)(param_1 + 0x518) = 0x15;
      *(undefined1 *)(param_1 + 0x4fd) = 1;
      return;
    }
  }
  return;
}


//==== 13BEDE0  FUN_1813bede0 entry=0x13BEDE0

void FUN_1813bede0(longlong param_1,longlong param_2,undefined4 param_3)

{
  uint uVar1;
  uint uVar2;
  uint uVar3;
  undefined4 uVar4;
  longlong lVar5;
  undefined8 uVar6;
  undefined4 *puVar7;
  uint uVar8;
  uint uVar9;
  uint uVar10;
  undefined4 local_f8;
  undefined4 local_f4;
  undefined4 local_f0;
  undefined8 local_ec;
  undefined4 local_e4;
  undefined8 local_e0;
  undefined8 uStack_d8;
  undefined8 local_d0;
  undefined4 local_c8;
  undefined4 local_c4;
  undefined4 local_c0;
  undefined4 local_bc;
  undefined4 local_b8;
  undefined4 local_b4;
  uint local_b0;
  uint local_ac;
  uint local_a8;
  uint local_a4;
  uint local_a0;
  uint local_9c;
  undefined4 local_98;
  undefined4 local_94;
  undefined8 local_90;
  undefined1 local_88 [128];
  
  lVar5 = FUN_18157c990();
  uVar6 = FUN_181587c20(lVar5 + 0x678);
  FUN_181260d10(uVar6,*(undefined4 *)(param_1 + 0x38));
  FUN_181261320(uVar6,*(undefined4 *)(param_1 + 0x38));
  lVar5 = FUN_18157c990();
  lVar5 = (*(code *)**(undefined8 **)(lVar5 + 0x40))(lVar5 + 0x40,local_88);
  uVar4 = *(undefined4 *)(param_2 + 0x38);
  uVar1 = *(uint *)(lVar5 + 0xc);
  uVar2 = *(uint *)(lVar5 + 0x10);
  uVar3 = *(uint *)(lVar5 + 0x14);
  uVar10 = uVar1 ^ DAT_181c7eb70;
  uVar9 = uVar2 ^ DAT_181c7eb70;
  uVar8 = uVar3 ^ DAT_181c7eb70;
  puVar7 = (undefined4 *)(**(code **)(**(longlong **)(param_2 + 0x40) + 0x110))();
  local_c4 = 0;
  local_f8 = 0x7777;
  local_ec = 0;
  local_e4 = 0;
  local_e0 = 0;
  uStack_d8 = 0;
  local_d0 = 0;
  local_c8 = 0xffffffff;
  local_c0 = 0;
  local_bc = *puVar7;
  local_b8 = puVar7[1];
  local_b4 = puVar7[2];
  local_98 = 0xffffffff;
  local_94 = 0;
  local_90 = 0;
  local_f4 = uVar4;
  local_f0 = param_3;
  local_b0 = uVar1;
  local_ac = uVar2;
  local_a8 = uVar3;
  local_a4 = uVar10;
  local_a0 = uVar9;
  local_9c = uVar8;
  uVar6 = FUN_181539730(param_2);
  FUN_18153a910(uVar6,&local_f8,param_1,0,0);
  return;
}


//==== 16B2290  FUN_1816b2290 entry=0x16B2290

void FUN_1816b2290(longlong param_1,uint param_2)

{
  byte bVar1;
  longlong lVar2;
  byte bVar3;
  ulonglong uVar4;
  uint uVar5;
  char *pcVar6;
  byte *pbVar7;
  undefined4 uVar8;
  longlong local_res8;
  
  lVar2 = FUN_1816a5650(*(undefined4 *)(param_1 + 0x80));
  if (lVar2 != 0) {
    uVar4 = 0xffffffff;
    uVar5 = 0xffffffff;
    pbVar7 = &DAT_181e31110;
    do {
      bVar1 = *pbVar7;
      pbVar7 = pbVar7 + 1;
      bVar3 = bVar1 + 0x20;
      if (0x19 < (byte)(bVar1 + 0xbf)) {
        bVar3 = bVar1;
      }
      uVar5 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar3 ^ (ulonglong)(uVar5 & 0xff)) * 4) ^
              uVar5 >> 8;
    } while (*pbVar7 != 0);
    local_res8 = param_1;
    if (param_2 == ~uVar5) {
      *(undefined4 *)(lVar2 + 0x490) = 0;
      uVar8 = FUN_1816b04c0(&local_res8);
      FUN_1816b2bc0(lVar2,uVar8,DAT_181c7eb28);
      return;
    }
    pcVar6 = "DoubleHit";
    do {
      bVar1 = *pcVar6;
      pcVar6 = pcVar6 + 1;
      bVar3 = bVar1 + 0x20;
      if (0x19 < (byte)(bVar1 + 0xbf)) {
        bVar3 = bVar1;
      }
      uVar5 = *(uint *)(&DAT_181c8ae40 + ((ulonglong)bVar3 ^ uVar4 & 0xff) * 4) ^ (uint)(uVar4 >> 8)
      ;
      uVar4 = (ulonglong)uVar5;
    } while (*pcVar6 != 0);
    if (param_2 == ~uVar5) {
      *(undefined4 *)(lVar2 + 0x490) = 0;
      uVar8 = FUN_1816b04c0(&local_res8);
      FUN_1816b2bc0(lVar2,uVar8,DAT_181c80790);
      return;
    }
    uVar5 = FUN_180d58d50("Chain");
    if (param_2 == uVar5) {
      *(undefined1 *)(lVar2 + 0x506) = 1;
      return;
    }
    uVar5 = FUN_180d58d50("SetParamClip");
    if (param_2 == uVar5) {
      uVar8 = FUN_1816b04c0(&local_res8);
      FUN_1813c0160(lVar2 + 0x4c8,lVar2,uVar8);
      return;
    }
    uVar5 = FUN_180d58d50("Charge");
    if (param_2 == uVar5) {
      if (*(char *)(lVar2 + 0x530) != '\0') {
        *(undefined1 *)(lVar2 + 0x531) = 1;
        return;
      }
      FUN_1816b0810(lVar2);
    }
  }
  return;
}


//==== B9F370  FUN_180b9f370 entry=0xB9F370

undefined8 * FUN_180b9f370(longlong param_1,undefined4 param_2,undefined8 *param_3)

{
  int *piVar1;
  char cVar2;
  undefined8 uVar3;
  undefined8 *puVar4;
  undefined8 *puVar5;
  bool bVar6;
  undefined8 *local_res20;
  undefined8 *local_28;
  undefined8 *local_20;
  undefined8 *local_18 [2];
  
  switch(param_2) {
  case 0:
    FUN_180b9e030(param_1,0,param_3,&local_res20);
    return local_res20;
  case 1:
    LOCK();
    bVar6 = *(int *)(param_1 + 0x5798) == 0;
    if (bVar6) {
      *(int *)(param_1 + 0x5798) = 0x10000;
    }
    UNLOCK();
    while (!bVar6) {
      LOCK();
      bVar6 = *(int *)(param_1 + 0x5798) == 0;
      if (bVar6) {
        *(int *)(param_1 + 0x5798) = 0x10000;
      }
      UNLOCK();
    }
    puVar5 = (undefined8 *)FUN_180ba0330(param_1,1,0x80);
    uVar3 = param_3[1];
    *puVar5 = *param_3;
    puVar5[1] = uVar3;
    uVar3 = param_3[3];
    puVar5[2] = param_3[2];
    puVar5[3] = uVar3;
    uVar3 = param_3[5];
    puVar5[4] = param_3[4];
    puVar5[5] = uVar3;
    uVar3 = param_3[7];
    puVar5[6] = param_3[6];
    puVar5[7] = uVar3;
    uVar3 = param_3[9];
    puVar5[8] = param_3[8];
    puVar5[9] = uVar3;
    uVar3 = param_3[0xb];
    puVar5[10] = param_3[10];
    puVar5[0xb] = uVar3;
    uVar3 = param_3[0xd];
    puVar5[0xc] = param_3[0xc];
    puVar5[0xd] = uVar3;
    uVar3 = param_3[0xf];
    puVar5[0xe] = param_3[0xe];
    puVar5[0xf] = uVar3;
    break;
  case 2:
    FUN_180b9e160(param_1,0x2000000,param_3,&local_28);
    return local_28;
  case 3:
    LOCK();
    bVar6 = *(int *)(param_1 + 0x5798) == 0;
    if (bVar6) {
      *(int *)(param_1 + 0x5798) = 0x10000;
    }
    UNLOCK();
    while (!bVar6) {
      LOCK();
      bVar6 = *(int *)(param_1 + 0x5798) == 0;
      if (bVar6) {
        *(int *)(param_1 + 0x5798) = 0x10000;
      }
      UNLOCK();
    }
    puVar5 = (undefined8 *)FUN_180ba0330(param_1,3,0x40);
    uVar3 = param_3[1];
    *puVar5 = *param_3;
    puVar5[1] = uVar3;
    uVar3 = param_3[3];
    puVar5[2] = param_3[2];
    puVar5[3] = uVar3;
    uVar3 = param_3[5];
    puVar5[4] = param_3[4];
    puVar5[5] = uVar3;
    uVar3 = param_3[7];
    puVar5[6] = param_3[6];
    puVar5[7] = uVar3;
    break;
  case 4:
    LOCK();
    bVar6 = *(int *)(param_1 + 0x5798) == 0;
    if (bVar6) {
      *(int *)(param_1 + 0x5798) = 0x10000;
    }
    UNLOCK();
    while (!bVar6) {
      LOCK();
      bVar6 = *(int *)(param_1 + 0x5798) == 0;
      if (bVar6) {
        *(int *)(param_1 + 0x5798) = 0x10000;
      }
      UNLOCK();
    }
    puVar4 = (undefined8 *)FUN_180ba0330(param_1,4,0x50);
    uVar3 = param_3[1];
    *puVar4 = *param_3;
    puVar4[1] = uVar3;
    uVar3 = param_3[3];
    puVar4[2] = param_3[2];
    puVar4[3] = uVar3;
    uVar3 = param_3[5];
    puVar4[4] = param_3[4];
    puVar4[5] = uVar3;
    uVar3 = param_3[7];
    puVar4[6] = param_3[6];
    puVar4[7] = uVar3;
    uVar3 = param_3[9];
    puVar4[8] = param_3[8];
    puVar4[9] = uVar3;
    *puVar4 = 0;
    puVar5 = *(undefined8 **)(param_1 + 0x51e8);
    if (puVar5 == (undefined8 *)0x0) {
      puVar5 = (undefined8 *)(param_1 + 0x51e0);
    }
    *puVar5 = puVar4;
    *(undefined8 **)(param_1 + 0x51e8) = puVar4;
    if ((*(uint *)(param_3[2] + 0x40) & 0x10000) != 0) {
      LOCK();
      piVar1 = (int *)(param_3[2] + 0x40);
      *piVar1 = *piVar1 + -0x10000;
      UNLOCK();
    }
    LOCK();
    *(int *)(param_1 + 0x5798) = *(int *)(param_1 + 0x5798) + -0x10000;
    UNLOCK();
    return puVar4;
  case 5:
    FUN_180b765a0(param_1,2,param_3,local_18);
    return local_18[0];
  case 6:
    LOCK();
    bVar6 = *(int *)(param_1 + 0x5798) == 0;
    if (bVar6) {
      *(int *)(param_1 + 0x5798) = 0x10000;
    }
    UNLOCK();
    while (!bVar6) {
      LOCK();
      bVar6 = *(int *)(param_1 + 0x5798) == 0;
      if (bVar6) {
        *(int *)(param_1 + 0x5798) = 0x10000;
      }
      UNLOCK();
    }
    puVar5 = (undefined8 *)FUN_180ba0330(param_1,6,200);
    uVar3 = param_3[1];
    *puVar5 = *param_3;
    puVar5[1] = uVar3;
    uVar3 = param_3[3];
    puVar5[2] = param_3[2];
    puVar5[3] = uVar3;
    uVar3 = param_3[5];
    puVar5[4] = param_3[4];
    puVar5[5] = uVar3;
    uVar3 = param_3[7];
    puVar5[6] = param_3[6];
    puVar5[7] = uVar3;
    uVar3 = param_3[9];
    puVar5[8] = param_3[8];
    puVar5[9] = uVar3;
    uVar3 = param_3[0xb];
    puVar5[10] = param_3[10];
    puVar5[0xb] = uVar3;
    uVar3 = param_3[0xd];
    puVar5[0xc] = param_3[0xc];
    puVar5[0xd] = uVar3;
    uVar3 = param_3[0xf];
    puVar5[0xe] = param_3[0xe];
    puVar5[0xf] = uVar3;
    uVar3 = param_3[0x11];
    puVar5[0x10] = param_3[0x10];
    puVar5[0x11] = uVar3;
    uVar3 = param_3[0x13];
    puVar5[0x12] = param_3[0x12];
    puVar5[0x13] = uVar3;
    uVar3 = param_3[0x15];
    puVar5[0x14] = param_3[0x14];
    puVar5[0x15] = uVar3;
    uVar3 = param_3[0x17];
    puVar5[0x16] = param_3[0x16];
    puVar5[0x17] = uVar3;
    puVar5[0x18] = param_3[0x18];
    goto LAB_180b9f63f;
  case 7:
    LOCK();
    bVar6 = *(int *)(param_1 + 0x5798) == 0;
    if (bVar6) {
      *(int *)(param_1 + 0x5798) = 0x10000;
    }
    UNLOCK();
    while (!bVar6) {
      LOCK();
      bVar6 = *(int *)(param_1 + 0x5798) == 0;
      if (bVar6) {
        *(int *)(param_1 + 0x5798) = 0x10000;
      }
      UNLOCK();
    }
    puVar5 = (undefined8 *)FUN_180ba0330(param_1,7,0x40);
    uVar3 = param_3[1];
    *puVar5 = *param_3;
    puVar5[1] = uVar3;
    uVar3 = param_3[3];
    puVar5[2] = param_3[2];
    puVar5[3] = uVar3;
    uVar3 = param_3[5];
    puVar5[4] = param_3[4];
    puVar5[5] = uVar3;
    uVar3 = param_3[7];
    puVar5[6] = param_3[6];
    puVar5[7] = uVar3;
    *puVar5 = 0;
    puVar4 = *(undefined8 **)(param_1 + 0x51e8);
    if (puVar4 == (undefined8 *)0x0) {
      *(undefined8 **)(param_1 + 0x51e0) = puVar5;
      *(undefined8 **)(param_1 + 0x51e8) = puVar5;
      LOCK();
      *(int *)(param_1 + 0x5798) = *(int *)(param_1 + 0x5798) + -0x10000;
      UNLOCK();
      return puVar5;
    }
    goto LAB_180b9f487;
  case 8:
    FUN_180b9e2d0(param_1,0x1000000,param_3,&local_20);
    return local_20;
  case 9:
    LOCK();
    *(int *)(param_1 + 0x57a0) = *(int *)(param_1 + 0x57a0) + 1;
    UNLOCK();
    cVar2 = *(char *)(param_1 + 0x57a2);
    while (cVar2 != '\0') {
      cVar2 = *(char *)(param_1 + 0x57a2);
    }
    for (puVar5 = *(undefined8 **)(param_1 + 0x5300); puVar5 != (undefined8 *)0x0;
        puVar5 = (undefined8 *)puVar5[1]) {
      (*(code *)*puVar5)(param_3);
    }
    LOCK();
    *(int *)(param_1 + 0x57a0) = *(int *)(param_1 + 0x57a0) + -1;
    UNLOCK();
    LOCK();
    bVar6 = *(int *)(param_1 + 0x5798) == 0;
    if (bVar6) {
      *(int *)(param_1 + 0x5798) = 0x10000;
    }
    UNLOCK();
    while (!bVar6) {
      LOCK();
      bVar6 = *(int *)(param_1 + 0x5798) == 0;
      if (bVar6) {
        *(int *)(param_1 + 0x5798) = 0x10000;
      }
      UNLOCK();
    }
    puVar5 = (undefined8 *)FUN_180ba0330(param_1,9,0x48);
    uVar3 = param_3[1];
    *puVar5 = *param_3;
    puVar5[1] = uVar3;
    uVar3 = param_3[3];
    puVar5[2] = param_3[2];
    puVar5[3] = uVar3;
    uVar3 = param_3[5];
    puVar5[4] = param_3[4];
    puVar5[5] = uVar3;
    uVar3 = param_3[7];
    puVar5[6] = param_3[6];
    puVar5[7] = uVar3;
    puVar5[8] = param_3[8];
    break;
  case 10:
    LOCK();
    *(int *)(param_1 + 0x57a0) = *(int *)(param_1 + 0x57a0) + 1;
    UNLOCK();
    cVar2 = *(char *)(param_1 + 0x57a2);
    while (cVar2 != '\0') {
      cVar2 = *(char *)(param_1 + 0x57a2);
    }
    for (puVar5 = *(undefined8 **)(param_1 + 0x5310); puVar5 != (undefined8 *)0x0;
        puVar5 = (undefined8 *)puVar5[1]) {
      (*(code *)*puVar5)(param_3);
    }
    LOCK();
    *(int *)(param_1 + 0x57a0) = *(int *)(param_1 + 0x57a0) + -1;
    UNLOCK();
    LOCK();
    bVar6 = *(int *)(param_1 + 0x5798) == 0;
    if (bVar6) {
      *(int *)(param_1 + 0x5798) = 0x10000;
    }
    UNLOCK();
    while (!bVar6) {
      LOCK();
      bVar6 = *(int *)(param_1 + 0x5798) == 0;
      if (bVar6) {
        *(int *)(param_1 + 0x5798) = 0x10000;
      }
      UNLOCK();
    }
    puVar5 = (undefined8 *)FUN_180ba0330(param_1,10,0x58);
    uVar3 = param_3[1];
    *puVar5 = *param_3;
    puVar5[1] = uVar3;
    uVar3 = param_3[3];
    puVar5[2] = param_3[2];
    puVar5[3] = uVar3;
    uVar3 = param_3[5];
    puVar5[4] = param_3[4];
    puVar5[5] = uVar3;
    uVar3 = param_3[7];
    puVar5[6] = param_3[6];
    puVar5[7] = uVar3;
    uVar3 = param_3[9];
    puVar5[8] = param_3[8];
    puVar5[9] = uVar3;
    puVar5[10] = param_3[10];
    break;
  case 0xb:
    LOCK();
    *(int *)(param_1 + 0x57a0) = *(int *)(param_1 + 0x57a0) + 1;
    UNLOCK();
    cVar2 = *(char *)(param_1 + 0x57a2);
    while (cVar2 != '\0') {
      cVar2 = *(char *)(param_1 + 0x57a2);
    }
    for (puVar5 = *(undefined8 **)(param_1 + 0x5320); puVar5 != (undefined8 *)0x0;
        puVar5 = (undefined8 *)puVar5[1]) {
      (*(code *)*puVar5)(param_3);
    }
    LOCK();
    *(int *)(param_1 + 0x57a0) = *(int *)(param_1 + 0x57a0) + -1;
    UNLOCK();
    LOCK();
    bVar6 = *(int *)(param_1 + 0x5798) == 0;
    if (bVar6) {
      *(int *)(param_1 + 0x5798) = 0x10000;
    }
    UNLOCK();
    while (!bVar6) {
      LOCK();
      bVar6 = *(int *)(param_1 + 0x5798) == 0;
      if (bVar6) {
        *(int *)(param_1 + 0x5798) = 0x10000;
      }
      UNLOCK();
    }
    puVar5 = (undefined8 *)FUN_180ba0330(param_1,0xb,0xa8);
    uVar3 = param_3[1];
    *puVar5 = *param_3;
    puVar5[1] = uVar3;
    uVar3 = param_3[3];
    puVar5[2] = param_3[2];
    puVar5[3] = uVar3;
    uVar3 = param_3[5];
    puVar5[4] = param_3[4];
    puVar5[5] = uVar3;
    uVar3 = param_3[7];
    puVar5[6] = param_3[6];
    puVar5[7] = uVar3;
    uVar3 = param_3[9];
    puVar5[8] = param_3[8];
    puVar5[9] = uVar3;
    uVar3 = param_3[0xb];
    puVar5[10] = param_3[10];
    puVar5[0xb] = uVar3;
    uVar3 = param_3[0xd];
    puVar5[0xc] = param_3[0xc];
    puVar5[0xd] = uVar3;
    uVar3 = param_3[0xf];
    puVar5[0xe] = param_3[0xe];
    puVar5[0xf] = uVar3;
    uVar3 = param_3[0x11];
    puVar5[0x10] = param_3[0x10];
    puVar5[0x11] = uVar3;
    uVar3 = param_3[0x13];
    puVar5[0x12] = param_3[0x12];
    puVar5[0x13] = uVar3;
    puVar5[0x14] = param_3[0x14];
    goto LAB_180b9f63f;
  case 0xc:
    LOCK();
    *(int *)(param_1 + 0x57a0) = *(int *)(param_1 + 0x57a0) + 1;
    UNLOCK();
    cVar2 = *(char *)(param_1 + 0x57a2);
    while (cVar2 != '\0') {
      cVar2 = *(char *)(param_1 + 0x57a2);
    }
    for (puVar5 = *(undefined8 **)(param_1 + 0x5330); puVar5 != (undefined8 *)0x0;
        puVar5 = (undefined8 *)puVar5[1]) {
      (*(code *)*puVar5)(param_3);
    }
    LOCK();
    *(int *)(param_1 + 0x57a0) = *(int *)(param_1 + 0x57a0) + -1;
    UNLOCK();
    LOCK();
    bVar6 = *(int *)(param_1 + 0x5798) == 0;
    if (bVar6) {
      *(int *)(param_1 + 0x5798) = 0x10000;
    }
    UNLOCK();
    while (!bVar6) {
      LOCK();
      bVar6 = *(int *)(param_1 + 0x5798) == 0;
      if (bVar6) {
        *(int *)(param_1 + 0x5798) = 0x10000;
      }
      UNLOCK();
    }
    puVar5 = (undefined8 *)FUN_180ba0330(param_1,0xc,0xa8);
    uVar3 = param_3[1];
    *puVar5 = *param_3;
    puVar5[1] = uVar3;
    uVar3 = param_3[3];
    puVar5[2] = param_3[2];
    puVar5[3] = uVar3;
    uVar3 = param_3[5];
    puVar5[4] = param_3[4];
    puVar5[5] = uVar3;
    uVar3 = param_3[7];
    puVar5[6] = param_3[6];
    puVar5[7] = uVar3;
    uVar3 = param_3[9];
    puVar5[8] = param_3[8];
    puVar5[9] = uVar3;
    uVar3 = param_3[0xb];
    puVar5[10] = param_3[10];
    puVar5[0xb] = uVar3;
    uVar3 = param_3[0xd];
    puVar5[0xc] = param_3[0xc];
    puVar5[0xd] = uVar3;
    uVar3 = param_3[0xf];
    puVar5[0xe] = param_3[0xe];
    puVar5[0xf] = uVar3;
    uVar3 = param_3[0x11];
    puVar5[0x10] = param_3[0x10];
    puVar5[0x11] = uVar3;
    uVar3 = param_3[0x13];
    puVar5[0x12] = param_3[0x12];
    puVar5[0x13] = uVar3;
    puVar5[0x14] = param_3[0x14];
LAB_180b9f63f:
    *puVar5 = 0;
    puVar4 = *(undefined8 **)(param_1 + 0x51e8);
    if (puVar4 == (undefined8 *)0x0) {
      puVar4 = (undefined8 *)(param_1 + 0x51e0);
    }
    *puVar4 = puVar5;
    *(undefined8 **)(param_1 + 0x51e8) = puVar5;
    LOCK();
    *(int *)(param_1 + 0x5798) = *(int *)(param_1 + 0x5798) + -0x10000;
    UNLOCK();
    return puVar5;
  case 0xd:
    LOCK();
    bVar6 = *(int *)(param_1 + 0x5798) == 0;
    if (bVar6) {
      *(int *)(param_1 + 0x5798) = 0x10000;
    }
    UNLOCK();
    while (!bVar6) {
      LOCK();
      bVar6 = *(int *)(param_1 + 0x5798) == 0;
      if (bVar6) {
        *(int *)(param_1 + 0x5798) = 0x10000;
      }
      UNLOCK();
    }
    puVar5 = (undefined8 *)FUN_180ba0330(param_1,0xd,0x30);
    uVar3 = param_3[1];
    *puVar5 = *param_3;
    puVar5[1] = uVar3;
    uVar3 = param_3[3];
    puVar5[2] = param_3[2];
    puVar5[3] = uVar3;
    uVar3 = param_3[5];
    puVar5[4] = param_3[4];
    puVar5[5] = uVar3;
    break;
  case 0xe:
    FUN_180b9e420(param_1,param_3,0);
    LOCK();
    bVar6 = *(int *)(param_1 + 0x5798) == 0;
    if (bVar6) {
      *(int *)(param_1 + 0x5798) = 0x10000;
    }
    UNLOCK();
    while (!bVar6) {
      LOCK();
      bVar6 = *(int *)(param_1 + 0x5798) == 0;
      if (bVar6) {
        *(int *)(param_1 + 0x5798) = 0x10000;
      }
      UNLOCK();
    }
    puVar5 = (undefined8 *)FUN_180ba0330(param_1,0xe,0x30);
    uVar3 = param_3[1];
    *puVar5 = *param_3;
    puVar5[1] = uVar3;
    uVar3 = param_3[3];
    puVar5[2] = param_3[2];
    puVar5[3] = uVar3;
    uVar3 = param_3[5];
    puVar5[4] = param_3[4];
    puVar5[5] = uVar3;
    break;
  default:
    return (undefined8 *)0x0;
  }
  *puVar5 = 0;
  puVar4 = *(undefined8 **)(param_1 + 0x51e8);
  if (puVar4 == (undefined8 *)0x0) {
    puVar4 = (undefined8 *)(param_1 + 0x51e0);
  }
LAB_180b9f487:
  *puVar4 = puVar5;
  *(undefined8 **)(param_1 + 0x51e8) = puVar5;
  LOCK();
  *(int *)(param_1 + 0x5798) = *(int *)(param_1 + 0x5798) + -0x10000;
  UNLOCK();
  return puVar5;
}


//==== 13B89B0  FUN_1813b89b0 entry=0x13B89B0

ulonglong FUN_1813b89b0(longlong param_1,undefined8 param_2,undefined8 param_3,int param_4)

{
  float fVar1;
  undefined4 uVar3;
  undefined4 uVar4;
  ulonglong uVar5;
  undefined8 uVar6;
  float *pfVar7;
  longlong local_38;
  longlong local_30;
  char local_28;
  float fVar2;
  
  uVar5 = FUN_18157c990();
  if (param_4 != 1) goto switchD_1813b8a20_default;
  uVar6 = FUN_181587c20(uVar5 + 0x678);
  uVar5 = FUN_1812618e0(uVar6);
  if ((char)uVar5 == '\0') goto switchD_1813b8a20_default;
  uVar5 = FUN_18157c990();
  local_38 = param_1;
  switch(*(undefined4 *)(param_1 + 0x518)) {
  case 0:
    pfVar7 = (float *)FUN_18159cb00(uVar5 + 0x58,&local_38);
    fVar1 = *pfVar7;
    fVar2 = *pfVar7;
    FUN_1813be430(param_1 + 0x4c0,&local_30,param_1);
    *(undefined4 *)(param_1 + 0x490) = 0;
    if (local_30 == 0) {
      uVar5 = FUN_1813b9750(param_1,(fVar2 <= 0.0 && fVar1 != 0.0) + '\x01',0);
    }
    else {
      uVar5 = FUN_1813b9750(param_1,0x17 - (uint)(local_28 != '\0'),0);
    }
    goto switchD_1813b8a20_default;
  case 1:
  case 2:
  case 3:
  case 4:
  case 5:
  case 6:
  case 7:
    *(undefined1 *)(param_1 + 0x4fc) = 1;
    goto switchD_1813b8a20_default;
  case 8:
  case 0xf:
    if (*(char *)(param_1 + 0x4fe) == '\0') goto switchD_1813b8a20_default;
    FUN_1813be430(param_1 + 0x4c0,&local_30,param_1);
    *(undefined4 *)(param_1 + 0x490) = 0;
    if (local_30 == 0) {
      uVar3 = *(undefined4 *)(*(longlong *)(param_1 + 0x470) + 4);
      FUN_1816930a0();
      uVar4 = DAT_181c7eb28;
      FUN_1813bfff0(param_1 + 0x4c0,0,DAT_181c7eb28);
      uVar5 = FUN_1813b8680(uVar3,param_1,uVar4);
      *(undefined4 *)(param_1 + 0x518) = 2;
      *(undefined1 *)(param_1 + 0x4fd) = 1;
      goto switchD_1813b8a20_default;
    }
    break;
  case 9:
  case 0x10:
    if (*(char *)(param_1 + 0x4fe) == '\0') goto switchD_1813b8a20_default;
    FUN_1813be430(param_1 + 0x4c0,&local_30,param_1);
    *(undefined4 *)(param_1 + 0x490) = 0;
    if (local_30 == 0) {
      uVar3 = *(undefined4 *)(*(longlong *)(param_1 + 0x470) + 8);
      FUN_1816930a0(param_1);
      uVar4 = DAT_181c7eb28;
      FUN_1813bfff0(param_1 + 0x4c0,0,DAT_181c7eb28);
      uVar5 = FUN_1813b8680(uVar3,param_1,uVar4);
      *(undefined4 *)(param_1 + 0x518) = 3;
      *(undefined1 *)(param_1 + 0x4fd) = 1;
      goto switchD_1813b8a20_default;
    }
    break;
  case 10:
  case 0x11:
    if (*(char *)(param_1 + 0x4fe) == '\0') goto switchD_1813b8a20_default;
    FUN_1813be430(param_1 + 0x4c0,&local_30,param_1);
    *(undefined4 *)(param_1 + 0x490) = 0;
    if (local_30 == 0) {
      uVar3 = *(undefined4 *)(*(longlong *)(param_1 + 0x470) + 0xc);
      FUN_1816930a0(param_1);
      uVar4 = DAT_181c7eb28;
      FUN_1813bfff0(param_1 + 0x4c0,0,DAT_181c7eb28);
      uVar5 = FUN_1813b8680(uVar3,param_1,uVar4);
      *(undefined4 *)(param_1 + 0x518) = 4;
      *(undefined1 *)(param_1 + 0x4fd) = 1;
      goto switchD_1813b8a20_default;
    }
    break;
  case 0xb:
  case 0x12:
    if (*(char *)(param_1 + 0x4fe) == '\0') goto switchD_1813b8a20_default;
    FUN_1813be430(param_1 + 0x4c0,&local_30,param_1);
    *(undefined4 *)(param_1 + 0x490) = 0;
    if (local_30 == 0) {
      uVar3 = *(undefined4 *)(*(longlong *)(param_1 + 0x470) + 0x10);
      FUN_1816930a0(param_1);
      uVar4 = DAT_181c7eb28;
      FUN_1813bfff0(param_1 + 0x4c0,0,DAT_181c7eb28);
      uVar5 = FUN_1813b8680(uVar3,param_1,uVar4);
      *(undefined4 *)(param_1 + 0x518) = 5;
      *(undefined1 *)(param_1 + 0x4fd) = 1;
      goto switchD_1813b8a20_default;
    }
    break;
  case 0xc:
  case 0x13:
    if (*(char *)(param_1 + 0x4fe) == '\0') goto switchD_1813b8a20_default;
    uVar6 = 6;
    goto LAB_1813b8cf7;
  case 0xd:
  case 0x14:
    if (*(char *)(param_1 + 0x4fe) == '\0') goto switchD_1813b8a20_default;
    uVar6 = 7;
    goto LAB_1813b8cf7;
  case 0xe:
  case 0x15:
    if (*(char *)(param_1 + 0x4fe) == '\0') goto switchD_1813b8a20_default;
    uVar6 = 1;
LAB_1813b8cf7:
    uVar5 = FUN_1813b8880(&local_38,uVar6,0x180000001);
  default:
    goto switchD_1813b8a20_default;
  }
  uVar5 = FUN_1813b9750(param_1,0x17 - (uint)(local_28 != '\0'),1);
switchD_1813b8a20_default:
  return uVar5 & 0xffffffffffffff00;
}


//==== 16B0700  FUN_1816b0700 entry=0x16B0700

void FUN_1816b0700(longlong param_1)

{
  int iVar1;
  longlong lVar2;
  undefined4 *puVar3;
  undefined8 uVar4;
  undefined4 *local_res8;
  
  if (*(char *)(param_1 + 0x532) != '\0') {
    lVar2 = FUN_18157c990();
    FUN_181592fd0(lVar2 + 0xac8,0);
    lVar2 = -1;
    local_res8 = (undefined4 *)&DAT_18224df7c;
    do {
      lVar2 = lVar2 + 1;
    } while ((&DAT_181d5cc30)[lVar2] != 0);
    if (lVar2 != 0) {
      puVar3 = (undefined4 *)FUN_1800a8b30(lVar2 * 2 + 0xe,0xf,1);
      *puVar3 = 1;
      local_res8 = puVar3 + 3;
      puVar3[1] = (int)lVar2;
      puVar3[2] = (int)lVar2;
      *(undefined2 *)(lVar2 * 2 + (longlong)local_res8) = 0;
      if (local_res8 != (undefined4 *)&DAT_181d5cc30) {
        memcpy(local_res8,&DAT_181d5cc30,lVar2 * 2);
      }
    }
    uVar4 = FUN_18119a3a0();
    uVar4 = FUN_18119a460(uVar4);
    FUN_181406d70(uVar4,0,&local_res8);
    iVar1 = local_res8[-3];
    if ((-1 < iVar1) && (iVar1 = iVar1 + -1, local_res8[-3] = iVar1, iVar1 < 1)) {
      FUN_1800a8b20();
    }
    *(undefined1 *)(param_1 + 0x532) = 0;
  }
  FUN_181284d30(param_1 + 0x4c0);
  return;
}


//==== 13BD620  FUN_1813bd620 entry=0x13BD620

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

undefined8 * FUN_1813bd620(longlong param_1,undefined8 *param_2,longlong param_3,float param_4)

{
  undefined4 uVar1;
  undefined4 uVar2;
  longlong **pplVar3;
  undefined4 uVar4;
  undefined4 uVar5;
  undefined4 uVar6;
  longlong lVar7;
  uint uVar8;
  longlong *plVar9;
  longlong *plVar10;
  longlong *plVar11;
  char cVar12;
  int iVar13;
  longlong lVar14;
  undefined8 uVar15;
  longlong *plVar16;
  ulonglong uVar17;
  float *pfVar18;
  undefined8 *puVar19;
  undefined8 *puVar20;
  undefined8 *puVar21;
  undefined1 *puVar22;
  float *pfVar23;
  longlong *plVar24;
  undefined8 *puVar25;
  undefined8 *puVar26;
  ulonglong uVar27;
  float fVar28;
  float fVar29;
  float fVar30;
  float fVar31;
  float fVar32;
  float fVar33;
  float fVar34;
  float fVar35;
  float fVar36;
  float fVar37;
  float fVar38;
  float fVar39;
  float fVar40;
  undefined8 in_stack_fffffffffffffba8;
  undefined4 uVar41;
  undefined8 in_stack_fffffffffffffbb0;
  uint uVar42;
  float local_438;
  float fStack_434;
  float local_430;
  float local_428;
  float local_424;
  float local_420;
  float fStack_41c;
  float local_418;
  float local_414;
  float local_410;
  float local_40c;
  undefined1 local_408;
  float local_404;
  float local_400;
  longlong *local_3f8;
  undefined8 local_3f0;
  float local_3e8;
  undefined8 local_3e4;
  undefined4 local_3dc;
  undefined8 *local_3d8;
  undefined8 *puStack_3d0;
  longlong local_3c8;
  undefined8 *local_3c0;
  undefined8 *puStack_3b8;
  longlong lStack_3b0;
  float local_3a8;
  longlong *local_3a0;
  longlong *local_398;
  longlong *local_390;
  longlong *local_388;
  undefined8 local_378;
  undefined8 uStack_370;
  float *local_368;
  float *pfStack_360;
  undefined1 *local_358;
  char *pcStack_350;
  undefined4 local_348;
  undefined4 uStack_344;
  undefined4 uStack_340;
  undefined4 uStack_33c;
  undefined4 local_338;
  undefined4 uStack_334;
  undefined8 uStack_330;
  float local_328;
  float fStack_324;
  undefined8 uStack_320;
  float local_318;
  float local_314;
  float local_310;
  uint local_30c;
  float local_308;
  undefined4 local_304;
  float local_2f8;
  float local_2f4;
  undefined4 local_2f0;
  undefined4 local_2ec;
  float local_2e8;
  float fStack_2e4;
  undefined8 uStack_2e0;
  float local_2d8;
  float local_2d4;
  float local_2d0;
  uint local_2cc;
  float local_2c8;
  undefined4 local_2c4;
  undefined8 local_2c0;
  float local_2b8;
  undefined4 local_2b4;
  float local_2b0;
  undefined4 local_2ac;
  float local_2a8;
  float local_2a4;
  float local_2a0;
  undefined1 local_298 [8];
  longlong local_290;
  float local_274;
  float local_270;
  float local_26c;
  char local_248 [8];
  undefined1 local_240 [8];
  longlong local_238;
  undefined4 local_1e8 [6];
  float local_1d0;
  float local_1cc;
  float local_1c8;
  undefined1 local_148 [272];
  
  plVar11 = DAT_18224da38;
  plVar9 = DAT_18224d9c8;
  uVar41 = (undefined4)((ulonglong)in_stack_fffffffffffffba8 >> 0x20);
  uVar42 = (uint)((ulonglong)in_stack_fffffffffffffbb0 >> 0x20);
  lVar14 = FUN_18157c990();
  local_3d8 = (undefined8 *)0x0;
  puStack_3d0 = (undefined8 *)0x0;
  plVar24 = *(longlong **)(lVar14 + 0x38);
  local_3c8 = 0;
  local_3a0 = plVar24;
  local_398 = (longlong *)(**(code **)(*plVar24 + 0x228))(plVar24);
  puVar26 = local_3d8;
  puVar20 = puStack_3d0;
  if (local_398 != (longlong *)0x0) {
    (*(code *)**(undefined8 **)(lVar14 + 0x40))(lVar14 + 0x40,&local_420);
    local_1e8[0] = 2;
    (**(code **)(*local_398 + 0x30))(local_398,local_1e8);
    local_2f4 = (float)FUN_181695020(param_3,&DAT_182bd7190);
    local_428 = 0.0;
    local_424 = (local_1d0 * local_414 + local_1cc * local_410 + local_1c8 * local_40c) *
                *(float *)(param_1 + 0x30);
    pfVar18 = &local_424;
    if (local_424 <= 0.0) {
      pfVar18 = &local_428;
    }
    pfVar23 = (float *)(param_1 + 0x34);
    if (*pfVar18 < *(float *)(param_1 + 0x34)) {
      pfVar23 = pfVar18;
    }
    fVar28 = (float)FUN_181695020(param_3,&DAT_182bd7100);
    fVar38 = local_40c;
    fVar31 = local_410;
    fVar33 = local_414;
    uVar8 = DAT_181c7eb70;
    fVar28 = fVar28 + *pfVar23;
    fVar39 = local_414 * fVar28;
    fVar40 = local_410 * fVar28;
    fVar35 = local_420 - local_2f4;
    if (DAT_181c7f7b4 <= local_420 - local_2f4) {
      fVar35 = DAT_181c7f7b4;
    }
    fVar28 = local_40c * fVar28;
    local_3e8 = 1e+15;
    local_430 = -1e+15;
    local_3f0 = (longlong *)CONCAT44(DAT_181c7f7b4,DAT_181c7f7b4);
    local_428 = DAT_181c7f7b4;
    if (fStack_41c - local_2f4 <= DAT_181c7f7b4) {
      local_428 = fStack_41c - local_2f4;
    }
    local_3e4 = CONCAT44(DAT_181c7f7c0,DAT_181c7f7c0);
    local_3dc = 0xd8635fa9;
    local_424 = 1e+15;
    if (local_418 - local_2f4 <= 1e+15) {
      local_424 = local_418 - local_2f4;
    }
    local_318 = 0.0;
    local_308 = 0.0;
    fVar37 = local_420 + fVar39;
    fVar36 = fStack_41c + fVar40;
    local_404 = local_420 + local_2f4;
    if (local_420 + local_2f4 <= DAT_181c7f7c0) {
      local_404 = DAT_181c7f7c0;
    }
    local_3a8 = DAT_181c7f7c0;
    if (DAT_181c7f7c0 <= fStack_41c + local_2f4) {
      local_3a8 = fStack_41c + local_2f4;
    }
    fVar34 = local_418 + fVar28;
    local_400 = -1e+15;
    if (-1e+15 <= local_418 + local_2f4) {
      local_400 = local_418 + local_2f4;
    }
    fVar30 = fVar36 - local_2f4;
    fVar36 = fVar36 + local_2f4;
    fVar32 = fVar34 - local_2f4;
    fVar34 = fVar34 + local_2f4;
    fVar29 = fVar37 - local_2f4;
    fVar37 = fVar37 + local_2f4;
    if (local_3a8 <= fVar36) {
      local_3a8 = fVar36;
    }
    if (fVar29 <= fVar35) {
      fVar35 = fVar29;
    }
    if (fVar30 <= local_428) {
      local_428 = fVar30;
    }
    local_3f8 = (longlong *)CONCAT44(local_3f8._4_4_,fVar35);
    if (fVar32 <= local_424) {
      local_424 = fVar32;
    }
    if (local_404 <= fVar37) {
      local_404 = fVar37;
    }
    if (local_400 <= fVar34) {
      local_400 = fVar34;
    }
    local_328 = (float)_DAT_181c7f7d0;
    fVar35 = SQRT(local_414 * local_414 + local_410 * local_410);
    if (fVar35 <= DAT_181c7f76c) {
      uStack_320._4_4_ = (float)((ulonglong)_UNK_181c7f7d8 >> 0x20);
      uStack_320._0_4_ = (float)_UNK_181c7f7d8;
      fStack_324 = (float)((ulonglong)_DAT_181c7f7d0 >> 0x20);
      local_310 = (float)uStack_320;
      fVar37 = fStack_324;
      fVar36 = local_40c;
    }
    else {
      local_318 = local_410;
      local_328 = (DAT_181c7eb28 / fVar35) * local_410;
      uStack_320._4_4_ = (float)((uint)((DAT_181c7eb28 / fVar35) * local_414) ^ DAT_181c7eb70);
      local_310 = uStack_320._4_4_ * local_40c;
      fVar37 = local_414;
      fVar36 = local_328 * local_40c;
      local_308 = fVar35;
    }
    local_30c = (uint)fVar36 ^ DAT_181c7eb70;
    _local_328 = CONCAT44(uStack_320._4_4_,local_328);
    fVar36 = param_4 * DAT_181c8b724 + DAT_181c86464;
    local_314 = local_40c;
    uStack_320 = (ulonglong)(uint)fVar37 << 0x20;
    local_304 = 1;
    local_2f8 = local_418;
    local_2f0 = 0x3d4ccccd;
    local_2ec = 0x3d4ccccd;
    local_248[0] = '\0';
    fVar35 = cosf(fVar36);
    fVar36 = sinf(fVar36);
    fVar37 = fVar36 * (fVar35 * fVar33 - fVar36 * fVar31);
    fVar35 = (float)((uint)((fVar36 * fVar33 + fVar35 * fVar31) * fVar36) ^ uVar8);
    fVar35 = fVar35 + fVar35 + fVar33;
    fVar37 = fVar37 + fVar37 + fVar31;
    uVar15 = FUN_1814ace70(&local_438,*(undefined8 *)(param_3 + 0x40));
    uVar15 = FUN_1810d6310(uVar15,&local_3f0,"Properties");
    uVar15 = FUN_1810d6310(uVar15,&local_390,"Wrench");
    local_2a0 = (float)FUN_181083240(uVar15,"fallbackSideSwipeLength",0);
    local_2a8 = local_2a0 * fVar35;
    local_2a4 = local_2a0 * fVar37;
    local_2a0 = local_2a0 * fVar38;
    if (local_390 != (longlong *)0x0) {
      (**(code **)(*local_390 + 0x18))();
    }
    if (local_3f0 != (longlong *)0x0) {
      (**(code **)(*local_3f0 + 0x18))();
    }
    if ((longlong *)CONCAT44(fStack_434,local_438) != (longlong *)0x0) {
      (**(code **)(*(longlong *)CONCAT44(fStack_434,local_438) + 0x18))();
    }
    local_2e8 = (float)_DAT_181c7f7d0;
    local_2d8 = 0.0;
    local_2c8 = 0.0;
    fVar33 = SQRT(local_414 * local_414 + local_410 * local_410);
    if (fVar33 <= DAT_181c7f76c) {
      uStack_2e0._4_4_ = (float)((ulonglong)_UNK_181c7f7d8 >> 0x20);
      uStack_2e0._0_4_ = (float)_UNK_181c7f7d8;
      fStack_2e4 = (float)((ulonglong)_DAT_181c7f7d0 >> 0x20);
      local_2d0 = (float)uStack_2e0;
      fVar36 = fStack_2e4;
      fVar31 = local_40c;
    }
    else {
      local_2d8 = local_410;
      local_2e8 = (DAT_181c7eb28 / fVar33) * local_410;
      uStack_2e0._4_4_ = (float)((uint)((DAT_181c7eb28 / fVar33) * local_414) ^ uVar8);
      local_2d0 = uStack_2e0._4_4_ * local_40c;
      fVar36 = local_414;
      fVar31 = local_2e8 * local_40c;
      local_2c8 = fVar33;
    }
    local_2cc = (uint)fVar31 ^ uVar8;
    _local_2e8 = CONCAT44(uStack_2e0._4_4_,local_2e8);
    fVar35 = fVar35 * DAT_181c7f77c;
    uVar27 = 0;
    fVar37 = fVar37 * DAT_181c7f77c;
    local_2c4 = 1;
    fVar38 = fVar38 * DAT_181c7f77c;
    local_2b4 = 0x3d4ccccd;
    uStack_2e0 = (ulonglong)(uint)fVar36 << 0x20;
    local_2b0 = *(float *)(param_1 + 0x2c);
    local_2d4 = local_40c;
    local_2ac = 0x3d4ccccd;
    local_390 = (longlong *)0x0;
    local_2b8 = (local_2b0 * local_40c + local_418) - (fVar38 + fVar38);
    local_2c0 = CONCAT44((fStack_41c + local_2b0 * local_410) - (fVar37 + fVar37),
                         (local_2b0 * local_414 + local_420) - (fVar35 + fVar35));
    local_438 = local_404;
    fStack_434 = local_3a8;
    local_430 = local_400;
    local_3f0 = (longlong *)CONCAT44(local_428,(int)local_3f8);
    local_3e8 = local_424;
    iVar13 = (**(code **)(*DAT_18224d9c8 + 0x110))
                       (DAT_18224d9c8,&local_3f0,&local_438,&local_390,CONCAT44(uVar41,0x117),
                        (ulonglong)uVar42 << 0x20);
    local_3c0 = (undefined8 *)0x0;
    puStack_3b8 = (undefined8 *)0x0;
    lStack_3b0 = 0;
    FUN_1805ddfd0(&local_3c0,local_390,local_390 + iVar13,local_408);
    uVar17 = (ulonglong)((longlong)puStack_3b8 + (7 - (longlong)local_3c0)) >> 3;
    if (puStack_3b8 < local_3c0) {
      uVar17 = uVar27;
    }
    puVar20 = local_3c0;
    if (uVar17 != 0) {
      do {
        local_3f8 = (longlong *)*puVar20;
        local_438 = fVar39;
        fStack_434 = fVar40;
        local_430 = fVar28;
        iVar13 = (**(code **)(*plVar9 + 0x1d0))
                           (plVar9,local_3f8,0,&local_328,&local_438,local_298,0);
        if (iVar13 != 0) {
          uStack_370 = &local_3f8;
          local_368 = &local_2e8;
          local_378 = plVar9;
          pfStack_360 = &local_2a8;
          local_358 = local_298;
          pcStack_350 = local_248;
          plVar16 = (longlong *)(**(code **)(*plVar11 + 0xe0))(plVar11,local_3f8);
          plVar10 = DAT_18224d9c8;
          if (plVar16 == (longlong *)0x0) {
            FUN_1813bd480(&local_378);
          }
          else if (plVar16 != plVar24) {
            local_3e8 = local_26c - local_418;
            local_390 = local_398;
            local_388 = local_3f8;
            local_3f0 = (longlong *)CONCAT44(local_270 - fStack_41c,local_274 - local_420);
            lVar14 = *DAT_18224d9c8;
            uVar15 = FUN_18124c9f0(local_148,&local_420,&local_3f0,2,&local_390);
            iVar13 = (**(code **)(lVar14 + 0x118))(plVar10,uVar15,"RayWorldIntersection(Game)",4);
            plVar24 = local_3a0;
            if (iVar13 == 0) {
              iVar13 = (**(code **)(*local_3f8 + 8))();
              if (iVar13 == 1) {
                FUN_1813bd480(&local_378);
                plVar24 = local_3a0;
              }
              else {
                FUN_1813bd020(&local_3d8,local_298);
                plVar24 = local_3a0;
              }
            }
          }
        }
        uVar27 = uVar27 + 1;
        puVar20 = puVar20 + 1;
      } while (uVar27 != uVar17);
    }
    puVar19 = local_3d8;
    puVar21 = puStack_3d0;
    puVar20 = puStack_3d0;
    puVar26 = local_3d8;
    if (local_3c0 != (undefined8 *)0x0) {
      uVar17 = lStack_3b0 - (longlong)local_3c0 >> 3;
      if ((0x1fffffffffffffff < uVar17) ||
         ((puVar20 = local_3c0, 0xfff < uVar17 * 8 &&
          (((((ulonglong)local_3c0 & 0x1f) != 0 ||
            (puVar20 = (undefined8 *)local_3c0[-1], local_3c0 <= puVar20)) ||
           (0x1f < (ulonglong)((longlong)local_3c0 + (-8 - (longlong)puVar20)))))))) {
                    /* WARNING: Subroutine does not return */
        _invalid_parameter_noinfo_noreturn();
      }
      free(puVar20);
      puVar19 = local_3d8;
      puVar21 = puStack_3d0;
      puVar20 = puStack_3d0;
      puVar26 = local_3d8;
    }
    for (; puVar19 != puVar21; puVar19 = puVar19 + 10) {
      while( true ) {
        uVar15 = (**(code **)(*plVar11 + 0xe0))(plVar11,puVar19[1]);
        cVar12 = FUN_18147d2f0(uVar15);
        if (cVar12 == '\0') break;
        puVar19 = puVar19 + 10;
        if (puVar19 == puVar21) goto LAB_1813be009;
      }
      do {
        puVar25 = puVar21;
        puVar21 = puVar25 + -10;
        if (puVar19 == puVar21) goto LAB_1813be009;
        uVar15 = (**(code **)(*plVar11 + 0xe0))(plVar11,puVar25[-9]);
        cVar12 = FUN_18147d2f0(uVar15);
      } while (cVar12 == '\0');
      pplVar3 = (longlong **)puVar19[1];
      uVar41 = *(undefined4 *)puVar19;
      uVar1 = *(undefined4 *)((longlong)puVar19 + 0x24);
      lVar14 = puVar19[5];
      uVar2 = *(undefined4 *)((longlong)puVar19 + 0x2c);
      pcStack_350 = (char *)puVar19[5];
      local_348 = *(undefined4 *)(puVar19 + 6);
      uStack_344 = *(undefined4 *)((longlong)puVar19 + 0x34);
      uStack_340 = *(undefined4 *)(puVar19 + 7);
      uVar4 = *(undefined4 *)(puVar19 + 6);
      uVar5 = *(undefined4 *)((longlong)puVar19 + 0x34);
      uVar6 = *(undefined4 *)(puVar19 + 7);
      uVar15 = puVar25[-9];
      local_368 = (float *)puVar19[2];
      pfStack_360 = (float *)puVar19[3];
      lVar7 = puVar19[4];
      local_358 = (undefined1 *)puVar19[4];
      uStack_33c = *(undefined4 *)((longlong)puVar19 + 0x3c);
      local_338 = *(undefined4 *)(puVar19 + 8);
      uStack_330 = puVar19[9];
      *puVar19 = *puVar21;
      puVar19[1] = uVar15;
      uVar15 = puVar25[-7];
      puVar19[2] = puVar25[-8];
      puVar19[3] = uVar15;
      uVar15 = puVar25[-5];
      puVar19[4] = puVar25[-6];
      puVar19[5] = uVar15;
      uVar15 = puVar25[-3];
      puVar19[6] = puVar25[-4];
      puVar19[7] = uVar15;
      uVar15 = puVar25[-1];
      puVar19[8] = puVar25[-2];
      puVar19[9] = uVar15;
      uStack_370._0_4_ = SUB84(pplVar3,0);
      uStack_370._4_4_ = (undefined4)((ulonglong)pplVar3 >> 0x20);
      *(undefined4 *)puVar21 = uVar41;
      *(undefined4 *)((longlong)puVar25 + -0x4c) = local_378._4_4_;
      *(undefined4 *)(puVar25 + -9) = (undefined4)uStack_370;
      *(undefined4 *)((longlong)puVar25 + -0x44) = uStack_370._4_4_;
      local_378 = (longlong *)CONCAT44(local_378._4_4_,uVar41);
      puVar25[-8] = local_368;
      puVar25[-7] = pfStack_360;
      *(int *)(puVar25 + -6) = (int)lVar7;
      *(undefined4 *)((longlong)puVar25 + -0x2c) = uVar1;
      *(int *)(puVar25 + -5) = (int)lVar14;
      *(undefined4 *)((longlong)puVar25 + -0x24) = uVar2;
      *(undefined4 *)(puVar25 + -4) = uVar4;
      *(undefined4 *)((longlong)puVar25 + -0x1c) = uVar5;
      *(undefined4 *)(puVar25 + -3) = uVar6;
      *(undefined4 *)((longlong)puVar25 + -0x14) = uStack_33c;
      puVar25[-2] = CONCAT44(uStack_334,local_338);
      puVar25[-1] = uStack_330;
      uStack_370 = pplVar3;
    }
LAB_1813be009:
    plVar24 = DAT_18224d9c8;
    if (puVar19 != puVar26) {
      fVar33 = *(float *)(param_1 + 0x24);
      puVar20 = puVar26 + 10;
      local_420 = fVar33 * local_414 + local_420;
      fStack_41c = fVar33 * local_410 + fStack_41c;
      local_418 = fVar33 * local_40c + local_418;
      if (puVar20 != puVar19) {
        pfVar18 = (float *)((longlong)puVar26 + 0x7c);
        puVar21 = puVar26;
        do {
          fVar39 = pfVar18[-2] - local_420;
          fVar33 = pfVar18[-1] - fStack_41c;
          fVar28 = *(float *)((longlong)puVar21 + 0x24) - local_420;
          fVar31 = *(float *)(puVar21 + 5) - fStack_41c;
          fVar38 = *pfVar18 - local_418;
          fVar35 = *(float *)((longlong)puVar21 + 0x2c) - local_418;
          if (fVar39 * fVar39 + fVar33 * fVar33 + fVar38 * fVar38 <
              fVar28 * fVar28 + fVar31 * fVar31 + fVar35 * fVar35) {
            puVar21 = puVar20;
          }
          puVar20 = puVar20 + 10;
          pfVar18 = pfVar18 + 0x14;
        } while (puVar20 != puVar19);
      }
      local_3c0 = (undefined8 *)0x0;
      puStack_3b8 = (undefined8 *)0x0;
      lStack_3b0 = 0;
      FUN_1813bd020(&local_3c0);
      local_248[0] = '\0';
      *param_2 = local_3c0;
      param_2[1] = puStack_3b8;
      param_2[2] = lStack_3b0;
      if (puVar26 == (undefined8 *)0x0) {
        return param_2;
      }
      uVar17 = (local_3c8 - (longlong)puVar26) / 0x50;
      if ((uVar17 < 0x333333333333334) &&
         ((puVar20 = puVar26, uVar17 * 0x50 < 0x1000 ||
          (((((ulonglong)puVar26 & 0x1f) == 0 &&
            (puVar20 = (undefined8 *)puVar26[-1], puVar20 < puVar26)) &&
           ((ulonglong)((longlong)puVar26 + (-8 - (longlong)puVar20)) < 0x20)))))) {
        free(puVar20);
        return param_2;
      }
                    /* WARNING: Subroutine does not return */
      _invalid_parameter_noinfo_noreturn();
    }
    local_430 = *(float *)(param_1 + 0x2c);
    local_438 = local_430 * local_414;
    fStack_434 = local_430 * local_410;
    local_430 = local_430 * local_40c;
    lVar14 = *DAT_18224d9c8;
    uVar15 = FUN_18124ba90(local_148,&local_420,&local_438,local_298,1,&local_398);
    iVar13 = (**(code **)(lVar14 + 0x118))(plVar24,uVar15,"RayWorldIntersection(Game)",4);
    if (iVar13 == 0) {
      puVar19 = puVar26;
      if (local_248[0] == '\0') goto LAB_1813be3f2;
      for (; puVar19 != puVar20; puVar19 = puVar19 + 10) {
        if (puVar19[1] == local_238) {
          if (puVar19 != puVar20) goto LAB_1813be3f2;
          break;
        }
      }
      puVar22 = local_240;
    }
    else {
      puVar19 = puVar26;
      if (puVar26 == puVar20) {
LAB_1813be3a3:
        puVar22 = local_298;
      }
      else {
        do {
          if (puVar19[1] == local_290) {
            if (puVar19 == puVar20) goto LAB_1813be3a3;
            goto LAB_1813be3f2;
          }
          puVar19 = puVar19 + 10;
        } while (puVar19 != puVar20);
        puVar22 = local_298;
      }
    }
    FUN_1813bd250(&local_3d8,puVar22);
    puVar26 = local_3d8;
    puVar20 = puStack_3d0;
  }
LAB_1813be3f2:
  param_2[2] = local_3c8;
  *param_2 = puVar26;
  param_2[1] = puVar20;
  return param_2;
}


//==== 13C0160  FUN_1813c0160 entry=0x13C0160

void FUN_1813c0160(longlong param_1,longlong param_2,undefined4 param_3)

{
  undefined1 uVar1;
  longlong lVar2;
  
  lVar2 = FUN_1812dc2e0(*(undefined4 *)(param_2 + 0x490));
  if (lVar2 != 0) {
    uVar1 = thunk_FUN_181531c10(lVar2,param_3);
    *(undefined1 *)(param_1 + 0x3f) = uVar1;
  }
  return;
}


//==== 13BD480  FUN_1813bd480 entry=0x13BD480

void FUN_1813bd480(undefined8 *param_1)

{
  undefined4 *puVar1;
  char *pcVar2;
  undefined8 *puVar3;
  undefined8 uVar4;
  int iVar5;
  undefined4 local_18;
  undefined4 local_14;
  undefined4 local_10;
  
  puVar1 = (undefined4 *)param_1[3];
  local_18 = *puVar1;
  local_14 = puVar1[1];
  local_10 = puVar1[2];
  iVar5 = (**(code **)(*(longlong *)*param_1 + 0x1d0))
                    ((longlong *)*param_1,*(undefined8 *)param_1[1],0,param_1[2],&local_18,
                     param_1[4],0);
  if (iVar5 != 0) {
    pcVar2 = (char *)param_1[5];
    puVar3 = (undefined8 *)param_1[4];
    if (*pcVar2 != '\0') {
      uVar4 = puVar3[1];
      *(undefined8 *)(pcVar2 + 8) = *puVar3;
      *(undefined8 *)(pcVar2 + 0x10) = uVar4;
      uVar4 = puVar3[3];
      *(undefined8 *)(pcVar2 + 0x18) = puVar3[2];
      *(undefined8 *)(pcVar2 + 0x20) = uVar4;
      uVar4 = puVar3[5];
      *(undefined8 *)(pcVar2 + 0x28) = puVar3[4];
      *(undefined8 *)(pcVar2 + 0x30) = uVar4;
      uVar4 = puVar3[7];
      *(undefined8 *)(pcVar2 + 0x38) = puVar3[6];
      *(undefined8 *)(pcVar2 + 0x40) = uVar4;
      uVar4 = puVar3[9];
      *(undefined8 *)(pcVar2 + 0x48) = puVar3[8];
      *(undefined8 *)(pcVar2 + 0x50) = uVar4;
      return;
    }
    *(undefined4 *)(pcVar2 + 8) = *(undefined4 *)puVar3;
    *(undefined8 *)(pcVar2 + 0x10) = puVar3[1];
    *(undefined4 *)(pcVar2 + 0x18) = *(undefined4 *)(puVar3 + 2);
    *(undefined4 *)(pcVar2 + 0x1c) = *(undefined4 *)((longlong)puVar3 + 0x14);
    *(undefined2 *)(pcVar2 + 0x20) = *(undefined2 *)(puVar3 + 3);
    *(undefined2 *)(pcVar2 + 0x22) = *(undefined2 *)((longlong)puVar3 + 0x1a);
    *(undefined4 *)(pcVar2 + 0x24) = *(undefined4 *)((longlong)puVar3 + 0x1c);
    *(undefined4 *)(pcVar2 + 0x28) = *(undefined4 *)(puVar3 + 4);
    *(undefined4 *)(pcVar2 + 0x2c) = *(undefined4 *)((longlong)puVar3 + 0x24);
    *(undefined4 *)(pcVar2 + 0x30) = *(undefined4 *)(puVar3 + 5);
    *(undefined4 *)(pcVar2 + 0x34) = *(undefined4 *)((longlong)puVar3 + 0x2c);
    *(undefined4 *)(pcVar2 + 0x38) = *(undefined4 *)(puVar3 + 6);
    *(undefined4 *)(pcVar2 + 0x3c) = *(undefined4 *)((longlong)puVar3 + 0x34);
    *(undefined4 *)(pcVar2 + 0x40) = *(undefined4 *)(puVar3 + 7);
    *(undefined4 *)(pcVar2 + 0x44) = *(undefined4 *)((longlong)puVar3 + 0x3c);
    *(undefined4 *)(pcVar2 + 0x48) = *(undefined4 *)(puVar3 + 8);
    *(undefined8 *)(pcVar2 + 0x50) = puVar3[9];
    *pcVar2 = '\x01';
  }
  return;
}


//==== 13BD020  FUN_1813bd020 entry=0x13BD020

void FUN_1813bd020(longlong *param_1,undefined4 *param_2)

{
  ulonglong uVar1;
  undefined4 *puVar2;
  ulonglong uVar3;
  longlong lVar4;
  longlong lVar5;
  ulonglong uVar6;
  undefined1 local_res18;
  
  puVar2 = (undefined4 *)param_1[1];
  if ((undefined4 *)param_1[2] == puVar2) {
    lVar4 = ((longlong)puVar2 - *param_1) / 0x50;
    if (lVar4 == 0x333333333333333) {
                    /* WARNING: Subroutine does not return */
      FUN_1802a3770();
    }
    uVar1 = lVar4 + 1;
    uVar3 = (param_1[2] - *param_1) / 0x50;
    uVar6 = uVar1;
    if ((uVar3 <= 0x333333333333333 - (uVar3 >> 1)) && (uVar6 = (uVar3 >> 1) + uVar3, uVar6 < uVar1)
       ) {
      uVar6 = uVar1;
    }
    lVar5 = FUN_180892550(param_1,uVar6);
    *(undefined4 *)(lVar5 + lVar4 * 0x50) = *param_2;
    *(undefined8 *)(lVar5 + 8 + lVar4 * 0x50) = *(undefined8 *)(param_2 + 2);
    *(undefined4 *)(lVar5 + 0x10 + lVar4 * 0x50) = param_2[4];
    *(undefined4 *)(lVar5 + 0x14 + lVar4 * 0x50) = param_2[5];
    *(undefined2 *)(lVar5 + 0x18 + lVar4 * 0x50) = *(undefined2 *)(param_2 + 6);
    *(undefined2 *)(lVar5 + 0x1a + lVar4 * 0x50) = *(undefined2 *)((longlong)param_2 + 0x1a);
    *(undefined4 *)(lVar5 + 0x1c + lVar4 * 0x50) = param_2[7];
    *(undefined4 *)(lVar5 + 0x20 + lVar4 * 0x50) = param_2[8];
    *(undefined4 *)(lVar5 + 0x24 + lVar4 * 0x50) = param_2[9];
    *(undefined4 *)(lVar5 + 0x28 + lVar4 * 0x50) = param_2[10];
    *(undefined4 *)(lVar5 + 0x2c + lVar4 * 0x50) = param_2[0xb];
    *(undefined4 *)(lVar5 + 0x30 + lVar4 * 0x50) = param_2[0xc];
    *(undefined4 *)(lVar5 + 0x34 + lVar4 * 0x50) = param_2[0xd];
    *(undefined4 *)(lVar5 + 0x38 + lVar4 * 0x50) = param_2[0xe];
    *(undefined4 *)(lVar5 + 0x3c + lVar4 * 0x50) = param_2[0xf];
    *(undefined4 *)(lVar5 + 0x40 + lVar4 * 0x50) = param_2[0x10];
    *(undefined8 *)(lVar5 + 0x48 + lVar4 * 0x50) = *(undefined8 *)(param_2 + 0x12);
    FUN_1813bcf60(*param_1,param_1[1],lVar5,param_1,0,local_res18,1,lVar5);
    FUN_1813c01a0(param_1,lVar5,uVar1,uVar6);
  }
  else {
    *puVar2 = *param_2;
    *(undefined8 *)(puVar2 + 2) = *(undefined8 *)(param_2 + 2);
    puVar2[4] = param_2[4];
    puVar2[5] = param_2[5];
    *(undefined2 *)(puVar2 + 6) = *(undefined2 *)(param_2 + 6);
    *(undefined2 *)((longlong)puVar2 + 0x1a) = *(undefined2 *)((longlong)param_2 + 0x1a);
    puVar2[7] = param_2[7];
    puVar2[8] = param_2[8];
    puVar2[9] = param_2[9];
    puVar2[10] = param_2[10];
    puVar2[0xb] = param_2[0xb];
    puVar2[0xc] = param_2[0xc];
    puVar2[0xd] = param_2[0xd];
    puVar2[0xe] = param_2[0xe];
    puVar2[0xf] = param_2[0xf];
    puVar2[0x10] = param_2[0x10];
    *(undefined8 *)(puVar2 + 0x12) = *(undefined8 *)(param_2 + 0x12);
    param_1[1] = param_1[1] + 0x50;
  }
  return;
}


//==== 16B0BF0  FUN_1816b0bf0 entry=0x16B0BF0

void FUN_1816b0bf0(undefined8 param_1)

{
  longlong lVar1;
  undefined8 uVar2;
  
  FUN_181210d60();
  lVar1 = FUN_18157c9c0();
  if ((lVar1 != 0) && (lVar1 = FUN_181274c80(lVar1 + 0x14b8), lVar1 != 0)) {
    uVar2 = FUN_181587c10(param_1);
    FUN_1816b2590(lVar1);
    FUN_1813bede0(lVar1 + 0x4c8,uVar2,*(undefined4 *)(lVar1 + 0x38));
  }
  return;
}


//==== 16B2B80  FUN_1816b2b80 entry=0x16B2B80

void FUN_1816b2b80(longlong param_1,undefined8 param_2)

{
  FUN_1816b2590();
  FUN_1813bede0(param_1 + 0x4c8,param_2,*(undefined4 *)(param_1 + 0x38));
  return;
}


