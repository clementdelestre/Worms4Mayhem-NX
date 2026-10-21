#!/usr/bin/env python3
"""Export the match-setup and team-editor lists of the install for the client (docs/match-setup.md, docs/w4m/frontend.md).

  setuplists.py [OUTDIR]   default: client/assets (gitignored: game data, never commit it)
multimaps.txt   the Level_FileName of every SCRIPTS.XOM WXFE_LevelDetails of Level_Type 0 (the Prebuilt landscapes of WXFEP.SelectLandscape2)
attachments.txt "<type>\t<WXFE.A id>\t<model>\t<MeshID>\t<text key>\t<lock>\t<state>\t<Glasses>\t<Tash>\t<offsets>\t<fe pos xyz>\t<fe rot xyz>\t<fe scale xyz>"
                per WORMATTACHMENTS entry, type 0 hat 1 gloves 2 glasses 3 tash 4 (Hat.Terminator); model = <dir>/<file> under assets/models,
                written by w4m-models from this list; state = the lock's DEFSAVE WXFE_UnlockableItem State (0 Hidden, 1 Purchasable, 2 Unlocked;
                2 without a lock); Glasses / Tash = the entry's bools (a hat's gate whether glasses / tash are attached with it, 0x7573f7 +0x30 / +0x31;
                a hat with 0 also disables that editor row, WXD.EyesOk / TashOk); offsets = the Offsets list, ';'-separated entries of
                "px py pz rx ry rz sx sy sz" (PositionOffset, OrientationOffset radians, ScaleOffset; docs/w4m/frontend.md);
                fe pos / rot / scale: FrontEndOffsets, the WXFEP.Outfit preview's WXD.Vector.Position / Orientation / Scale (0x738e38)
graves.txt      "<model>\t<mesh>\t<text key>\t<name>" per WXFEP.SelectGraveList row, in User.Grave order (0x725eda: index -> Grave.* mesh)
flags.txt       "<Flag id>\t<ui png>\t<text key>" per WXFEP.SelectFlagList row (LOCAL FlagDataContainer: TargaName, TextResource)
speech.txt      "<speech bank>\t<text key>" per WXFEP.SelectSoundbankList row (the bank is our voices/<bank without 'vo'>)
stockweapons.json every LOCAL DATA.LockedWeapons stock Weapon Factory weapon (StockWeapon 1) in the client's weapons JSON: its
                WeaponFactoryContainer pair (docs/w4m/weapons.md "Stock weapons"), meshes proj_<Factory.Proj name>, graphic its
                WeaponGraphicsResourceID (0x5984a0)
stockmodels.txt "<model>\t<Factory.Proj mesh>" per payload those weapons use, for w4m-models
factorymodels.txt "<model>\t<mesh>" per held mesh of those weapons and of the WXD.Default* templates (factory/<mesh>), for w4m-models
defteams.txt    DEFSAVE DATA.TeamBarracks in the client's teams.txt format (client/src/barracks.h)
"""
import json, os, sys
from pe import GAME, PE
from tweak import dump

DIRS = {0: 'hats', 1: 'gloves', 2: 'glasses', 3: 'tash', 4: 'hats'}  # 4: Hat.Terminator alone


def model(mesh):  # w4m-models file name: the MeshID without its category prefix, lower-cased, '.' -> '_'
    return mesh.split('.', 1)[1].lower().replace('.', '_')


def column_text(r):
    return next((c['Text'] for c in r['Columns'] if c['_type'] in ('WXFE_StringTableColumns', 'WXFE_StringColumns')), '')


PAYLOAD_MESHES = 0x91fda8  # WeaponPayLoadEnum -> Factory.Proj.* mesh (0x5983f0 at 0x599486)
GAS_POISON = 10  # PARTTWK WXP_GasCloud ParticleCollisionWormPoisonMagnitude: the Poison payload's cloud


def proj_model(pe, payload):
    return 'proj_' + pe.cstr(pe.u32(PAYLOAD_MESHES + 4 * payload)).split('Factory.Proj.', 1)[1].lower().replace('.', '_')


def graphic(pe, w):  # 0x5984a0: "Factory.Proj.<payload>,," with no GraphicalResourceID, else "res,loc," per entry ("" past the locators)
    res, loc = w['GraphicalResourceID'], w['GraphicalLocatorID']
    if not res: return pe.cstr(pe.u32(PAYLOAD_MESHES + 4 * w['PayloadResourceId'])) + ',,'
    return ''.join('%s,%s,' % (r, loc[i] if i < len(loc) else '') for i, r in enumerate(res))


def stock_weapon(w, c, pe=PE()):
    assert w['Detonate'] in (0, 1, 3) and c['Detonate'] in (0, 3), w['Name']  # FirePress: no stock weapon
    kind = 'airstrike' if w['ProjectileLaunchType'] == 0 else 'homing' if w['Homing'] else 'shell'
    d = {'name': w['Name'], 'kind': kind, 'launch': w['ProjectileLaunchType'], 'detonate': w['Detonate'], 'cluster_detonate': c['Detonate'],
         'fuse_time': w['FuseTime'], 'powers_up': bool(w['ProjectilePowersUp']), 'max_power': w['ProjectileMaxPower'], 'wind': bool(w['EffectedByWind']),
         'avoid_land': bool(w['HomingAvoidLand']), 'poison': GAS_POISON if w['Poison'] else 0, 'cluster_poison': bool(c['Poison']),
         'clusters': w['ProjectileNumClusters'], 'cluster_spread': w['ClusterSpread'], 'collision': w['ProjectileCollisionRadius'],
         'cluster_collision': c['ProjectileCollisionRadius'], 'model': proj_model(pe, w['PayloadResourceId']), 'cluster_model': proj_model(pe, c['PayloadResourceId']),
         'ariel': w['ArielFX'], 'graphic': graphic(pe, w), 'count': 0, 'icon': 'Secret Weapon'}  # count: WXD.DefaultSchemeData WeaponFactoryWeapon.Ammo
    for p, x in (('', w), ('cluster_', c)):
        d.update({p + 'land_damage': x['LandDamageRadius'], p + 'worm_radius': x['WormDamageRadius'], p + 'worm_damage': x['WormDamageMagnitude'], p + 'blast_strength': x['Push']})
    return d


def main(a):
    out = a[0] if a else os.path.join(os.path.dirname(__file__), '..', '..', 'client', 'assets')
    tw = os.path.join(GAME, 'Data', 'Tweak')
    att = dump(os.path.join(tw, 'WORMATTACHMENTS.XOM'))
    save = dump(os.path.join(tw, 'DEFSAVE.XOM'))
    parts = [(k, v) for k, v in att.items() if v.get('_type') == 'WXFE_WormAttachments' and v['Type'] in DIRS]
    with open(os.path.join(out, 'attachments.txt'), 'w') as f:
        for k, v in parts:
            offs = ';'.join(' '.join('%g' % x for x in o['PositionOffset'] + o['OrientationOffset'] + o['ScaleOffset']) for o in v['Offsets'])
            fo = v.get('FrontEndOffsets') or {}
            vec = lambda key, d: ' '.join('%g' % x for x in fo.get(key, d))
            f.write('%d\t%s\t%s/%s\t%s\t%s\t%s\t%d\t%d\t%d\t%s\t%s\t%s\t%s\n' % (v['Type'], k, DIRS[v['Type']], model(v['MeshID']), v['MeshID'], v['MeshTextNameID'], v.get('Lock', ''),
                    save[v['Lock']]['State'] if v.get('Lock') in save else 2, v['Glasses'], v['Tash'], offs,
                    vec('PositionOffset', [0, 0, 0]), vec('OrientationOffset', [0, 0, 0]), vec('ScaleOffset', [1, 1, 1])))
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
    stock = [stock_weapon(w['Weapon'], w['Cluster']) for w in local['DATA.LockedWeapons']['Weapons'] if w['StockWeapon']]
    with open(os.path.join(out, 'stockweapons.json'), 'w') as f:
        f.write('[\n' + ',\n'.join('  ' + json.dumps(w) for w in stock) + '\n]\n')
    pe = PE()
    with open(os.path.join(out, 'stockmodels.txt'), 'w') as f:
        for p in sorted({x[k] for w in local['DATA.LockedWeapons']['Weapons'] if w['StockWeapon'] for x in (w['Weapon'], w['Cluster']) for k in ('PayloadResourceId',)}):
            f.write('%s\t%s\n' % (proj_model(pe, p), pe.cstr(pe.u32(PAYLOAD_MESHES + 4 * p))))
    held = [w['Weapon'] for w in local['DATA.LockedWeapons']['Weapons'] if w['StockWeapon']] + [menu[k]['Weapon'] for k in ('WXD.DefaultWeapon', 'WXD.DefaultThrown', 'WXD.DefaultAirStrike')]
    with open(os.path.join(out, 'factorymodels.txt'), 'w') as f:
        for m in sorted({m for w in held for m in graphic(pe, w).split(',')[::2] if m}):
            f.write('factory/%s\t%s\n' % (m.lower().replace('.', '_'), m))
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
