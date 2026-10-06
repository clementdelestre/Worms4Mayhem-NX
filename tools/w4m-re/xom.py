#!/usr/bin/env python3
"""XOM reader with field names taken from the exe schema (see pe.py schema).

CLI:
  xom.py types FILE               type table (name, count, GUID)
  xom.py list FILE [TYPE_RE]      containers: index (1-based), type, name, size
  xom.py dump FILE NAME|#INDEX    decode one container (JSON, refs followed one level)
  xom.py check FILE...            how many containers decode to their exact end
FILE may be a path or a name relative to Data/ (e.g. Tweak/WEAPTWK.XOM).
Containers are walked in type order: schema size for CTNR-tagged ones, custom readers for untagged bundle ones.
"""
import json, os, re, struct, sys
from pe import PE, GAME

ELEM = {'bool': ('<B', 1), 'u8': ('<B', 1), 'i8': ('<b', 1), 'u16': ('<H', 2), 'i16': ('<h', 2), 'i32': ('<i', 4),
        'bits64': ('<Q', 8), 'u32': ('<I', 4), 'enum': ('<I', 4), 'u64': ('<Q', 8), 'f32': ('<f', 4),
        'rgbf': ('<3f', 12), 'rgbaf': ('<4f', 16), 'rgb8': ('<3B', 3), 'rgba8': ('<4B', 4),
        'vec2': ('<2f', 8), 'vec3': ('<3f', 12), 'vec3b': ('<3f', 12), 'vec3c': ('<3f', 12),
        'vec4': ('<4f', 16), 'quat': ('<4f', 16), 'mat43': ('<12f', 48), 'mat44': ('<16f', 64)}

# type objects pe.py leaves as hex: bounding sphere, bounding box, 2 f32 (tex coords), 3 f32 (normals)
ELEM.update({'0x96ece8': ('<4f', 16), '0x96ed00': ('<6f', 24), '0x96e9e8': ('<2f', 8), '0x96eb08': ('<3f', 12)})

_pe = None
def exe():
    global _pe
    if _pe is None: _pe = PE()
    return _pe


def path_of(f):
    return f if os.path.exists(f) else os.path.join(GAME, 'Data', f)


def varint(b, p):
    v = sh = 0
    while True:
        c = b[p]; p += 1; v |= (c & 0x7f) << sh; sh += 7
        if not c & 0x80: return v, p


def untagged_end(t, b, p):
    """End of a container written without CTNR/header (bundles: descriptors, XGraphSet, XAnimClipLibrary); None if unknown."""
    v = lambda p: varint(b, p)[1]
    u16 = lambda p: struct.unpack_from('<H', b, p)[0]
    u32 = lambda p: struct.unpack_from('<I', b, p)[0]
    if t == 'XMeshDescriptor': return v(v(p) + 2) + 2          # name, u16 bundle, ref graph, 2 bytes
    if t == 'XBitmapDescriptor': return v(v(p) + 2) + 4        # name, u16 bundle, ref, u16 w, u16 h
    if t == 'XSpriteSetDescriptor': return v(v(p) + 2)
    if t == 'XCustomDescriptor': return v(p) + 4
    if t == 'XNullDescriptor': return v(p) + 2
    if t == 'XTextDescriptor': p = v(v(p) + 2); return p + 4 + 6 * u16(p)  # name, u16 bundle, ref, u16 k, 2 B, k x 6 B
    if t == 'XGraphSet':
        n, p = varint(b, p)
        for _ in range(n): p = v(v(p + 16))
        return p
    if t == 'XAnimClipLibrary':
        p = v(p); nk = u32(p); p += 4
        for _ in range(nk): p = v(p + 4)
        nc = u32(p); p += 4
        for _ in range(nc):
            p = v(p + 4); m = u16(p); full = m in (0x100, 0x101)
            if not full: nch = u32(p); p += 4
            for _ in range(nk if full else nch):
                if u16(p) == 0x100: p += 16; continue
                p += 4 + (0 if full else 2) + 8
                p += 4 + 24 * u32(p)
        return p
    return None


class Xom:
    def __init__(self, path):
        self.path = path_of(path)
        b = self.b = open(self.path, 'rb').read()
        assert b[:4] == b'MOIK', 'not a XOM file'
        nt = struct.unpack_from('<I', b, 24)[0]
        self.types = []
        for t in range(nt):
            o = 64 + 64 * t
            self.types.append((b[o + 32:o + 64].split(b'\0')[0].decode(), struct.unpack_from('<I', b, o + 8)[0],
                               b[o + 16:o + 32].hex()))
        s = b.find(b'STRS')
        n, nb = struct.unpack_from('<II', b, s + 4)
        offs = struct.unpack_from('<%dI' % n, b, s + 12)
        blob = b[s + 12 + 4 * n:s + 12 + 4 * n + nb]
        self.strs = [blob[o:blob.find(b'\0', o)].decode('latin1') for o in offs]
        p = s + 12 + 4 * n + nb
        self.ctn, self.untagged = [], set()  # (type, bytes after the CTNR tag, or all bytes when untagged)
        for name, cnt, _ in self.types:
            for _ in range(cnt):
                if p >= len(b): break
                if b[p:p + 4] != b'CTNR':
                    e = untagged_end(name, b, p)
                    if e is None: break
                    self.untagged.add(len(self.ctn) + 1); self.ctn.append((name, b[p:e])); p = e; continue
                e = self._end(name, b, p)
                self.ctn.append((name, b[p + 4:e])); p = e
        self.names = {}
        for i, (t, d) in enumerate(self.ctn):
            if t == 'XContainerResourceDetails':
                try:
                    ref, p = varint(d, 3); nm, _ = varint(d, p)
                    self.names.setdefault(ref, self.strs[nm])
                except (IndexError, ValueError): pass

    def _end(self, t, b, p):
        """End of the tagged container at p: schema size (optional flag-0x20 fields dropped if needed) that lands on
        the nearest following CTNR, else that CTNR (bundles: the next container may be untagged)."""
        nx = b.find(b'CTNR', p + 4)
        nx = len(b) if nx < 0 else nx
        fl, best = exe().fields(t), None
        for fs in (fl, [f for f in fl if not f[4] & 0x20]):
            q = p + 7
            try:
                for f in fs: q = self._skip(b, q, f[2], f[4] & 1)
            except (ValueError, IndexError): continue
            if q == nx: return q
            if best is None and q < nx: best = q
        return best if fl and best is not None else nx

    def _skip(self, b, p, ty, arr):
        n = 1
        if arr: n, p = varint(b, p)
        if ty in ELEM: return p + n * ELEM[ty][1]
        if ty not in ('str', 'ref', 'ptr'): raise ValueError(ty)
        for _ in range(n): _, p = varint(b, p)
        return p

    def name(self, i):
        """Name of container i (1-based): XContainerResourceDetails key, else its first string field."""
        if i in self.names: return self.names[i]
        if i in self.untagged:
            return self.strs[varint(self.ctn[i - 1][1], 0)[0]] if self.ctn[i - 1][0].endswith('Descriptor') else ''
        r = self.decode(i, follow=False)
        for k in ('Name', 'ResourceName', 'ScriptName'):
            if isinstance(r.get(k), str): return r[k]
        return ''

    def find(self, key):
        if key.startswith('#'): return int(key[1:])
        for i, n in self.names.items():
            if n == key: return i
        for i in range(1, len(self.ctn) + 1):
            if self.name(i) == key: return i
        raise KeyError(key)

    def _val(self, d, p, ty, arr):
        n = 1
        if arr: n, p = varint(d, p)
        out = []
        for _ in range(n):
            if ty == 'str':
                v, p = varint(d, p); out.append(self.strs[v] if v < len(self.strs) else '?%d' % v)
            elif ty in ('ref', 'ptr'):
                v, p = varint(d, p); out.append({'ref': v})
            elif ty in ELEM:
                fmt, sz = ELEM[ty]; v = struct.unpack_from(fmt, d, p); p += sz
                v = [round(x, 6) if isinstance(x, float) else x for x in v]
                out.append(v[0] if len(v) == 1 else v)
            else:
                raise ValueError('unknown type ' + ty)
        return (out if arr else out[0]), p

    def decode(self, i, follow=True, depth=0):
        """dict of fields of container i; '_rest' holds undecoded bytes, '_exact' says if it ends cleanly."""
        fields = exe().fields(self.ctn[i - 1][0])
        r = self._decode(i, fields, follow, depth)
        if not r['_exact'] and any(f[3] == 0 for f in fields):
            # struct offset 0 marks fields some files lack (e.g. SchemeData.AssistedShotSettings)
            r2 = self._decode(i, [f for f in fields if f[3] != 0], follow, depth)
            if r2['_exact']: return r2
        return r

    def _decode(self, i, fields, follow, depth):
        t, d = self.ctn[i - 1]
        r = {'_index': i, '_type': t}
        if i in self.untagged:  # custom writer, no schema: raw bytes (layouts in docs/w4m/formats.md §15)
            return dict(r, _raw=d[:256].hex(), _size=len(d), _exact=True)
        p = 3  # 3 header bytes after CTNR
        try:
            for _, nm, ty, off, fl, va, _ in fields:
                v, p = self._val(d, p, ty, fl & 1)
                if follow and depth < 2:
                    for x in (v if isinstance(v, list) else [v]):
                        if isinstance(x, dict) and 'ref' in x and 0 < x['ref'] <= len(self.ctn):
                            x['name'] = self.names.get(x['ref'], '')
                            if depth < 1: x['value'] = self.decode(x['ref'], True, depth + 1)
                r[nm] = v
        except (ValueError, IndexError, struct.error) as e:
            r['_error'] = str(e)
        if p < len(d): r['_rest'] = d[p:p + 64].hex()
        r['_exact'] = p == len(d) and bool(fields) and '_error' not in r  # a read past the end also stops at len(d)
        return r


def main(a):
    if len(a) < 2: return print(__doc__)
    x = Xom(a[1])
    if a[0] == 'types':
        for n, c, g in x.types: print('%-48s %5d  %s' % (n, c, g))
    elif a[0] == 'list':
        r = re.compile(a[2] if len(a) > 2 else '.')
        for i, (t, d) in enumerate(x.ctn, 1):
            if r.search(t): print('%5d %-40s %-40s %d' % (i, t, x.name(i), len(d)))
    elif a[0] == 'dump':
        print(json.dumps(x.decode(x.find(a[2])), indent=1))
    elif a[0] == 'check':
        for f in a[1:]:
            x, bad, ok = Xom(f), {}, 0
            for i in range(1, len(x.ctn) + 1):
                if x.decode(i, False)['_exact']: ok += 1
                else: bad[x.ctn[i - 1][0]] = bad.get(x.ctn[i - 1][0], 0) + 1
            print('%s: %d/%d exact; not exact by type: %s' % (f, ok, len(x.ctn), bad))
    else: print(__doc__)


if __name__ == '__main__':
    main(sys.argv[1:])
