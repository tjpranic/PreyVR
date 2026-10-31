"""Summarise a cdb ~*kn dump: PreyDll frames as RVAs, idle threads folded."""
import re
import sys

path = sys.argv[1]
# Export-relative names cdb printed, as RVAs (from frames whose VA is known).
BASES = {'AK::MemoryMgr::GetPoolName': 0x597420, 'AK::Monitor::SetLocalOutput': 0x335000}
frame = re.compile(r'^\s*[0-9a-f]{2} [0-9a-f`]{17} ([0-9a-f`]{17})\s+(\S+)')
head = re.compile(r'^\s*[#.]?\s*(\d+)\s+Id: ([0-9a-f]+)\.([0-9a-f]+)')
threads = []
for line in open(path, encoding='utf-8', errors='replace'):
    m = head.match(line)
    if m:
        threads.append((int(m.group(1)), m.group(3), []))
        continue
    m = frame.match(line)
    if m and threads:
        sym = m.group(2)
        mm = re.match(r'PreyDll!(.+)\+0x([0-9a-f]+)$', sym)
        if mm and mm.group(1) in BASES:
            sym = 'PreyDll+0x%X' % (BASES[mm.group(1)] + int(mm.group(2), 16))
        threads[-1][2].append(sym)

IDLE = ('NtWaitForSingleObject', 'ZwWaitForMultipleObjects', 'NtDelayExecution', 'NtRemoveIoCompletion',
        'ZwWaitForAlertByThreadId', 'NtWaitForAlertByThreadId', 'ZwRemoveIoCompletionEx', 'NtWaitForWorkViaWorkerFactory')
idle = {}
for n, tid, frames in threads:
    interesting = any(s.startswith(('PreyVR', 'd3d11', 'openxr', 'vrclient', 'nvwgf', 'amdxx', 'dxgi')) for s in frames)
    top = frames[0] if frames else ''
    sig = ' > '.join(frames[2:5])
    if not interesting and any(k in top for k in IDLE):
        idle.setdefault(sig, []).append(n)
        continue
    print('--- thread %d (%s)' % (n, tid))
    for s in frames[:18]:
        print('   ' + s)
print('\n=== idle threads grouped by the frames under the wait')
for sig, ns in sorted(idle.items(), key=lambda kv: -len(kv[1])):
    print('%3d  %s  (threads %s)' % (len(ns), sig, ','.join(map(str, ns[:12]))))
