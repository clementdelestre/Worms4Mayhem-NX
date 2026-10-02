#!/usr/bin/env python3
"""FMOD Ex designer file (Data/Audio/PC/WormsX.fev, FEV1 v0x00400000): one row per event.

  fev.py [-g REGEX] [FILE]   TSV to stdout: event path, loop, volumes, 2D/3D, distances, sounds
  fev.py --json [FILE]       full parse -> $W4M_CACHE/fev.json
  fev.py --check [FILE]      self-check: parse ends at EOF, counts match the header hints
Layout: docs/w4m-map.md section 7. Output must stay outside the repository (game data).
"""
import json, math, os, re, struct, sys
from pe import GAME, CACHE

FEV = os.path.join(GAME, 'Data', 'Audio', 'PC', 'WormsX.fev')
LOOP = {0: 'loop', 1: 'oneshot', 2: 'loop_to_end'}  # sound instance loop mode
# sounddef play mode, raw: 3 on every 1-wave def (default), 2 on most multi-variant defs (random pick, assumed)
# Event property block (0x84 bytes, same for simple and complex events): offset -> (name, fmt)
HDR = {0x00: ('volume', 'f'), 0x04: ('pitch', 'f'), 0x08: ('pitch_rand', 'f'), 0x0c: ('vol_rand', 'f'),
       0x10: ('priority', 'I'), 0x14: ('max_playbacks', 'I'), 0x18: ('steal_priority', 'I'), 0x1c: ('mode', 'I'),
       0x20: ('min_dist', 'f'), 0x24: ('max_dist', 'f'), 0x28: ('flags28', 'I'), 0x58: ('max_pb_behavior', 'I'),
       0x5c: ('f5c', 'f'), 0x6c: ('fade_in_ms', 'I'), 0x70: ('fade_out_ms', 'I'), 0x7c: ('f7c', 'f')}


class Fev:
    def __init__(self, path=FEV):
        self.d, self.p = open(path, 'rb').read(), 0
        assert self.d[:4] == b'FEV1', 'not FEV1'
        self.p = 4
        self.ver, _, _ = self.u('3I')
        n = self.u('I')
        self.hints = dict(zip(*[iter(self.u('%dI' % (2 * n)))] * 2))
        self.project = self.s()
        self.banks = [dict(loadmode=self.u('I'), maxstreams=self.u('I'), hash=self.u('Q'), name=self.s())
                      for _ in range(self.u('I'))]
        self.cats = {}
        self.cat('', 1.0)
        self.events, self.ngroups, self.ninst = [], 0, 0
        for _ in range(self.u('I')): self.group('')
        self.sdprops = [self.sdprop() for _ in range(self.u('I'))]
        self.sds = [self.sd() for _ in range(self.u('I'))]
        self.reverbs = []
        for _ in range(self.u('I')):
            self.reverbs.append(self.s()); self.p += 132  # I3DL2-like block, not decoded
        size, tag = self.u('I4s')
        assert tag == b'comp' and self.p - 8 + size == len(self.d), 'trailing comp chunk'
        self.p = len(self.d)

    def u(self, f):
        v = struct.unpack_from('<' + f, self.d, self.p); self.p += struct.calcsize('<' + f)
        return v if len(v) > 1 else v[0]

    def s(self):
        n = self.u('I'); v = self.d[self.p:self.p + n]; self.p += n
        return v.rstrip(b'\0').decode('latin1')

    def cat(self, path, vol):
        nm = self.s(); v, pitch, _, _, n = self.u('ff3I')
        full = path + '/' + nm if path else nm
        self.cats[full.split('/', 1)[-1]] = dict(volume=v, pitch=pitch, gain=vol * v)
        for _ in range(n): self.cat(full, vol * v)

    def group(self, path):
        nm = self.s(); self.ngroups += 1
        full = path + '/' + nm if path else nm
        for _ in range(self.u('I')):
            k, t = self.s(), self.u('I')
            assert t in (0, 1, 2), 'user property type %d' % t
            self.u('i') if t == 0 else self.u('f') if t == 1 else self.s()
        ns, ne = self.u('2I')
        for _ in range(ne): self.event(full)
        for _ in range(ns): self.group(full)

    def inst(self):
        self.ninst += 1
        v = self.u('Hff3I4I3f2I')
        return dict(sd=v[0], start=v[1], length=v[2], start_mode=v[3], loop=LOOP.get(v[4], v[4]),
                    loop_count=struct.unpack('<i', struct.pack('<I', v[5]))[0], volume=v[10])

    def env(self):
        parent, name = self.u('i'), self.s()
        dsp_param, flags, _, n = self.u('4I')
        pts = [self.u('ffI') for _ in range(n)]
        self.u('2I')
        return dict(parent=parent, dsp=name or None, dsp_param=dsp_param, flags=flags, points=pts)

    def event(self, path):
        kind = self.u('I'); name = self.s(); guid = self.d[self.p:self.p + 16].hex(); self.p += 16
        hdr = self.d[self.p:self.p + 0x84]; self.p += 0x84
        e = dict(path=(path + '/' + name).split('/', 1)[1], kind={0x10: 'simple', 0x08: 'complex'}[kind], guid=guid)
        e.update({k: struct.unpack_from('<' + f, hdr, o)[0] for o, (k, f) in HDR.items()})
        if kind == 0x10:
            assert self.u('I') == 1
            e['layers'], e['params'] = [dict(insts=[self.inst()], envs=[])], []
        else:
            e['layers'] = []
            for _ in range(self.u('I')):
                flags, prio, param, ni, nenv = self.u('HhhHH')
                e['layers'].append(dict(param=param, insts=[self.inst() for _ in range(ni)],
                                        envs=[self.env() for _ in range(nenv)]))
            e['params'] = []
            for _ in range(self.u('I')):
                nm, vel, lo, hi, flags, _, _, nsus = self.s(), *self.u('fff4I')
                e['params'].append(dict(name=nm, velocity=vel, min=lo, max=hi, flags=flags,
                                        sustain=[self.u('f') for _ in range(nsus)]))
            self.u('I')
        assert self.u('I') == 1, 'category count'
        e['category'] = self.s()
        self.events.append(e)

    def sdprop(self):
        b = self.d[self.p:self.p + 70]; self.p += 70
        play, smin, smax, maxspawn, vol = struct.unpack_from('<4If', b)
        return dict(play=play, spawn_ms=(smin, smax), max_spawned=maxspawn, volume=vol,
                    f52=struct.unpack_from('<f', b, 52)[0], delay_ms=struct.unpack_from('<2H', b, 64))

    def sd(self):
        name, prop, n = self.s(), *self.u('2I')
        waves = []
        for _ in range(n):
            assert self.u('I') == 0, 'non-wavetable waveform'
            waves.append(dict(weight=self.u('I'), file=self.s(), bank=self.s(), index=self.u('I'), ms=self.u('I')))
        return dict(name=name, prop=prop, waves=waves)


def db(g): return '%.1f' % (20 * math.log10(g)) if g > 0 else '-inf'


def rows(f):
    yield ['event', 'kind', 'loop', 'vol_dB', 'sd_vol_dB', 'cat_dB', 'mode', 'rolloff', 'min', 'max', 'max_pb',
           'prio', 'pitch', 'pitch_rand', 'vol_rand', 'fade_in', 'fade_out', 'category', 'params', 'sounds']
    for e in f.events:
        insts = [i for L in e['layers'] for i in L['insts']]
        loops = sorted({i['loop'] for i in insts}) or ['silent']
        sds = [f.sds[i['sd']] for i in insts]
        sdvol = sorted({round(f.sdprops[s['prop']]['volume'], 4) for s in sds})
        m = e['mode']
        sounds = ['%s[p%s,%s,%d:%s]' % (s['name'], f.sdprops[s['prop']]['play'], db(f.sdprops[s['prop']]['volume']),
                                       len(s['waves']), ','.join(sorted({w['bank'] for w in s['waves']}))) for s in sds]
        yield [e['path'], e['kind'], '/'.join(loops), db(e['volume']), '/'.join(db(v) for v in sdvol),
               db(f.cats[e['category']]['gain']), '3D' if m & 0x10 else '2D',
               'linear' if m & 0x200000 else 'log' if m & 0x100000 else 'custom' if m & 0x4000000 else '-',
               '%g' % e['min_dist'], '%g' % e['max_dist'], e['max_playbacks'], e['priority'], '%g' % e['pitch'],
               '%g' % e['pitch_rand'], '%g' % e['vol_rand'], e['fade_in_ms'], e['fade_out_ms'], e['category'],
               ','.join('%s[%g..%g]' % (p['name'], p['min'], p['max']) for p in e['params']), ' '.join(sounds)]


def check(f):
    h = f.hints
    assert f.p == len(f.d)
    assert (len(f.banks), len(f.cats), f.ngroups) == (h[1], h[2], h[3]), 'bank/category/group counts'
    assert sum(e['kind'] == 'simple' for e in f.events) == h[10] and sum(e['kind'] == 'complex' for e in f.events) == h[11]
    assert f.ninst == h[8] and len(f.sds) == h[17] and sum(len(s['waves']) for s in f.sds) == h[13]
    cx = [e for e in f.events if e['kind'] == 'complex']
    envs = [en for e in cx for L in e['layers'] for en in L['envs']]
    assert (sum(len(e['layers']) for e in cx), sum(len(e['params']) for e in cx), len(envs),
            sum(len(en['points']) for en in envs)) == (h[9], h[5], h[6], h[7]), 'layer/param/envelope/point counts'
    assert all(i['sd'] < len(f.sds) for e in f.events for L in e['layers'] for i in L['insts'])
    assert all(s['prop'] < len(f.sdprops) for s in f.sds)
    print('ok: %d events, %d sounddefs, %d waveforms, parsed to EOF (%d bytes)'
          % (len(f.events), len(f.sds), h[13], len(f.d)))


def main(a):
    mode = a.pop(0) if a[:1] and a[0] in ('--json', '--check', '-g') else None
    rx = re.compile(a.pop(0)) if mode == '-g' else None
    f = Fev(*a)
    check(f) if mode == '--check' else None
    if mode == '--check': return
    if mode == '--json':
        os.makedirs(CACHE, exist_ok=True); dst = os.path.join(CACHE, 'fev.json')
        json.dump(dict(banks=f.banks, categories=f.cats, events=f.events, sounddefs=f.sds, sdprops=f.sdprops),
                  open(dst, 'w'), indent=1)
        print('%d events -> %s' % (len(f.events), dst)); return
    for r in rows(f):
        if rx is None or r[0] == 'event' or rx.search(r[0]): print('\t'.join(map(str, r)))


if __name__ == '__main__':
    main(sys.argv[1:])
