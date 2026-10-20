#!/usr/bin/env python3
"""Export the match-setup lists of the install for the client (docs/match-setup.md).

  setuplists.py [OUTDIR]   default: client/assets (gitignored: game data, never commit it)
hats.txt      "<hat file>\t<text key>" per hat: WORMATTACHMENTS.XOM Hat.* MeshID (file = lower case, '.' -> '_') and its MeshTextNameID
multimaps.txt the Level_FileName of every SCRIPTS.XOM WXFE_LevelDetails of Level_Type 0 (the Prebuilt landscapes of WXFEP.SelectLandscape2)
"""
import os, sys
from pe import GAME
from tweak import dump


def main(a):
    out = a[0] if a else os.path.join(os.path.dirname(__file__), '..', '..', 'client', 'assets')
    tw = os.path.join(GAME, 'Data', 'Tweak')
    hats = [v for v in dump(os.path.join(tw, 'WORMATTACHMENTS.XOM')).values() if v.get('MeshID', '').startswith('Hat.')]
    with open(os.path.join(out, 'hats.txt'), 'w') as f:
        for h in hats: f.write('%s\t%s\n' % (h['MeshID'][4:].lower().replace('.', '_'), h['MeshTextNameID']))
    lv = sorted({v['Level_FileName'] for v in dump(os.path.join(tw, 'SCRIPTS.XOM')).values() if v.get('_type') == 'WXFE_LevelDetails' and v['Level_Type'] == 0})
    with open(os.path.join(out, 'multimaps.txt'), 'w') as f:
        f.write('\n'.join(lv) + '\n')
    print('%d hats, %d multiplayer maps -> %s' % (len(hats), len(lv), out))


if __name__ == '__main__':
    main(sys.argv[1:])
