#!/usr/bin/env python3
"""Dump Data/Tweak/*.XOM resources as readable JSON, keyed by resource name, refs resolved.

  tweak.py [-o DIR] [FILE...]   default: every Tweak/*.XOM, output $W4M_CACHE/tweaks/<FILE>.json
  tweak.py -g REGEX [FILE...]   print matching resource names and scalar values (no JSON written)
Output must stay outside the repository (game data).
"""
import glob, json, os, re, sys
from pe import GAME, CACHE
from xom import Xom


def resolve(x, i, seen=()):
    if i in seen: return {'ref': i, 'cycle': True}
    r = x.decode(i, follow=False)
    out = {'_type': r['_type']}
    if not r['_exact']: out['_undecoded'] = r.get('_rest', '') or r.get('_error', '')
    for k, v in r.items():
        if k.startswith('_'): continue
        out[k] = sub(x, v, seen + (i,))
    return out


def sub(x, v, seen):
    if isinstance(v, list): return [sub(x, e, seen) for e in v]
    if isinstance(v, dict) and 'ref' in v:
        return resolve(x, v['ref'], seen) if 0 < v['ref'] <= len(x.ctn) else None
    return v


def dump(path):
    x, out = Xom(path), {}
    for i, (t, _) in enumerate(x.ctn, 1):
        if not t.startswith('X') or not t.endswith('ResourceDetails'): continue
        r = x.decode(i, follow=False)
        if 'Name' not in r: continue
        v = r.get('Value')
        out[r['Name']] = sub(x, v, ()) if t == 'XContainerResourceDetails' else v
    return out


def main(a):
    out_dir, grep = os.path.join(CACHE, 'tweaks'), None
    if a[:1] == ['-o']: out_dir, a = a[1], a[2:]
    if a[:1] == ['-g']: grep, a = re.compile(a[1]), a[2:]
    files = a or sorted(glob.glob(os.path.join(GAME, 'Data', 'Tweak', '*.XOM')))
    if not grep: os.makedirs(out_dir, exist_ok=True)
    for f in files:
        d = dump(f); base = os.path.basename(f)
        if grep:
            for k, v in d.items():
                if grep.search(k): print('%s: %s = %s' % (base, k, json.dumps(v)[:200]))
            continue
        dst = os.path.join(out_dir, base + '.json')
        json.dump(d, open(dst, 'w'), indent=1)
        print('%s: %d resources -> %s' % (base, len(d), dst))


if __name__ == '__main__':
    main(sys.argv[1:])
