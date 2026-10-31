from re_disasm import img, dis
for t in (0xbb2a10, 0xba9810):
    print(f"=== {t:#x}")
    lines = dis(t, 200).splitlines()
    rets = 0
    for line in lines:
        print(line)
        if " ret" in line:
            rets += 1
            if rets == 2: break
