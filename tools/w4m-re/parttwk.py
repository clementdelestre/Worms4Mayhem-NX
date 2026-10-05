#!/usr/bin/env python3
"""Export the PARTTWK effects the client starts from data (level EMITTER_ details, weather) for client/src/fx.cpp.

  parttwk.py [ASSETS]   default: client/assets (gitignored: game data, never commit it)
Reads ASSETS/maps/*.json "emitters", writes ASSETS/fx/parttwk.json (effects: name -> emitter names; emitters: name -> the
ParticleEmitterContainer fields below, W4M names and units) and ASSETS/fx/sets.txt for tools/w4m-models
("sprite <SpriteSet>" / "mesh <MeshSet> <clip>[+<clip>]"). Run after w4m-maps, before w4m-models.
"""
import glob, json, os, struct, sys, zlib
from pe import GAME
from tweak import dump
from xom import Xom, varint

# 0x5c0a84: an unknown name falls back to this effect; weather effects (RainGraphicEntity) are not named by maps
ROOTS = ['XXX_PlaceholderPP', 'WXP_RainFall', 'WXP_RainFallBG']
FIELDS = ['EmitterType', 'SpriteSet', 'MeshSet', 'MeshAnimNodeName', 'EmitterLifeTime', 'EmitterLifeTimeRandomise', 'EmitterMaxParticles',
          'EmitterNumSpawn', 'EmitterNumSpawnRadnomise', 'EmitterOriginOffset', 'EmitterOriginRandomise', 'EmitterParticleExpireFX',
          'EmitterParticleFX', 'EmitterSoundFX', 'EmitterSoundFXVolume', 'EmitterSpawnFreq', 'EmitterSpawnFreqRansomise', 'EmitterStartDelay',
          'ParticleAcceleration', 'ParticleAccelerationRandomise', 'ParticleAlpha', 'ParticleAlphaFadeIn', 'ParticleAlphaVelocity',
          'ParticleAlphaVelocityDelay', 'ParticleAlternateAccelerationN', 'ParticleAlternateAccelerationS', 'ParticleIsAlternateAcceleration',
          'ParticleColor', 'ParticleColorBand', 'ParticleNumColors', 'ParticleIsEffectedByWind', 'ParticleIsSpiral', 'ParticleLife',
          'ParticleLifeRandomise', 'ParticleMass', 'ParticleOrientation', 'ParticleOrientationRandomise', 'ParticleOrientationVelocity',
          'ParticleOrientationVelocityRandomise', 'ParticleSize', 'ParticleSizeRandomise', 'ParticleSizeVelocity', 'ParticleSizeVelocityRandomise',
          'ParticleSizeVelocityDelay', 'ParticleSizeVelocityDelayRandomise', 'ParticleFinalSizeScale', 'ParticleFinalSizeScaleRandomise',
          'ParticleSizeFadeIn', 'ParticleSizeFadeInRandomize', 'ParticleSizeFadeInDelay', 'ParticleSizeFadeInDelayRandomize',
          'ParticleSpiralRadius', 'ParticleSpiralRadiusRandomise', 'ParticleSpiralRadiusVelocity', 'ParticleSpiralRadiusVelocityRandomise',
          'ParticleSpiralRadiusSizeVelocity', 'ParticleVelocity', 'ParticleVelocityRandomise', 'ParticleVelocityIsNormalised',
          'ParticleLandCollideType', 'ParticleRenderScene', 'ParticleCanEnterWater', 'ParticleIsUnderWaterEffect']


def png(w, h, rgba):
    chunk = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
    raw = b''.join(b'\0' + bytes(rgba[r * w * 4:(r + 1) * w * 4]) for r in range(h))
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')


def sprite(bundles, name):
    """Sprite set / bitmap descriptor -> (XImage, blend (src, dst) or None, frames [(u, v, w, h)]): descriptor -> XGroup -> XShape -> shader."""
    for x in bundles:
        desc = [i for i, (t, d) in enumerate(x.ctn, 1) if t in ('XSpriteSetDescriptor', 'XBitmapDescriptor') and x.strs[varint(d, 0)[0]] == name]
        if not desc: continue
        d = x.ctn[desc[0] - 1][1]
        _, q = varint(d, 0)
        g, _ = varint(d, q + 2)
        while x.ctn[g - 1][0] not in ('XTexFont', 'XSimpleShader'):  # a bitmap descriptor names its shader directly
            r = x.decode(g, follow=False)
            if r['_type'] == 'XShape': g = r['Shader']['ref']; continue
            if not r.get('Children'): print('sprite set %s: %s without children' % (name, r['_type'])); return None
            g = r['Children'][0]['ref']
        sh = x.decode(g, follow=False)
        img = x.decode(x.decode(sh['TextureStages'][0]['ref'], follow=False)['Texture']['ref'], follow=False)
        blend = None
        for a in sh['Attributes']:
            r = x.decode(a['ref'], follow=False)
            if r['_type'] == 'XBlendModeGL': blend = (r['SourceFactor'], r['DestFactor'])
        frames = [c + s for c, s in zip(sh.get('CharCoords', [[0, 0]]), sh.get('CharSizes', [[1, 1]]))]
        if x.ctn[desc[0] - 1][0] == 'XBitmapDescriptor': frames = [[0, 0, 1, 1]]  # a bitmap is drawn whole (font glyph table unused)
        return img, blend, frames
    return None


def main(a):
    assets = a[0] if a else os.path.join(os.path.dirname(__file__), '..', '..', 'client', 'assets')
    tw = dump(os.path.join(GAME, 'Data', 'Tweak', 'PARTTWK.XOM'))
    folded = {k.lower(): k for k in tw}  # the resource trie folds case (0x6bdff0)
    names = set(ROOTS)
    for f in glob.glob(os.path.join(assets, 'maps', '*.json')):
        names.update(e['fx'] for e in json.load(open(f)).get('emitters', []))
    effects, emitters, sets = {}, {}, {'sprite Particle.RainSplash'}  # RainGraphicEntity's splashes (0x482910)
    todo = sorted(names)
    while todo:
        n = folded.get(todo.pop().lower(), '')
        c = tw.get(n)
        if c is None or n in effects or n in emitters: continue
        if c['_type'] == 'EffectDetailsContainer':
            effects[n] = c['EffectNames']
            todo += c['EffectNames']
            continue
        emitters[n] = {k: c[k] for k in FIELDS}
        todo += [c[k] for k in ('EmitterParticleFX', 'EmitterParticleExpireFX') if c[k]]
        sets.update('sprite ' + s for s in c['SpriteSet'].split(',') if s)
        clips = '+'.join(p.split(':')[-1] for p in c['MeshAnimNodeName'].split('+') if p)
        sets.update('mesh %s %s' % (m, clips or '-') for m in c['MeshSet'] if m)
    os.makedirs(os.path.join(assets, 'fx'), exist_ok=True)
    with open(os.path.join(assets, 'fx', 'parttwk.json'), 'w') as f:
        json.dump({'effects': effects, 'emitters': emitters}, f, separators=(',', ':'))
    with open(os.path.join(assets, 'fx', 'sets.txt'), 'w') as f:
        f.write(''.join(s + '\n' for s in sorted(sets)))
    bundles = [Xom(os.path.join(GAME, 'Data', 'Bundles', b)) for b in ('Bundl09.xom', 'Bundl10.xom')]
    with open(os.path.join(assets, 'fx', 'sprites.txt'), 'w') as f:
        for n in sorted(s[7:] for s in sets if s.startswith('sprite ')):
            r = sprite(bundles, n)
            if not r: print('sprite set %s: not found' % n); continue
            img, blend, frames = r
            px = img['Data']
            if img['Format'] == 0: px = [c for k in range(0, len(px), 3) for c in px[k:k + 3] + [255]]
            if img['Format'] not in (0, 1, 2): print('sprite set %s: image format %d' % (n, img['Format'])); continue
            w, h = img['Width'], img['Height']
            open(os.path.join(assets, 'fx', n.lower() + '.png'), 'wb').write(png(w, h, px[:w * h * 4]))
            # name, blend factors (W4M BlendFactor, -1 -1 = none), frames (u v w h)
            f.write('%s %d %d %s\n' % (n, *(blend or (-1, -1)), ' '.join('%g' % v for fr in frames for v in fr)))
    missing = sorted(n for n in names if n.lower() not in folded)
    print('%d effects, %d emitters, %d sets; not in PARTTWK (placeholder): %s' % (len(effects), len(emitters), len(sets), ' '.join(missing)))


if __name__ == '__main__':
    main(sys.argv[1:])
