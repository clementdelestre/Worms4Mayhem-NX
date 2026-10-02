#!/usr/bin/env python3
"""Export the acting scenes of Data/Tweak/WORMACTING.XOM for the client (docs/worm-reactions.md).

  acting.py [OUT]   default: client/assets/acting.txt (gitignored: game data, never commit it)
One scene per line: name, then one field per track: "<criteria tag>|<event> <event>...", tab-separated.
An event is <ms><op><arg>: e emote (name[,PermittedEyeMovement[,Coyness]]), p play clip, x stop clip (BlendTime ms), s speech, l look at track, g gesture at track,
t threaten 0/1, f particle (WXP_ name). CastActor and accessories are dropped (acting scenes are pre-cast).
"""
import os, sys
from pe import GAME
from tweak import dump

OPS = {'WormEmote': ('e', 'Emote'), 'PlayAnimation': ('p', 'Animation'), 'StopAnimation': ('x', 'BlendTime'),
       'TriggerSpeech': ('s', 'Speech'), 'WormLookAt': ('l', 'TargetCastMember'), 'WormGestureAt': ('g', 'TargetCastMember'),
       'ThreatenWorm': ('t', 'Threatened'), 'SpawnParticle': ('f', 'ResourceId')}


def event(e):
    for k, (op, field) in OPS.items():
        if e['_type'].startswith('EFMV_' + k):
            arg = '' if field is None else str(e[field])
            if op == 'e' and (e.get('PermittedEyeMovement') or e.get('Coyness')):  # degrees (0x59be40); Coyness: head turned off the target
                arg += ',%g' % e['PermittedEyeMovement'] + (',%g' % e['Coyness'] if e.get('Coyness') else '')
            return '%d%s%s' % (e['Time'], op, arg[4:] if op == 'f' and arg.startswith('WXP_') else arg)


def main(a):
    out = a[0] if a else os.path.join(os.path.dirname(__file__), '..', '..', 'client', 'assets', 'acting.txt')
    scenes = dump(os.path.join(GAME, 'Data', 'Tweak', 'WORMACTING.XOM'))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, 'w') as f:
        for name, m in scenes.items():
            tracks = []
            for t in m['Track']:
                ev = [event(e) for e in t['Event']]  # file order: W4M's cursor stops at the first later Time (0x60b6db)
                tracks.append(t['Tag'].strip() + '|' + ' '.join(e for e in ev if e))
            f.write('\t'.join([name] + tracks) + '\n')
    print('%d scenes -> %s' % (len(scenes), out))


if __name__ == '__main__':
    main(sys.argv[1:])
