#!/usr/bin/env python3
"""FMV subtitle table: PERSIST.XOM FMVSubTiles -> <out>/movies/subs.txt (default client/assets, gitignored).
Lines: `<movie>\t<ms>\t<FETXT key>`; `<movie>\thold\t<ms>` = how long the last shown line stays (MoviePlayerService 0x609d8c: 8000 Welcome/Jurassic, else 5000)."""
import os, sys
from xom import Xom

MOVIES = {'FMV_MeetTheProf': 'Meet_The_Professor', 'FMV_Welcome': 'Welcome', 'FMV_Camelot': 'Camelot',
          'FMV_WildWest': 'WildWest', 'FMV_Arabian': 'Arabian', 'FMV_Jurassic': 'Jurassic'}
HOLD = {'Welcome': 8000, 'Jurassic': 8000}

def main(out):
    x = Xom('Tweak/PERSIST.XOM')
    rows = []
    for key, name in MOVIES.items():
        d = x.decode(x.find(key))
        rows.append('%s\thold\t%d' % (name, HOLD.get(name, 5000)))
        for l in sorted((l['value'] for l in d['TextLines']), key=lambda v: v['TimeOffset']):
            rows.append('%s\t%d\t%s' % (name, l['TimeOffset'], l['TextID']))
    os.makedirs(os.path.join(out, 'movies'), exist_ok=True)
    open(os.path.join(out, 'movies', 'subs.txt'), 'w').write('\n'.join(rows) + '\n')
    print(len(rows), 'rows')

if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', '..', 'client', 'assets'))
