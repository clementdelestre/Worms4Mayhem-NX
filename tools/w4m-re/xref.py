#!/usr/bin/env python3
"""Cross-references to an address or string in WormsMayhem.exe.

usage: xref.py [-r] [--callers] TARGET...
       xref.py --field 0xOFF [--in FUNC_VA...]   instructions using [reg+0xOFF] (struct field), optionally only in those functions
  TARGET  hex VA (0x...) or exact string; -r: TARGET is a regex over exe strings
Prints code refs `site  func_start  insn`, then data refs (pointers stored in .rdata/.data).
"""
import argparse, re
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from pe import PE
from scan import index, func_start

md = Cs(CS_ARCH_X86, CS_MODE_32)


def insn(p, va):
    o = p.v2f(va)
    for i in md.disasm(p.b[o:o + 16], va, 1): return '%s %s' % (i.mnemonic, i.op_str)
    return '?'


def targets(p, t, regex):
    if regex:
        r = re.compile(t)
        return [(va, s) for va, s in p.strings(3) if r.search(s)]
    if re.fullmatch(r'0x[0-9a-fA-F]+', t): return [(int(t, 16), p.cstr(int(t, 16)))]
    return [(va, t) for va in p.find_str(t)]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('target', nargs='*')
    ap.add_argument('--field', type=lambda x: int(x, 16), help='struct offset to search as [reg+OFF]')
    ap.add_argument('--in', dest='funcs', nargs='+', type=lambda x: int(x, 16), default=[], help='with --field: only these functions')
    ap.add_argument('-r', action='store_true', help='targets are regexes over exe strings')
    ap.add_argument('--callers', action='store_true', help='also print callers of each referencing function')
    a = ap.parse_args()
    p, idx = PE(), index()
    if a.field is not None:  # ponytail: full linear sweep (~10 s), index displacements in scan.py if used often
        pat = re.compile(r'\+ 0x%x\]' % a.field)
        _, tva, _, tra, trs = p.sec('.text')
        md.skipdata = True
        for va, size, mn, ops in md.disasm_lite(p.b[tra:tra + trs], tva):
            if pat.search(ops) and (fs := func_start(va, idx)) and (not a.funcs or fs in a.funcs):
                print('  %08x  %08x  %s %s' % (va, fs, mn, ops))
    for t in a.target:
        tl = targets(p, t, a.r)
        if not tl: print('%s: not found%s' % (t, '' if a.r else ' (exact string or 0xVA; regex needs -r)'))
        for va, s in tl:
            print('== %08x %s%s' % (va, p.section_of(va), ' %r' % s if s else ''))
            for site in sorted(set(idx['refs'].get(va, []) + idx['calls'].get(va, []))):
                fs = func_start(site, idx)
                print('  %08x  %08x  %s' % (site, fs or 0, insn(p, site)))
                if a.callers and fs:
                    for c in sorted(set(idx['calls'].get(fs, []))):
                        print('      <- %08x  %08x  %s' % (c, func_start(c, idx) or 0, insn(p, c)))
            for d in p._ptr_index().get(va, []):
                print('  %08x  data %s' % (d, p.section_of(d)))


if __name__ == '__main__':
    main()
