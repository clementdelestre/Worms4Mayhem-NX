#!/usr/bin/env python3
"""Lua 5.0 bytecode (.lub) lister: functions, constants and pseudo-code.

usage: lua.py FILE.lub [--code] [REGEX]   functions (name, line, params) and constants; REGEX filters
       lua.py --all [REGEX]               `file: function: line` matches over every Data/scripts/*.lub
FILE may be relative to Data/scripts. Header: 4-byte int/size_t/instruction, f32 numbers.
"""
import argparse, glob, os, re, struct
from pe import GAME

SCRIPTS = os.path.join(GAME, 'Data', 'scripts')
OPS = ('MOVE LOADK LOADBOOL LOADNIL GETUPVAL GETGLOBAL GETTABLE SETGLOBAL SETUPVAL SETTABLE NEWTABLE SELF '
       'ADD SUB MUL DIV POW UNM NOT CONCAT JMP EQ LT LE TEST CALL TAILCALL RETURN FORLOOP TFORLOOP TFORPREP '
       'SETLIST SETLISTO CLOSE CLOSURE').split()
ARITH = dict(zip(('ADD', 'SUB', 'MUL', 'DIV', 'POW'), '+-*/^'))
RK = 250  # MAXSTACK: B/C >= RK index constants


class Reader:
    def __init__(self, b):
        if b[:5] != b'\x1bLuaP': raise ValueError('not a Lua 5.0 chunk')
        self.b, self.p = b, 18

    def take(self, n):
        self.p += n
        return self.b[self.p - n:self.p]

    def int(self): return struct.unpack('<i', self.take(4))[0]

    def str(self):
        n = self.int()
        return self.take(n)[:-1].decode('latin1') if n else None

    def proto(self):
        f = dict(src=self.str(), line=self.int())
        f['nups'], f['npar'], f['vararg'], _ = self.take(4)
        f['lines'] = [self.int() for _ in range(self.int())]
        f['locs'] = []
        for _ in range(self.int()):
            f['locs'].append(self.str()); self.take(8)
        f['ups'] = [self.str() for _ in range(self.int())]
        f['k'] = []
        for _ in range(self.int()):
            t = self.take(1)[0]
            f['k'].append(struct.unpack('<f', self.take(4))[0] if t == 3 else self.str() if t == 4 else None)
        f['ps'] = [self.proto() for _ in range(self.int())]
        n = self.int()
        f['code'] = struct.unpack('<%dI' % n, self.take(4 * n))
        return f


def kstr(v):
    return '"%s"' % v if isinstance(v, str) else '%g' % v if isinstance(v, float) else 'nil'


def walk(f, name, out):
    """Append (name, proto, pseudo-code lines) for f and its nested functions to out."""
    k, reg, lines = f['k'], {}, []
    r = lambda x: reg.get(x, 'r%d' % x)
    rk = lambda x: kstr(k[x - RK]) if x >= RK else r(x)
    for pc, i in enumerate(f['code']):
        op, a, b, c, bx = i & 63, i >> 24, (i >> 15) & 511, (i >> 6) & 511, (i >> 6) & 0x3ffff
        o = OPS[op] if op < len(OPS) else '?'
        if o == 'MOVE': reg[a] = r(b)
        elif o == 'LOADK': reg[a] = kstr(k[bx])
        elif o == 'LOADBOOL': reg[a] = 'true' if b else 'false'
        elif o == 'LOADNIL': reg.update((x, 'nil') for x in range(a, b + 1))
        elif o == 'GETUPVAL': reg[a] = 'up%d' % b
        elif o == 'GETGLOBAL': reg[a] = k[bx]
        elif o == 'GETTABLE':
            key = rk(c)
            reg[a] = r(b) + '.' + key[1:-1] if key.startswith('"') else '%s[%s]' % (r(b), key)
        elif o == 'SETGLOBAL': lines.append('%s = %s' % (k[bx], r(a)))
        elif o == 'SETTABLE': lines.append('%s[%s] = %s' % (r(a), rk(b), rk(c)))
        elif o == 'NEWTABLE': reg[a] = '{}'
        elif o == 'SELF': reg[a + 1], reg[a] = r(b), r(b) + ':' + rk(c).strip('"')
        elif o in ARITH: reg[a] = '(%s %s %s)' % (rk(b), ARITH[o], rk(c))
        elif o == 'UNM': reg[a] = '-' + r(b)
        elif o == 'NOT': reg[a] = 'not ' + r(b)
        elif o == 'CONCAT': reg[a] = '..'.join(r(x) for x in range(b, c + 1))
        elif o in ('EQ', 'LT', 'LE'):
            lines.append('if %s(%s %s %s) skip' % ('' if a else 'not ', rk(b), {'EQ': '==', 'LT': '<', 'LE': '<='}[o], rk(c)))
        elif o == 'TEST': lines.append('test %s==%d' % (r(b), c))
        elif o == 'JMP': lines.append('jmp -> %d' % (pc + 1 + bx - 131071))
        elif o in ('CALL', 'TAILCALL'):
            args = [r(x) for x in range(a + 1, a + b)] if b else [r(x) for x in range(a + 1, a + 4) if x in reg] + ['...']
            s = '%s(%s)' % (r(a), ', '.join(args))
            lines.append(s if c in (0, 1) else 'r%d = %s' % (a, s))
            reg.update((x, 'ret(%s)' % s if len(s) < 60 else 'ret') for x in range(a, a + max(c - 1, 1)))
        elif o == 'RETURN': lines.append('return ' + ', '.join(r(x) for x in range(a, a + b - 1)) if b > 1 else 'return')
        elif o == 'CLOSURE':
            sub, nm = f['ps'][bx], None
            for j in range(pc + 1, min(pc + 3 + len(sub['ups']), len(f['code']))):
                if f['code'][j] & 63 == 7: nm = k[(f['code'][j] >> 6) & 0x3ffff]; break
            nm = nm or '%s.closure%d' % (name, bx)
            reg[a] = 'function ' + nm
            walk(sub, nm, out)
        else: reg[a] = '?'
    out.append((name, f, lines))
    return out


def dump(path):
    if not os.path.exists(path): path = os.path.join(SCRIPTS, path)
    return walk(Reader(open(path, 'rb').read()).proto(), 'main', [])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('file', nargs='?')
    ap.add_argument('regex', nargs='?')
    ap.add_argument('--code', action='store_true', help='print pseudo-code lines')
    ap.add_argument('--all', action='store_true', help='search every Data/scripts/*.lub')
    a = ap.parse_args()
    if a.all:
        pat = re.compile(a.file or '.', re.I)
        for path in sorted(glob.glob(os.path.join(SCRIPTS, '*.lub'))):
            for name, f, lines in dump(path):
                hits = [l for l in lines if pat.search(l)]
                for l in hits: print('%s: %s: %s' % (os.path.basename(path), name, l))
                if not hits and (pat.search(name) or any(isinstance(x, str) and pat.search(x) for x in f['k'])):
                    print('%s: %s' % (os.path.basename(path), name))
        return
    if not a.file: ap.error('FILE or --all required')
    pat = re.compile(a.regex, re.I) if a.regex else None
    for name, f, lines in dump(a.file):
        if pat and not (pat.search(name) or any(pat.search(l) for l in lines)
                        or any(isinstance(x, str) and pat.search(x) for x in f['k'])): continue
        params = f['locs'][:f['npar']] or ['a%d' % x for x in range(f['npar'])]  # stripped chunks have no local names
        print('== function %s (line %d, params %s%s)' % (name, f['line'], ', '.join(params) or '-', ', ...' if f['vararg'] else ''))
        print('   K: %s' % ' '.join(kstr(x) for x in f['k'] if x is not None))
        if a.code:
            for l in lines: print('    ' + l)


if __name__ == '__main__':
    main()
