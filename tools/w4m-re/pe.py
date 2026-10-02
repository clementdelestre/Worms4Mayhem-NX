#!/usr/bin/env python3
"""WormsMayhem.exe (PE32) helpers: sections, VA <-> offset, strings, RTTI, schema records.

CLI: pe.py sections | str REGEX | va2off VA | off2va OFF | words VA [N] | rtti REGEX | schema [CLASS_REGEX]
     | msg REGEX|VA (message handle objects <-> names)
"""
import os, re, signal, struct, sys, pickle

GAME = os.environ.get('W4M_DIR', os.path.expanduser(
    '~/snap/steam/common/.local/share/Steam/steamapps/common/WormsXHD'))
EXE = os.environ.get('W4M_EXE', os.path.join(GAME, 'WormsMayhem.exe'))
CACHE = os.environ.get('W4M_CACHE', os.path.expanduser('~/.cache/w4m-re'))

# Field type descriptors (BSS objects, built at runtime): name and byte size on disk ('v' = varint).
# Deduced from field names and from XOM files that parse to their exact end.
FIELD_TYPES = {
    0x967614: ('bool', 1), 0x967620: ('u8', 1), 0x96762c: ('i8', 1), 0x967638: ('u16', 2),
    0x967644: ('i16', 2), 0x967650: ('u32', 4), 0x96765c: ('i32', 4), 0x967668: ('u64', 8),
    0x967680: ('f32', 4), 0x967698: ('str', 'v'), 0x9676b0: ('bits64', 8), 0x9676c8: ('ptr', 'v'), 0x9676d4: ('enum', 4),
    0x96e910: ('rgbf', 12), 0x96e928: ('rgbaf', 16), 0x96e970: ('rgb8', 3), 0x96e988: ('rgba8', 4),
    0x96ea00: ('vec3b', 12), 0x96ea18: ('vec3c', 12), 0x96ebc8: ('vec2', 8), 0x96ebe0: ('vec3', 12),
    0x96ebf8: ('vec4', 16), 0x96ecb8: ('mat43', 48), 0x96ecd0: ('mat44', 64), 0x96ed48: ('quat', 16),
    0x970b94: ('ref', 'v'),
}


class PE:
    def __init__(self, path=EXE):
        self.path = path
        self.b = b = open(path, 'rb').read()
        pe = struct.unpack_from('<I', b, 0x3c)[0]
        n = struct.unpack_from('<H', b, pe + 6)[0]
        opt = struct.unpack_from('<H', b, pe + 20)[0]
        self.base = struct.unpack_from('<I', b, pe + 24 + 28)[0]
        self.secs = []  # (name, va, vsize, raw_off, raw_size)
        for i in range(n):
            o = pe + 24 + opt + 40 * i
            vs, va, rs, ra = struct.unpack_from('<IIII', b, o + 8)
            self.secs.append((b[o:o + 8].rstrip(b'\0').decode(), self.base + va, vs, ra, rs))

    def sec(self, name):
        return next(s for s in self.secs if s[0] == name)

    def section_of(self, va):
        for s in self.secs:
            if s[1] <= va < s[1] + max(s[2], s[4]): return s[0]

    def v2f(self, va):
        for _, sva, vs, ra, rs in self.secs:
            if sva <= va < sva + rs: return va - sva + ra

    def f2v(self, off):
        for _, sva, vs, ra, rs in self.secs:
            if ra <= off < ra + rs: return sva + off - ra

    def u32(self, va): return struct.unpack_from('<I', self.b, self.v2f(va))[0]
    def f32(self, va): return struct.unpack_from('<f', self.b, self.v2f(va))[0]
    def f64(self, va): return struct.unpack_from('<d', self.b, self.v2f(va))[0]

    def cstr(self, va, maxlen=200):
        """Printable NUL-terminated ASCII string at va, else None."""
        o = self.v2f(va)
        if o is None: return None
        e = self.b.find(b'\0', o, o + maxlen + 1)
        if e <= o: return None
        s = self.b[o:e]
        return s.decode() if all(32 <= c < 127 or c in (9, 10, 13) for c in s) else None

    def strings(self, minlen=4, sections=('.rdata', '.data')):
        """Yield (va, str) for every printable run ending with NUL."""
        pat = re.compile(rb'[\x20-\x7e\t\r\n]{%d,}\x00' % minlen)
        for s in self.secs:
            if s[0] not in sections: continue
            for m in pat.finditer(self.b, s[3], s[3] + s[4]):
                yield self.f2v(m.start()), m.group()[:-1].decode()

    def find_str(self, s):
        """VAs of exact NUL-terminated occurrences of s (also as suffix of a longer run)."""
        out, k = [], s.encode() + b'\0'
        for m in re.finditer(re.escape(k), self.b):
            va = self.f2v(m.start())
            if va and self.section_of(va) != '.text': out.append(va)
        return out

    # --- RTTI (MSVC) ---
    def rtti(self):
        """{class name: type descriptor VA} from '.?AV<name>@@' strings."""
        if hasattr(self, '_rtti'): return self._rtti
        self._rtti = {}
        for m in re.finditer(rb'\.\?A[VU]([\w@?$]+?)@@\0', self.b):
            self._rtti[m.group(1).decode()] = self.f2v(m.start() - 8)
        return self._rtti

    def _ptr_index(self):
        """{u32 value: [VA where it is stored]} over .rdata/.data, 4-byte aligned."""
        if hasattr(self, '_pidx'): return self._pidx
        idx = {}
        for s in self.secs:
            if s[0] not in ('.rdata', '.data'): continue
            for o in range(s[3], s[3] + s[4] - 3, 4):
                idx.setdefault(struct.unpack_from('<I', self.b, o)[0], []).append(s[1] + o - s[3])
        self._pidx = idx
        return idx

    def class_info(self, name):
        """(bases [self first], vtables [(va, offset in object)]) for an RTTI class."""
        td = self.rtti().get(name)
        if td is None: return [], []
        names = {v: k for k, v in self.rtti().items()}
        bases, vts = [], []
        for col in self._ptr_index().get(td, []):
            col -= 12  # COL: sig, offset, cdOffset, pTD, pCHD
            try: sig, off, cd, t, chd = struct.unpack_from('<5I', self.b, self.v2f(col))
            except (TypeError, struct.error): continue
            if sig != 0 or t != td or self.section_of(chd) != '.rdata': continue
            for p in self._ptr_index().get(col, []):  # vtable[-1] = COL
                vts.append((p + 4, off))
            if not bases:
                nb, bca = struct.unpack_from('<2I', self.b, self.v2f(chd) + 8)
                for i in range(min(nb, 64)):
                    bcd = self.u32(bca + 4 * i)
                    bases.append(names.get(self.u32(bcd), hex(self.u32(bcd))))
        return bases, sorted(set(vts))

    def vtables(self):
        """Sorted [(vtable VA, class)] over every RTTI class."""
        if hasattr(self, '_vts'): return self._vts
        self._vts = sorted((va, k) for k in self.rtti() for va, off in self.class_info(k)[1])
        return self._vts

    def vfuncs(self):
        """{function VA: [(class, slot)]} from every vtable."""
        if hasattr(self, '_vf'): return self._vf
        t = self.sec('.text'); vf = {}
        starts = {v for v, _ in self.vtables()}
        for va, cls in self.vtables():
            for k in range(400):
                if k and va + 4 * k in starts: break
                f = self.u32(va + 4 * k)
                if not t[1] <= f < t[1] + t[2]: break
                vf.setdefault(f, []).append((cls, k))
        self._vf = vf
        return vf

    def pushes(self, targets):
        """{imm32: [VA of 'push imm32']} in .text, for imm32 in targets."""
        t = self.sec('.text'); out = {}
        for m in re.finditer(rb'\x68', self.b[t[3]:t[3] + t[4]]):
            o = t[3] + m.start()
            v = struct.unpack_from('<I', self.b, o + 1)[0]
            if v in targets: out.setdefault(v, []).append(t[1] + m.start())
        return out

    # --- XOM class registry and schema ---
    def xclasses(self):
        """{type object VA: (name, parent type object VA, Serialize VA, factory VA, version)}.

        Static init stubs: push Serialize; push version; push factory; push parent; push info;
        mov ecx, typeobj; call 0x6c3c99 (0x6c5015 for abstract ones). info[0] -> class name.
        """
        if hasattr(self, '_xc'): return self._xc
        t = self.sec('.text'); code = self.b[t[3]:t[3] + t[4]]; out = {}
        pat = re.compile(rb'\x68(....)(?:\x6a(.)|\x68(....))\x68(....)\x68(....)\x68(....)\xb9(....)\xe8(....)', re.S)
        for m in pat.finditer(code):
            tgt = t[1] + m.end() + struct.unpack('<i', m.group(8))[0]
            if tgt not in (0x6c3c99, 0x6c5015): continue
            u = lambda k: struct.unpack('<I', m.group(k))[0]
            name = self.cstr(self.u32(u(6))) if self.v2f(u(6)) else None
            ver = m.group(2)[0] if m.group(2) else u(3)
            out[u(7)] = (name or '?', u(5), u(1), u(4), ver)
        self._xc = out
        return out

    def xchain(self, name):
        """[name, parent, grandparent...] through the XOM registry (prefix match for truncated names)."""
        xc = self.xclasses(); byname = {v[0]: k for k, v in xc.items()}
        k = byname.get(name) or next((byname[n] for n in byname if n.startswith(name)), None)
        out = []
        while k in xc and len(out) < 32:
            out.append(xc[k][0]); k = xc[k][1]
        return out

    def schema(self):
        """{class: [(push site, name, type, struct offset, flags, record VA, index)]} in file order.

        Field records: u32 name ptr, u32 (flags<<24 | index<<16 | struct offset), u32 type descriptor;
        scalars in .data, arrays (flag 1) in .rdata; flag 4 = transient (caches, not in files).
        The class Serialize() (from xclasses) pushes its records in file order, then calls its base.
        """
        import bisect, scan
        cache = os.path.join(CACHE, 'schema.pkl')
        st = os.stat(self.path)
        if os.path.exists(cache):
            key, data = pickle.load(open(cache, 'rb'))
            if key == (st.st_size, st.st_mtime, 5): return data
        recs = {}
        for sec in ('.data', '.rdata'):
            _, sva, _, ra, rs = self.sec(sec)
            o = ra
            while o < ra + rs - 12:
                n, w, t = struct.unpack_from('<III', self.b, o)
                if 0x960000 < t < 0x980000 and (w >> 24) & 8 and 0x815000 <= n < 0x90c000:
                    s = self.cstr(n, 64)
                    if s and re.fullmatch(r'[A-Za-z_][\w ]*', s):
                        recs[sva + o - ra] = (s, FIELD_TYPES.get(t, (hex(t), None))[0], w)
                        o += 12
                        continue
                o += 4
        ser = {v[2]: v[0] for v in self.xclasses().values()}
        starts = sorted(set(scan.index(self)['starts']) | set(ser))
        out = {}
        for va, sites in self.pushes(set(recs)).items():
            s, ty, w = recs[va]
            for site in sites:
                f = starts[bisect.bisect_right(starts, site) - 1]
                if f in ser:
                    out.setdefault(ser[f], []).append((site, s, ty, w & 0xffff, w >> 24, va, (w >> 16) & 0xff))
        for v in out.values(): v.sort()
        os.makedirs(CACHE, exist_ok=True)
        pickle.dump(((st.st_size, st.st_mtime, 5), out), open(cache, 'wb'))
        return out

    def msgnames(self):
        """{message handle object VA: name}, from `push name; mov ecx, obj; call 0x68bb9f` (one name may own several objects)."""
        if hasattr(self, '_msg'): return self._msg
        t = self.sec('.text'); out = {}
        for m in re.finditer(rb'\x68(....)\xb9(....)\xe8(....)', self.b[t[3]:t[3] + t[4]], re.S):
            s, obj, rel = struct.unpack('<IIi', m.group(1) + m.group(2) + m.group(3))
            if t[1] + m.end() + rel == 0x68bb9f and (n := self.cstr(s)): out[obj] = n
        self._msg = out
        return out

    def enum_values(self, rec):
        """(enum name, [value names]) for an enum field record: rec+0xc -> {name, 0, NUL-terminated name list}."""
        d = self.u32(rec + 0xc); out = []
        if not self.v2f(d) or not self.cstr(self.u32(d)): return None, []
        lst = self.u32(d + 8)
        while self.v2f(lst) and (n := self.u32(lst)) and (s := self.cstr(n)) and len(out) < 256:
            out.append(s); lst += 4
        return self.cstr(self.u32(d)), out

    def fields(self, cls):
        """Serialised fields of cls and its bases, file order (most derived class first)."""
        sch, out = self.schema(), []
        for c in self.xchain(cls) or [cls]:
            out += [f for f in sch.get(c, []) if not f[4] & 4]
        return out


def main(a):
    p = PE()
    if not a or a[0] == 'sections':
        for s in p.secs: print('%-9s va %08x vsize %07x raw %07x rsize %07x' % s)
    elif a[0] == 'str':
        r = re.compile(a[1])
        for va, s in p.strings(3):
            if r.search(s): print('%08x %-6s %r' % (va, p.section_of(va), s))
    elif a[0] == 'va2off': print(hex(p.v2f(int(a[1], 16))))
    elif a[0] == 'off2va': print(hex(p.f2v(int(a[1], 16))))
    elif a[0] == 'words':
        va = int(a[1], 16)
        for i in range(int(a[2]) if len(a) > 2 else 16):
            if p.v2f(va + 4 * i) is None: print('%08x (no file data: uninitialised)' % (va + 4 * i)); break
            w = p.u32(va + 4 * i); s = p.cstr(w, 80)
            print('%08x %08x %s' % (va + 4 * i, w, repr(s) if s else '(%s)' % p.section_of(w) if p.section_of(w) else ''))
    elif a[0] == 'rtti':
        r = re.compile(a[1])
        for k, v in sorted(p.rtti().items()):
            if not r.search(k): continue
            bases, vts = p.class_info(k)
            print('%s td=%08x bases=%s vtables=%s' % (k, v, ' < '.join(bases[1:]) or '-', ' '.join('%08x@%d' % x for x in vts) or '-'))
    elif a[0] == 'schema':
        r = re.compile(a[1] if len(a) > 1 else '.')
        for c, f in sorted(p.schema().items()):
            if not r.search(c): continue
            print(c)
            for site, n, t, off, fl, va, i in f:
                print('  %02x %-34s %-8s +0x%03x %s rec %08x push %08x' % (i, n, t + '[]' * (fl & 1), off, '~' if fl & 4 else ' ', va, site))
                if t == 'enum' and (e := p.enum_values(va))[0]: print('     %s: %s' % (e[0], ' '.join('%d=%s' % x for x in enumerate(e[1]))))
    elif a[0] == 'msg':
        m = p.msgnames()
        if re.fullmatch(r'0x[0-9a-fA-F]+', a[1]): print(m.get(int(a[1], 16), 'not a message handle'))
        else:
            r = re.compile(a[1])
            for va, n in sorted(m.items(), key=lambda x: (x[1], x[0])):
                if r.search(n): print('%08x %s' % (va, n))
    else:
        print(__doc__)


if __name__ == '__main__':
    signal.signal(signal.SIGPIPE, signal.SIG_DFL)
    main(sys.argv[1:])
