#!/usr/bin/env python3
"""Annotated capstone disassembly of WormsMayhem.exe.

usage: disasm.py VA [--before N] [--after N]
  default: the whole function containing VA; --before/--after: N instructions around VA instead.
Annotates message handles (msg Name), XOM class descriptors (class Name), strings, .rdata f32/f64 constants, call targets (<fn 0x...> + assert .cpp name). '>' marks VA.
"""
import argparse, bisect, re
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from pe import PE
from scan import index, func_start

HEX = re.compile(r'(?:(dword|qword) ptr \[)?0x([0-9a-f]+)\b')


def cpp_names(p, idx):
    """{function start: assert .cpp filename it references}."""
    out = {}
    for va, sites in idx['refs'].items():
        if p.section_of(va) != '.rdata': continue
        s = p.cstr(va)
        if s and s.lower().endswith('.cpp'):
            for site in sites: out.setdefault(func_start(site, idx), s.replace('\\', '/').rsplit('/', 1)[-1])
    return out


def note(p, i, cpp):
    out = []
    for m in HEX.finditer(i.op_str):
        v = int(m.group(2), 16)
        if not p.section_of(v): continue
        if i.mnemonic == 'call' or i.mnemonic.startswith('j') and i.op_str.startswith('0x'):
            if i.mnemonic == 'call': out.append('<fn 0x%x>%s' % (v, ' ' + cpp[v] if v in cpp else ''))
        elif m.group(1) and p.section_of(v) == '.rdata':
            out.append('%g' % (p.f32(v) if m.group(1) == 'dword' else p.f64(v)))
        elif v in p.msgnames():
            out.append('msg ' + p.msgnames()[v])
        elif (s := p.cstr(v)) is not None:
            out.append(repr(s))
        elif p.section_of(v) == '.rdata' and p.v2f(v + 0x14) and (s := p.cstr(p.u32(v + 0x10))) and re.fullmatch(r'\w+', s):
            out.append('class ' + s)  # XOM class descriptor: 16-byte GUID, then name pointer
    return '  ; ' + ' '.join(out) if out else ''


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('va', type=lambda x: int(x, 16))
    ap.add_argument('--func', action='store_true', help='whole function (default)')
    ap.add_argument('--before', type=int)
    ap.add_argument('--after', type=int)
    a = ap.parse_args()
    p, idx = PE(), index()
    fs = func_start(a.va, idx)
    s = idx['starts']
    end = s[bisect.bisect_right(s, a.va)] if bisect.bisect_right(s, a.va) < len(s) else p.sec('.text')[1] + p.sec('.text')[2]
    window = a.before is not None or a.after is not None
    o = p.v2f(fs)
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    ins, after = [], None
    for i in md.disasm(p.b[o:o + 0x40000], fs):
        ins.append(i)
        if window:
            if after is None and i.address + i.size > a.va: after = len(ins) - 1
            if after is not None and len(ins) > after + (a.after or 0) or i.address + i.size >= end: break
        elif i.address + i.size >= end or i.mnemonic == 'ret' and p.b[p.v2f(i.address + i.size)] == 0xcc and i.address >= a.va:
            break
    if window: ins = ins[max(0, (after or 0) - (a.before or 0)):]
    cpp = cpp_names(p, idx)
    print('; function %08x' % fs)
    for i in ins:
        mark = '>' if i.address <= a.va < i.address + i.size else ' '
        print('%s%08x  %-8s %s%s' % (mark, i.address, i.mnemonic, i.op_str, note(p, i, cpp)))


if __name__ == '__main__':
    main()
