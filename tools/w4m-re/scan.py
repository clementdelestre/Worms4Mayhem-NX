#!/usr/bin/env python3
"""Linear-sweep index of WormsMayhem.exe .text (capstone), cached in $W4M_CACHE/scan.pkl.

  calls:  {target: [call sites]}        (call/jmp rel32 and jcc to a VA)
  refs:   {imm32/disp32 VA: [insn VAs]}  (push/mov/lea/cmp... of an address in the image)
  starts: sorted function starts (call targets, vtable slots, code after int3 padding)
Run `scan.py` once to build it (about a minute); xref.py and disasm.py load it.
"""
import bisect, os, pickle, re, sys
from pe import PE, CACHE

HEX = re.compile(r'0x([0-9a-f]{6,8})\b')


def build(p):
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.skipdata = True
    _, tva, tvs, tra, trs = p.sec('.text')
    code = p.b[tra:tra + trs]
    lo, hi = p.base, p.secs[-1][1] + p.secs[-1][2]
    calls, refs, starts, prev = {}, {}, set(), None
    for a, size, mn, ops in md.disasm_lite(code, tva):
        if mn in ('int3', '.byte'):
            prev = mn; continue
        if prev == 'int3' and a % 4 == 0: starts.add(a)
        if mn == 'push' and ops == 'ebp' and code[a - tva + 1:a - tva + 3] == b'\x8b\xec': starts.add(a)
        prev = mn
        for m in HEX.finditer(ops):
            v = int(m.group(1), 16)
            if not lo <= v < hi: continue
            if mn == 'call' or (mn.startswith('j') and ops.startswith('0x')):
                calls.setdefault(v, []).append(a)
                if mn == 'call': starts.add(v)
            else:
                refs.setdefault(v, []).append(a)
    starts |= set(p.vfuncs())
    return {'calls': calls, 'refs': refs, 'starts': sorted(s for s in starts if tva <= s < tva + tvs)}


_idx = None
def index(p=None):
    global _idx
    if _idx is not None: return _idx
    p = p or PE()
    f = os.path.join(CACHE, 'scan.pkl'); st = os.stat(p.path)
    if os.path.exists(f):
        key, d = pickle.load(open(f, 'rb'))
        if key == (st.st_size, st.st_mtime): _idx = d; return d
    sys.stderr.write('building %s (once)...\n' % f)
    d = build(p)
    os.makedirs(CACHE, exist_ok=True)
    pickle.dump(((st.st_size, st.st_mtime), d), open(f, 'wb'))
    _idx = d
    return d


def func_start(va, idx=None):
    s = (idx or index())['starts']
    i = bisect.bisect_right(s, va) - 1
    return s[i] if i >= 0 else None


if __name__ == '__main__':
    d = index()
    print('%d call targets, %d referenced addresses, %d function starts' % (len(d['calls']), len(d['refs']), len(d['starts'])))
