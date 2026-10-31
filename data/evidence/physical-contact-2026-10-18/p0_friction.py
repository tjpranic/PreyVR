import struct, re
from re_disasm import img, function_of, window
text = img.section(".text"); tb = img.at(text.rva, text.raw_size)
def strings(pat):
    out=[]
    for s in img.sections:
        if s.name not in (".rdata",".data"): continue
        blob=img.at(s.rva,s.raw_size)
        for m in re.finditer(pat,blob):
            a=blob.rfind(b"\0",0,m.start())+1; e=blob.find(b"\0",m.end())
            out.append((s.rva+a,blob[a:e].decode("latin1")))
    return sorted(set(out))
leas={}
for i in range(len(tb)-7):
    if tb[i] in (0x48,0x4C) and tb[i+1]==0x8D and (tb[i+2]&0xC7)==5:
        t=text.rva+i+7+struct.unpack_from("<i",tb,i+3)[0]; leas.setdefault(t,[]).append(text.rva+i)
for rva,s in strings(rb"[Ff]riction|[Bb]ounc[iy]ness|SurfaceParam"):
    refs=leas.get(rva,[])
    print(hex(rva), len(refs), repr(s[:70]), [hex(function_of(r)[0]) for r in refs[:4] if function_of(r)])
