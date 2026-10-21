#!/usr/bin/env python3
"""Export the match-setup and team-editor lists of the install for the client (docs/match-setup.md, docs/w4m/frontend.md).

  setuplists.py [OUTDIR]   default: client/assets (gitignored: game data, never commit it)
multimaps.txt   the Level_FileName of every SCRIPTS.XOM WXFE_LevelDetails of Level_Type 0 (the Prebuilt landscapes of WXFEP.SelectLandscape2)
attachments.txt "<type>\t<WXFE.A id>\t<model>\t<MeshID>\t<text key>\t<lock>" per WORMATTACHMENTS entry, type 0 hat 1 gloves 2 glasses 3 tash
                4 (Hat.Terminator); model = <dir>/<file> under assets/models, written by w4m-models from this list
graves.txt      "<model>\t<mesh>\t<text key>\t<name>" per WXFEP.SelectGraveList row, in User.Grave order (0x725eda: index -> Grave.* mesh)
flags.txt       "<Flag id>\t<ui png>\t<text key>" per WXFEP.SelectFlagList row (LOCAL FlagDataContainer: TargaName, TextResource)
speech.txt      "<speech bank>\t<text key>" per WXFEP.SelectSoundbankList row (the bank is our voices/<bank without 'vo'>)
stockweapons.txt the Name of every LOCAL DATA.LockedWeapons stock Weapon Factory weapon (StockWeapon 1)
defteams.txt    DEFSAVE DATA.TeamBarracks in the client's teams.txt format (client/src/barracks.h)
"""
import os, sys
from pe import GAME
from tweak import dump

DIRS = {0: 'hats', 1: 'gloves', 2: 'glasses', 3: 'tash', 4: 'hats'}  # 4: Hat.Terminator alone


def model(mesh):  # w4m-models file name: the MeshID without its category prefix, lower-cased, '.' -> '_'
    return mesh.split('.', 1)[1].lower().replace('.', '_')


def column_text(r):
    return next((c['Text'] for c in r['Columns'] if c['_type'] in ('WXFE_StringTableColumns', 'WXFE_StringColumns')), '')


def main(a):
    out = a[0] if a else os.path.join(os.path.dirname(__file__), '..', '..', 'client', 'assets')
    tw = os.path.join(GAME, 'Data', 'Tweak')
    att = dump(os.path.join(tw, 'WORMATTACHMENTS.XOM'))
    parts = [(k, v) for k, v in att.items() if v.get('_type') == 'WXFE_WormAttachments' and v['Type'] in DIRS]
    with open(os.path.join(out, 'attachments.txt'), 'w') as f:
        for k, v in parts:
            f.write('%d\t%s\t%s/%s\t%s\t%s\t%s\n' % (v['Type'], k, DIRS[v['Type']], model(v['MeshID']), v['MeshID'], v['MeshTextNameID'], v.get('Lock', '')))
    lv = sorted({v['Level_FileName'] for v in dump(os.path.join(tw, 'SCRIPTS.XOM')).values() if v.get('_type') == 'WXFE_LevelDetails' and v['Level_Type'] == 0})
    with open(os.path.join(out, 'multimaps.txt'), 'w') as f:
        f.write('\n'.join(lv) + '\n')
    menu, local = dump(os.path.join(tw, 'MENUTWKX.XOM')), dump(os.path.join(tw, 'LOCAL.XOM'))
    graves = menu['WXFEP.SelectGraveList']['Rows']
    with open(os.path.join(out, 'graves.txt'), 'w') as f:
        for i, r in enumerate(graves):
            assert 'WXMsg.SetDataResource^User.Grave^%d' % i in r['Messages_Selected']
            f.write('graves/%s\t%s\t%s\t%s\n' % (model(r['Index']), r['Index'], column_text(r), r['Sort']))
    flags = menu['WXFEP.SelectFlagList']['Rows']
    with open(os.path.join(out, 'flags.txt'), 'w') as f:
        for r in flags:
            fd = local[r['Index']]
            f.write('%s\t%s\t%s\n' % (r['Index'], fd['TargaName'].lower().replace(' ', '_'), fd['TextResource']))
    speech = menu['WXFEP.SelectSoundbankList']['Rows']
    with open(os.path.join(out, 'speech.txt'), 'w') as f:
        for r in speech: f.write('%s\t%s\n' % (r['Index'], column_text(r)))
    stock = [w['Weapon']['Name'] for w in local['DATA.LockedWeapons']['Weapons'] if w['StockWeapon']]
    with open(os.path.join(out, 'stockweapons.txt'), 'w') as f:
        f.write('\n'.join(stock) + '\n')
    teams = dump(os.path.join(tw, 'DEFSAVE.XOM'))['DATA.TeamBarracks']['Teams']
    with open(os.path.join(out, 'defteams.txt'), 'w') as f:
        for t in teams:
            f.write('team %s\n' % t['TeamName'])
            for w in t['Worms']: f.write('worm %s\n' % w)
            f.write('skill %d\ngrave %d\nflag %s\nspeech %s\nweapon %s\n' % (t['Skill'], t['Grave'], t['Flag'], t['Speech'], t['CustomWeapon']))
            for k in ('Hat', 'Gloves', 'Glasses', 'Tash'): f.write('%s %s\n' % (k.lower(), t[k]))
            f.write('end\n')
    print('%d attachments, %d maps, %d graves, %d flags, %d speech banks, %d stock weapons, %d teams -> %s' %
          (len(parts), len(lv), len(graves), len(flags), len(speech), len(stock), len(teams), out))


if __name__ == '__main__':
    main(sys.argv[1:])
