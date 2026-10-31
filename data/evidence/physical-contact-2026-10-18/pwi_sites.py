import struct
from re_disasm import img, function_of, window, dis
text = img.section(".text"); tb = img.at(text.rva, text.raw_size)
def lea_refs(target):
    out=[]
    for i in range(len(tb)-7):
        if tb[i] in (0x48,0x4C) and tb[i+1]==0x8D and (tb[i+2]&0xC7)==5:
            if text.rva+i+7+struct.unpack_from("<i",tb,i+3)[0]==target: out.append(text.rva+i)
    return out
err = lea_refs(0x1dfeea0)
game = lea_refs(0x1df1630)
print("error-string refs in functions:", sorted({function_of(r)[0] for r in err if function_of(r)}))
print("game-tag refs in functions:", len(game), sorted({hex(function_of(r)[0]) for r in game if function_of(r)})[:60])
