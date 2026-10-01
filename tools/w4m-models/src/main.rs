// w4m-models <W4M dir> [out dir]: converts Worms 4 Mayhem meshes (XOM bundles) into glTF binaries
// (<name>.glb: embedded PNG textures, skin + sampled animations for the worm). Format notes: docs/w4m-formats.md.
use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};

// (output name, XMeshDescriptor name, target size in metres (0 = raw units, for meshes held at the worm's WeaponLocator),
// origin at the feet instead of the centre, clips; none = static)
const MODELS: &[(&str, &str, f32, bool, &[&str])] = &[
    ("worm", "W4.Worm", 1.25, true, WORM_CLIPS),
    ("bazooka", "Bazooka.Payload", 0.8, false, &[]),
    ("grenade", "Grenade.Payload", 0.5, false, &[]),
    ("cluster", "ClusterGrenade", 0.5, false, &[]),
    ("clusterlet", "ClusterBomb", 0.3, false, &[]),
    ("banana", "BananaBomb", 0.55, false, &[]),
    ("bananette", "Bananette", 0.35, false, &[]),
    ("holy", "HolyHandGrenade", 0.6, false, &[]),
    ("sheep", "Sheep", 0.8, false, &["Run", "Jump"]),
    ("airstrike", "Airstrike.Payload", 1.0, false, &[]),
    ("donkey", "Donkey", 1.6, false, &[]),
    ("crate_health", "Crate.Health", 0.9, false, &[]),
    ("crate_weapon", "Crate.Weapon", 0.9, false, &[]),
    ("crate_utility", "Crate.Utility", 0.9, false, &[]),
    ("mine", "Landmine", 0.4, false, &[]),
    ("barrel", "OilDrum", 1.0, false, &[]),
    ("hold_bazooka", "Bazooka.Weapon", 0.0, false, &[]),
    ("hold_grenade", "Grenade.Weapon", 0.0, false, &[]),
    ("hold_cluster", "ClusterGrenade", 0.0, false, &[]),
    ("hold_banana", "BananaBomb", 0.0, false, &[]),
    ("hold_holy", "HolyHandGrenade", 0.0, false, &[]),
    ("hold_sheep", "Sheep", 0.0, false, &[]),
    ("hold_shotgun", "Shotgun", 0.0, false, &[]),
    ("hold_radio", "Radio", 0.0, false, &[]),
    ("hold_rope", "NinjaRope.Gun", 0.0, false, &[]),
    ("supersheep", "SuperSheep", 0.9, false, &["Fly", "Run"]),
    ("oldwoman", "Oldwoman", 1.1, true, &["Walk", "Run"]),
    ("arrow", "Arrow", 0.7, false, &[]),
    ("homing", "HomingMissile.Payload", 0.8, false, &[]),
    ("dynamite", "Dynamite", 0.5, false, &[]),
    ("gas", "GasCanister", 0.5, false, &[]),
    ("starburst", "Starburst", 0.8, false, &[]),
    ("fatkins", "Fatkins.Fatboy", 2.2, false, &[]),
    ("scouser", "InflatedScouser", 1.4, false, &[]),
    ("sentry", "SentryGun", 1.0, false, &[]),
    ("hold_bat", "BaseballBat", 0.0, false, &[]),
    ("hold_sniper", "SniperRifle", 0.0, false, &[]),
    ("hold_bow", "Bow", 0.0, false, &[]),
    ("hold_homing", "HomingMissile.Weapon", 0.0, false, &[]),
    ("hold_dynamite", "Dynamite", 0.0, false, &[]),
    ("hold_gas", "GasCanister", 0.0, false, &[]),
    ("hold_landmine", "Landmine", 0.0, false, &[]),
    ("hold_starburst", "Starburst", 0.0, false, &[]),
    ("hold_supersheep", "SuperSheep", 0.0, false, &[]),
    ("hold_oldwoman", "Oldwoman", 0.0, false, &[]),
    ("hold_scouser", "Scouser", 0.0, false, &[]),
    ("hold_sentry", "SentryGun", 0.0, false, &[]),
    ("hold_flag", "SurrenderFlag", 0.0, false, &[]),
    ("grave0", "Grave.Cross", 0.9, true, &[]),
    ("grave1", "Grave.Worm", 0.9, true, &[]),
    ("grave2", "Grave.Obelisk", 0.9, true, &[]),
    ("grave3", "Grave.SkullnBones", 0.9, true, &[]),
    // Hats (Bundl172-248, one "Hat.<Name>" XMeshDescriptor each; attach at the worm's HatLocator like held items at WeaponLocator)
    ("hats/afro", "Hat.Afro", 0.0, false, &[]),
    ("hats/alien", "Hat.Alien", 0.0, false, &[]),
    ("hats/americanfootball", "Hat.AmericanFootball", 0.0, false, &[]),
    ("hats/americanfootball_bl", "Hat.AmericanFootball.Bl", 0.0, false, &[]),
    ("hats/americanfootball_s", "Hat.AmericanFootball.S", 0.0, false, &[]),
    ("hats/americanfootball_y", "Hat.AmericanFootball.Y", 0.0, false, &[]),
    ("hats/arabian", "Hat.Arabian", 0.0, false, &[]),
    ("hats/arabian_d", "Hat.Arabian.D", 0.0, false, &[]),
    ("hats/arabian_r", "Hat.Arabian.R", 0.0, false, &[]),
    ("hats/arabian_w", "Hat.Arabian.W", 0.0, false, &[]),
    ("hats/baseball", "Hat.Baseball", 0.0, false, &[]),
    ("hats/baseball_c", "Hat.Baseball.C", 0.0, false, &[]),
    ("hats/baseball_gy", "Hat.Baseball.Gy", 0.0, false, &[]),
    ("hats/baseball_p", "Hat.Baseball.P", 0.0, false, &[]),
    ("hats/baseball_pe", "Hat.Baseball.Pe", 0.0, false, &[]),
    ("hats/baseball_r", "Hat.Baseball.R", 0.0, false, &[]),
    ("hats/baseball_t17", "Hat.Baseball.T17", 0.0, false, &[]),
    ("hats/bishop", "Hat.Bishop", 0.0, false, &[]),
    ("hats/bluesbrother", "Hat.BluesBrother", 0.0, false, &[]),
    ("hats/britishtommy", "Hat.BritishTommy", 0.0, false, &[]),
    ("hats/builder", "Hat.Builder", 0.0, false, &[]),
    ("hats/bunny", "Hat.Bunny", 0.0, false, &[]),
    ("hats/burberry", "Hat.Burberry", 0.0, false, &[]),
    ("hats/chinese", "Hat.Chinese", 0.0, false, &[]),
    ("hats/cowboy", "Hat.Cowboy", 0.0, false, &[]),
    ("hats/cowboy_bk", "Hat.Cowboy.Bk", 0.0, false, &[]),
    ("hats/cowboy_r", "Hat.Cowboy.R", 0.0, false, &[]),
    ("hats/cowboy_w", "Hat.Cowboy.W", 0.0, false, &[]),
    ("hats/cowboy2", "Hat.Cowboy2", 0.0, false, &[]),
    ("hats/crown", "Hat.Crown", 0.0, false, &[]),
    ("hats/dino", "Hat.Dino", 0.0, false, &[]),
    ("hats/flatcap", "Hat.FlatCap", 0.0, false, &[]),
    ("hats/frankenstein", "Hat.Frankenstein", 0.0, false, &[]),
    ("hats/german", "Hat.German", 0.0, false, &[]),
    ("hats/helmet", "Hat.Helmet", 0.0, false, &[]),
    ("hats/helmetking", "Hat.HelmetKing", 0.0, false, &[]),
    ("hats/hockey", "Hat.Hockey", 0.0, false, &[]),
    ("hats/jetpack", "Hat.JetPack", 0.0, false, &[]),
    ("hats/party", "Hat.Party", 0.0, false, &[]),
    ("hats/pirate", "Hat.Pirate", 0.0, false, &[]),
    ("hats/pigtails", "Hat.Pigtails", 0.0, false, &[]),
    ("hats/pigtails_bl", "Hat.Pigtails.Bl", 0.0, false, &[]),
    ("hats/pigtails_bnd", "Hat.Pigtails.Bnd", 0.0, false, &[]),
    ("hats/pigtails_r", "Hat.Pigtails.R", 0.0, false, &[]),
    ("hats/police", "Hat.Police", 0.0, false, &[]),
    ("hats/prehistoric", "Hat.Prehistoric", 0.0, false, &[]),
    ("hats/professor", "Hat.Professor", 0.0, false, &[]),
    ("hats/punk", "Hat.Punk", 0.0, false, &[]),
    ("hats/punk_bl", "Hat.Punk.Bl", 0.0, false, &[]),
    ("hats/punk_gr", "Hat.Punk.Gr", 0.0, false, &[]),
    ("hats/punk_y", "Hat.Punk.Y", 0.0, false, &[]),
    ("hats/queenofsheba", "Hat.QueenOfSheba", 0.0, false, &[]),
    ("hats/redberet", "Hat.RedBeret", 0.0, false, &[]),
    ("hats/rocketman", "Hat.Rocketman", 0.0, false, &[]),
    ("hats/scottish", "Hat.Scottish", 0.0, false, &[]),
    ("hats/skull", "Hat.Skull", 0.0, false, &[]),
    ("hats/sovietarmy", "Hat.SovietArmy", 0.0, false, &[]),
    ("hats/spacesuit", "Hat.Spacesuit", 0.0, false, &[]),
    ("hats/spacesuit_bl", "Hat.Spacesuit.Bl", 0.0, false, &[]),
    ("hats/spacesuit_gy", "Hat.Spacesuit.Gy", 0.0, false, &[]),
    ("hats/spacesuit_p", "Hat.Spacesuit.P", 0.0, false, &[]),
    ("hats/terminator", "Hat.Terminator", 0.0, false, &[]),
    ("hats/usmarine", "Hat.USMarine", 0.0, false, &[]),
    ("hats/viking", "Hat.Viking", 0.0, false, &[]),
    ("hats/wizard", "Hat.Wizard", 0.0, false, &[]),
    ("hats/wizard_d", "Hat.Wizard.D", 0.0, false, &[]),
    ("hats/wizard_gr", "Hat.Wizard.Gr", 0.0, false, &[]),
    ("hats/wizard_r", "Hat.Wizard.R", 0.0, false, &[]),
    ("hats/alienbreed", "Hat.AlienBreed", 0.0, false, &[]),
    ("hats/worms", "Hat.Worms", 0.0, false, &[]),
    ("hats/wormsarmageddon", "Hat.WormsArmageddon", 0.0, false, &[]),
    ("hats/daveycrockett", "Hat.DaveyCrockett", 0.0, false, &[]),
    ("hats/deerstalker", "Hat.Deerstalker", 0.0, false, &[]),
    ("hats/fighterpilot", "Hat.FighterPilot", 0.0, false, &[]),
    ("hats/samurai", "Hat.Samurai", 0.0, false, &[]),
    ("hats/polarbear", "Hat.PolarBear", 0.0, false, &[]),
    // Title screen diorama (Bundl06/10), raw units: island + wreck, cloud dome, seagull (flight path is its Location clip)
    ("frontend/title", "WX.Mesh.Title", 0.0, false, &[]),
    ("frontend/sky", "FRONTEND.Sky", 0.0, false, &[]),
    ("seagull", "Particle.WXPMesh31", 0.0, false, &["WXM_SGull_WingFlap+WXM_SGull_Location"]),
];
// Worm clips exported (the rest of its 329 are emotes, weapon-specific holds and lip sync).
const WORM_CLIPS: &[&str] = &[
    "Base", "Walk", "Jump", "Fall", "Land", "Backflip", "Blastflight2", "AimBazooka+HoldBazooka", "AimGrenade+HoldThrown", "AimShotgun+HoldShotgun",
    "HoldBazooka", "HoldThrown", "Wounded", "Victorious_Grin", "Hit_Front", "HoldAirstrike", "HoldNinjarope", "Wave",
    "Yawn", "ScratchHead", "AimBat+HoldBat", "AimSniper+HoldSniper", "AimBow+HoldBow", "AimHomingMissile+HoldHomingMissile",
    "HoldFirepunch", "HoldProd", "HoldDynamite", "HoldLandmine", "HoldOldWoman", "HoldScouser", "HoldSentrygun", "HoldSurrender",
    "HoldSkipGo", "HoldGasgrenade", "HoldStarburst", "HoldSheep",
];
const FPS: f32 = 30.0;

fn vi(d: &[u8], p: &mut usize) -> usize {
    let mut v = 0usize;
    for n in 0..4 {
        let b = *d.get(*p).unwrap_or(&0);
        *p += 1;
        v |= ((b & 0x7f) as usize) << (7 * n);
        if b & 0x80 == 0 { break; }
    }
    v
}
fn u16le(d: &[u8], p: usize) -> usize { d.get(p..p + 2).map_or(0, |s| u16::from_le_bytes([s[0], s[1]]) as usize) }
fn u32le(d: &[u8], p: usize) -> u32 { d.get(p..p + 4).map_or(0, |s| u32::from_le_bytes(s.try_into().unwrap())) }
fn f32le(d: &[u8], p: usize) -> f32 { f32::from_bits(u32le(d, p)) }
fn fl<const N: usize>(d: &[u8], p: usize) -> [f32; N] { std::array::from_fn(|i| f32le(d, p + 4 * i)) }
fn skip_set(d: &[u8], p: &mut usize) { for _ in 0..vi(d, p) { vi(d, p); } }

fn strings(b: &[u8]) -> Option<(Vec<String>, usize)> {
    let nt = u32le(b, 24) as usize;
    let mut p = 64 + nt * 64 + 16;
    if b.get(p..p + 4)? != b"STRS" { p += 16; }
    let (ns, ls) = (u32le(b, p + 4) as usize, u32le(b, p + 8) as usize);
    let base = p + 12 + ns * 4;
    let mut s = Vec::with_capacity(ns);
    for i in 0..ns {
        let o = base + u32le(b, p + 12 + i * 4) as usize;
        let e = o + b.get(o..)?.iter().position(|&c| c == 0)?;
        s.push(String::from_utf8_lossy(&b[o..e]).into_owned());
    }
    Some((s, base + ls))
}

// End of a container's data for types sized by their content (XomView ReadXContainer); None = runs to the next
// "CTNR". Descriptors, graph sets and anim data have no CTNR tag nor 3-byte header, a few others may lack the tag.
fn exact(t: &str, b: &[u8], mut p: usize) -> Option<usize> {
    let v = |p: &mut usize| vi(b, p);
    Some(match t {
        "XBitmapDescriptor" => { v(&mut p); p += 2; v(&mut p); p + 4 }
        "XCustomDescriptor" => { v(&mut p); p + 4 }
        "XMeshDescriptor" => { v(&mut p); p += 2; v(&mut p); p + 2 }
        "XSpriteSetDescriptor" => { v(&mut p); p += 2; v(&mut p); p }
        "XTextDescriptor" => { v(&mut p); p += 1; v(&mut p); v(&mut p); let k = u16le(b, p); p + 4 + 6 * k }
        "XExpandedAnimInfo" => p + 7,
        "XAnimInfo" => p + 5,
        "XGraphSet" => {
            for _ in 0..v(&mut p) { p += 16; v(&mut p); v(&mut p); }
            p
        }
        "XAnimClipLibrary" => { clips(b, &[], &mut p); p }
        "XIndexSet" => { p += 3; let n = v(&mut p); p + 2 * n }
        "XIndexSet8" => { p += 3; let n = v(&mut p); p + n }
        "XAlphaTest" => p + 12,
        "XCullFace" => p + 7,
        "XLightingEnable" => p + 26,
        "XInteriorNode" => { p += 3; skip_set(b, &mut p); p += 20; v(&mut p); p }
        "XSimpleShader" => { p += 3; skip_set(b, &mut p); skip_set(b, &mut p); p += 4; v(&mut p); p + 1 }
        "XOglTextureMap" => { p += 23; v(&mut p); p += 26; v(&mut p); p }
        "PC_LandChunk" => p + 28,
        "PC_LandFrame" => {
            p += 3;
            for _ in 0..2 { let k = v(&mut p); p += k; }
            p += 374;
            for m in [8, 8, 4] { let k = v(&mut p); p += k * m; }
            p += 4;
            for m in [4, 4] { let k = v(&mut p); p += k * m; }
            let k = v(&mut p);
            p += k.saturating_sub(1) * 4 + 20;
            skip_set(b, &mut p);
            p += 20;
            v(&mut p);
            p
        }
        _ => return None,
    })
}

struct Xom { s: Vec<String>, c: Vec<(String, Vec<u8>)> } // c[0] = null reference

impl Xom {
    fn read(b: &[u8]) -> Option<Xom> {
        if b.get(0..4)? != b"MOIK" { return None; }
        let (s, mut p) = strings(b)?;
        let mut c = vec![(String::new(), Vec::new())];
        for t in 0..u32le(b, 24) as usize {
            let o = 64 + t * 64;
            let name = String::from_utf8_lossy(b.get(o + 32..o + 64)?).trim_end_matches('\0').to_string();
            for _ in 0..u32le(b, o + 8) {
                let ct = b.get(p..p + 4) == Some(b"CTNR");
                let q = if ct { p + 4 } else { p };
                let e = match exact(&name, b, q) {
                    Some(e) => e,
                    None if ct => b[q..].windows(4).position(|w| w == b"CTNR").map_or(b.len(), |i| q + i),
                    None => return None,
                };
                c.push((name.clone(), b.get(q..e)?.to_vec()));
                p = e;
            }
        }
        Some(Xom { s, c })
    }
    fn t(&self, i: usize) -> &str { self.c.get(i).map_or("", |c| c.0.as_str()) }
    fn d(&self, i: usize) -> &[u8] { self.c.get(i).map_or(&[], |c| &c.1) }
    fn str(&self, i: usize) -> String { self.s.get(i).cloned().unwrap_or_default() }
}

// Animation curve key: in-weight, in-angle, out-weight, out-angle, time, value (XomView TAnimClip).
type Key = [f32; 6];
struct Clip { name: String, dur: f32, ch: HashMap<(String, u32), Vec<Key>> }

// XAnimClipLibrary: key types (u32 type, object) then clips (duration, name, channels of keys).
fn clips(b: &[u8], s: &[String], p: &mut usize) -> Vec<Clip> {
    vi(b, p);
    let nk = u32le(b, *p) as usize;
    *p += 4;
    let mut keys = Vec::new();
    for _ in 0..nk {
        let t = u32le(b, *p);
        *p += 4;
        let o = vi(b, p);
        keys.push((s.get(o).cloned().unwrap_or_default(), t));
    }
    let nc = u32le(b, *p) as usize;
    *p += 4;
    let mut out = Vec::new();
    for _ in 0..nc {
        let dur = f32le(b, *p);
        *p += 4;
        let name = s.get(vi(b, p)).cloned().unwrap_or_default();
        // 0x100/0x101: one channel per key type in order, else a count and an explicit key-type index per channel
        let n = u16le(b, *p);
        let expanded = n == 256 || n == 257;
        let n = if expanded { nk } else { *p += 4; u32le(b, *p - 4) as usize };
        let mut ch = HashMap::new();
        for k in 0..n {
            if u16le(b, *p) == 256 { *p += 16; continue; }
            *p += 4; // flags: must contribute, weighted, static, linear
            let ki = if expanded { k } else { *p += 2; u16le(b, *p - 2) };
            *p += 8; // pre/post infinity
            let nkf = u32le(b, *p) as usize;
            *p += 4;
            let kf: Vec<Key> = (0..nkf).map(|i| fl::<6>(b, *p + 24 * i)).collect();
            *p += 24 * nkf;
            if let Some(key) = keys.get(ki) { ch.insert(key.clone(), kf); }
        }
        out.push(Clip { name, dur, ch });
    }
    out
}

// Bezier segment between keys, solved for time by bisection (XomView GenAnimFrame/findBezier).
fn eval(k: &[Key], t: f32) -> f32 {
    let n = k.len() - 1;
    let i = if t < k[0][4] { 0 } else { (0..n).find(|&j| k[j][4] <= t && t < k[j + 1][4]).unwrap_or(n) };
    let (a, b) = (k[i], k[(i + 1).min(n)]);
    if t < k[0][4] || i == n || a[4] == b[4] { return a[5]; }
    let (x1, y1) = (a[4] + (b[4] - a[4]) * a[3].cos() * a[2] / 3.0, a[5] + (b[5] - a[5]) * a[3].sin() * a[2] / 3.0);
    let (x2, y2) = (b[4] - (b[4] - a[4]) * b[1].cos() * b[0] / 3.0, b[5] - (b[5] - a[5]) * b[1].sin() * b[0] / 3.0);
    let bz = |u: f32, p0: f32, p1: f32, p2: f32, p3: f32| {
        let v = 1.0 - u;
        v * v * v * p0 + 3.0 * u * v * v * p1 + 3.0 * u * u * v * p2 + u * u * u * p3
    };
    let (mut lo, mut hi) = (0.0, 1.0);
    let mut u = 0.5;
    for _ in 0..30 {
        u = (lo + hi) / 2.0;
        let x = bz(u, a[4], x1, x2, b[4]);
        if (x - t).abs() < 1e-4 { break; }
        if x > t { hi = u; } else { lo = u; }
    }
    bz(u, a[5], y1, y2, b[5])
}

type M4 = [[f32; 4]; 4];
const ID: M4 = [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]];
fn mul(a: &M4, b: &M4) -> M4 { std::array::from_fn(|i| std::array::from_fn(|j| (0..4).map(|k| a[i][k] * b[k][j]).sum())) }
fn tr(v: [f32; 3]) -> M4 { let mut m = ID; for i in 0..3 { m[i][3] = v[i]; } m }
fn sc(v: [f32; 3]) -> M4 { let mut m = ID; for i in 0..3 { m[i][i] = v[i]; } m }
// Rz * Ry * Rx, as XomView applies XOM euler angles
fn rot(v: [f32; 3]) -> M4 {
    let ([cx, cy, cz], [sx, sy, sz]) = (v.map(f32::cos), v.map(f32::sin));
    [
        [cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx, 0.0],
        [sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx, 0.0],
        [-sy, cy * sx, cy * cx, 0.0],
        [0.0, 0.0, 0.0, 1.0],
    ]
}
fn apply(m: &M4, p: [f32; 3], w: f32) -> [f32; 3] { std::array::from_fn(|i| m[i][0] * p[0] + m[i][1] * p[1] + m[i][2] * p[2] + m[i][3] * w) }

// Translation, rotation quaternion (x, y, z, w), scale of an affine matrix without shear.
fn decompose(m: &M4) -> ([f32; 3], [f32; 4], [f32; 3]) {
    let col = |j: usize| [m[0][j], m[1][j], m[2][j]];
    let len = |v: [f32; 3]| (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]).sqrt().max(1e-8);
    let mut s = [len(col(0)), len(col(1)), len(col(2))];
    let det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
        + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if det < 0.0 { s[0] = -s[0]; }
    let r: [[f32; 3]; 3] = std::array::from_fn(|i| std::array::from_fn(|j| m[i][j] / s[j]));
    let tr_ = r[0][0] + r[1][1] + r[2][2];
    let q = if tr_ > 0.0 {
        let k = (tr_ + 1.0).sqrt() * 2.0;
        [(r[2][1] - r[1][2]) / k, (r[0][2] - r[2][0]) / k, (r[1][0] - r[0][1]) / k, k / 4.0]
    } else if r[0][0] > r[1][1] && r[0][0] > r[2][2] {
        let k = (1.0 + r[0][0] - r[1][1] - r[2][2]).sqrt() * 2.0;
        [k / 4.0, (r[0][1] + r[1][0]) / k, (r[0][2] + r[2][0]) / k, (r[2][1] - r[1][2]) / k]
    } else if r[1][1] > r[2][2] {
        let k = (1.0 + r[1][1] - r[0][0] - r[2][2]).sqrt() * 2.0;
        [(r[0][1] + r[1][0]) / k, k / 4.0, (r[1][2] + r[2][1]) / k, (r[0][2] - r[2][0]) / k]
    } else {
        let k = (1.0 + r[2][2] - r[0][0] - r[1][1]).sqrt() * 2.0;
        [(r[0][2] + r[2][0]) / k, (r[1][2] + r[2][1]) / k, k / 4.0, (r[1][0] - r[0][1]) / k]
    };
    let ql = (q.iter().map(|v| v * v).sum::<f32>()).sqrt().max(1e-8);
    ([m[0][3], m[1][3], m[2][3]], q.map(|v| v / ql), s)
}

struct Group { path: String, xf: usize, parent: Option<usize> }
struct Part { pos: Vec<[f32; 3]>, nrm: Vec<[f32; 3]>, uv: Vec<[f32; 2]>, idx: Vec<u16>, img: usize, group: Option<usize>, skin: Vec<([u8; 4], [f32; 4])>, rgba: Vec<[u8; 4]> }
#[derive(Default)]
struct Scene { groups: Vec<Group>, seen: HashMap<usize, usize>, bones: Vec<(usize, usize)>, parts: Vec<Part>, lib: usize }

impl Scene {
    fn walk(&mut self, x: &Xom, i: usize, g: Option<usize>, depth: u32) {
        if i == 0 || depth > 64 { return; }
        let d = x.d(i);
        let mut p = 3;
        let refs = |p: &mut usize| -> Vec<usize> { (0..vi(d, p)).map(|_| vi(d, p)).collect() };
        match x.t(i) {
            "XGraphSet" => {
                p = 0;
                for _ in 0..vi(d, &mut p) {
                    p += 16;
                    let r = vi(d, &mut p);
                    vi(d, &mut p);
                    if x.t(r) == "XAnimClipLibrary" { self.lib = r; } else { self.walk(x, r, g, depth + 1); }
                }
            }
            "XInteriorNode" => for r in refs(&mut p) { self.walk(x, r, g, depth + 1) },
            "XGroup" | "XSkeletonRoot" | "XBinModifier" => {
                if self.seen.contains_key(&i) { return; }
                if x.t(i) == "XBinModifier" { p += 2; }
                let xf = vi(d, &mut p);
                let mut kids = refs(&mut p);
                p += 20;
                let name = if x.t(i) == "XBinModifier" { String::new() } else { x.str(vi(d, &mut p)) };
                if x.t(xf) == "XChildSelector" { kids.truncate(1); } // texture-animation alternatives: keep the first
                let path = match g { Some(g) if !self.groups[g].path.is_empty() => format!("{}|{name}", self.groups[g].path), _ => name };
                self.groups.push(Group { path, xf, parent: g });
                let gi = self.groups.len() - 1;
                self.seen.insert(i, gi);
                for r in kids { self.walk(x, r, Some(gi), depth + 1); }
            }
            "XSkin" => {
                let root = vi(d, &mut p);
                self.walk(x, root, None, depth + 1);
                for r in refs(&mut p) { self.walk(x, r, g, depth + 1); }
            }
            "XBone" => if let Some(g) = g { if !self.bones.iter().any(|b| b.0 == i) { self.bones.push((i, g)); } },
            "XShape" => {
                p += 4;
                let (sh, geo) = (vi(d, &mut p), vi(d, &mut p));
                self.part(x, sh, geo, &[], g);
            }
            "XSkinShape" => {
                let bones = refs(&mut p);
                p += 4;
                let (sh, geo) = (vi(d, &mut p), vi(d, &mut p));
                for &b in &bones { if !self.bones.iter().any(|x| x.0 == b) { self.bones.push((b, g.unwrap_or(0))); } }
                self.part(x, sh, geo, &bones, g);
            }
            _ => {}
        }
    }

    fn part(&mut self, x: &Xom, shader: usize, geo: usize, bones: &[usize], g: Option<usize>) {
        if x.t(geo) != "XIndexedTriangleSet" { return; }
        let d = x.d(geo);
        let mut p = 3;
        let iset = vi(d, &mut p);
        p += 8;
        let [cs, ns, cl, ts, ws] = [0; 5].map(|_| vi(d, &mut p));
        let arr = |i: usize, k: usize| -> (usize, usize) {
            let d = x.d(i);
            let mut p = 3;
            let n = vi(d, &mut p);
            if d.len() < p + n * k { (0, p) } else { (n, p) }
        };
        let (n, pp) = if x.t(cs) == "XCoord3fSet" { arr(cs, 12) } else { (0, 0) };
        if n == 0 { return; }
        let pos = (0..n).map(|i| fl::<3>(x.d(cs), pp + 12 * i)).collect();
        let nrm = match arr(ns, 12) { (m, q) if m == n && x.t(ns) == "XNormal3fSet" => (0..n).map(|i| fl::<3>(x.d(ns), q + 12 * i)).collect(), _ => vec![[0.0, 1.0, 0.0]; n] };
        let uv = match arr(ts, 8) { (m, q) if m == n && x.t(ts) == "XTexCoord2fSet" => (0..n).map(|i| fl::<2>(x.d(ts), q + 8 * i)).collect(), _ => vec![[0.0; 2]; n] };
        let rgba = match arr(cl, 4) { (m, q) if m == n && x.t(cl) == "XColor4ubSet" => (0..n).map(|i| std::array::from_fn(|c| x.d(cl)[q + 4 * i + c])).collect(), _ => vec![] };
        let (ni, q) = arr(iset, 2);
        let idx: Vec<u16> = (0..ni).map(|i| u16le(x.d(iset), q + 2 * i) as u16).filter(|&v| (v as usize) < n).collect();
        if x.t(iset) != "XIndexSet" || idx.len() != ni || ni % 3 != 0 { return; }
        // palette weights: per vertex `per` (shape-local bone index, weight) pairs
        let mut skin = Vec::new();
        if x.t(ws) == "XPaletteWeightSet" && !bones.is_empty() {
            let d = x.d(ws);
            let mut p = 3;
            let k = vi(d, &mut p);
            let ib = p;
            p += k;
            let per = u16le(d, p).clamp(1, 4);
            p += 2;
            let nw = vi(d, &mut p);
            if k == n * per && nw == k {
                for v in 0..n {
                    let (mut j, mut w) = ([0u8; 4], [0f32; 4]);
                    for c in 0..per {
                        let b = bones.get(d[ib + v * per + c] as usize).copied().unwrap_or(0);
                        j[c] = self.bones.iter().position(|x| x.0 == b).unwrap_or(0) as u8;
                        w[c] = f32le(d, p + 4 * (v * per + c));
                    }
                    skin.push((j, w));
                }
            }
        }
        // shader: first texture stage -> XOglTextureMap -> XImage
        let sd = x.d(shader);
        let mut sp = 3;
        let stages = if x.t(shader) == "XSimpleShader" { (0..vi(sd, &mut sp)).map(|_| vi(sd, &mut sp)).collect() } else { vec![] };
        let img = stages.first().filter(|&&s| x.t(s) == "XOglTextureMap").map_or(0, |&s| { let mut q = 23; vi(x.d(s), &mut q) });
        self.parts.push(Part { pos, nrm, uv, idx, img: if x.t(img) == "XImage" { img } else { 0 }, group: g, skin, rgba });
    }

    // Local matrix of every group, with the clip's curves layered on Base (offsets for keys Base also has).
    fn locals(&self, x: &Xom, clip: Option<(&[&Clip], Option<&Clip>, f32)>) -> Vec<M4> {
        self.groups.iter().map(|gr| {
            let d = x.d(gr.xf);
            let val = |ty: u32, k: usize, rest: f32| -> f32 {
                let key = (gr.path.clone(), ty | (k as u32) << 24);
                let Some((layers, base, t)) = clip else { return rest };
                let b = base.filter(|b| b.name != layers[0].name).and_then(|b| b.ch.get(&key)).filter(|k| !k.is_empty()).map(|k| k[0][5]);
                let top = layers.iter().find_map(|c| c.ch.get(&key).filter(|k| !k.is_empty()).map(|k| (k, if c.dur > 0.0 { t % (c.dur + 1e-4) } else { t })));
                match (top, b) {
                    (Some((kf, t)), Some(b)) if ty == 0x904 => (eval(kf, t) + b) / 2.0,
                    (Some((kf, t)), Some(b)) => eval(kf, t) + b,
                    (Some((kf, t)), None) => eval(kf, t),
                    (None, Some(b)) => b,
                    (None, None) => rest,
                }
            };
            let v3 = |ty: u32, r: [f32; 3]| -> [f32; 3] { std::array::from_fn(|k| val(ty, k, r[k])) };
            let size = |r: [f32; 3]| -> [f32; 3] { std::array::from_fn(|k| val(0x904, k, val(0x104, k, r[k]))) };
            match x.t(gr.xf) {
                "XJointTransform" => {
                    let f = fl::<15>(d, 3);
                    let (ro, r, pos, jo, s) = ([f[0], f[1], f[2]], [f[3], f[4], f[5]], [f[6], f[7], f[8]], [f[9], f[10], f[11]], [f[12], f[13], f[14]]);
                    mul(&mul(&mul(&mul(&tr(v3(0x102, pos)), &rot(r)), &rot(v3(0x103, jo))), &rot(ro)), &sc(size(s)))
                }
                "XTransform" => {
                    let f = fl::<9>(d, 3);
                    mul(&mul(&tr(v3(0x102, [f[0], f[1], f[2]])), &rot(v3(0x103, [f[3], f[4], f[5]]))), &sc(size([f[6], f[7], f[8]])))
                }
                "XMatrix" => {
                    let f = fl::<12>(d, 3);
                    let mut m = ID;
                    for c in 0..4 { for r in 0..3 { m[r][c] = f[c * 3 + r]; } }
                    m
                }
                _ => ID,
            }
        }).collect()
    }
    fn worlds(&self, locals: &[M4]) -> Vec<M4> {
        let mut w: Vec<M4> = Vec::with_capacity(locals.len());
        for (i, g) in self.groups.iter().enumerate() {
            let m = match g.parent { Some(p) if p < i => mul(&w[p], &locals[i]), _ => locals[i] };
            w.push(m);
        }
        w
    }
    // Bone skinning matrices: world of the bone's group times its inverse bind (pose) matrix.
    fn skinning(&self, x: &Xom, worlds: &[M4]) -> Vec<M4> {
        self.bones.iter().map(|&(b, g)| {
            let f = fl::<16>(x.d(b), 3);
            let pose: M4 = std::array::from_fn(|r| std::array::from_fn(|c| f[c * 4 + r]));
            mul(&worlds[g], &pose)
        }).collect()
    }
}

// Top mip of an XImage as RGBA8.
fn image(x: &Xom, i: usize) -> Option<(usize, usize, Vec<u8>)> {
    let d = x.d(i);
    let mut p = 3;
    vi(d, &mut p);
    let (w, h) = (u16le(d, p), u16le(d, p + 2));
    let mut q = p + 8;
    q += 1 + 4 * *d.get(q)? as usize;
    q += 1 + 4 * *d.get(q)? as usize;
    let fmt = u32le(d, q);
    q += 4;
    vi(d, &mut q);
    let bpp = match fmt { 0 => 3, 1 | 2 => 4, _ => { eprintln!("  image {} format {fmt} not supported", x.str(vi(d, &mut 3))); return None } };
    let px = d.get(q..q + w * h * bpp)?;
    Some((w, h, px.chunks(bpp).flat_map(|c| [c[0], c[1], c[2], if bpp == 4 { c[3] } else { 255 }]).collect()))
}

// PNG with stored (uncompressed) deflate blocks: no compressor needed, raylib/stb read it.
fn png(w: usize, h: usize, rgba: &[u8]) -> Vec<u8> {
    let crc_t: Vec<u32> = (0..256u32).map(|n| (0..8).fold(n, |c, _| if c & 1 != 0 { 0xedb88320 ^ (c >> 1) } else { c >> 1 })).collect();
    let crc = |d: &[u8]| !d.iter().fold(!0u32, |c, &b| crc_t[((c ^ b as u32) & 0xff) as usize] ^ (c >> 8));
    let mut raw = Vec::with_capacity((w * 4 + 1) * h);
    for r in rgba.chunks(w * 4) { raw.push(0); raw.extend_from_slice(r); }
    let mut z = vec![0x78, 0x01];
    let blocks: Vec<&[u8]> = raw.chunks(65535).collect();
    for (i, b) in blocks.iter().enumerate() {
        z.push((i + 1 == blocks.len()) as u8);
        z.extend((b.len() as u16).to_le_bytes());
        z.extend((!(b.len() as u16)).to_le_bytes());
        z.extend_from_slice(b);
    }
    let (a, bb) = raw.iter().fold((1u32, 0u32), |(a, b), &c| { let a = (a + c as u32) % 65521; (a, (b + a) % 65521) });
    z.extend(((bb << 16) | a).to_be_bytes());
    let mut o = b"\x89PNG\r\n\x1a\n".to_vec();
    let mut chunk = |t: &[u8], d: &[u8]| {
        o.extend((d.len() as u32).to_be_bytes());
        let mut c = t.to_vec();
        c.extend_from_slice(d);
        o.extend_from_slice(&c);
        o.extend(crc(&c).to_be_bytes());
    };
    let mut ihdr = (w as u32).to_be_bytes().to_vec();
    ihdr.extend((h as u32).to_be_bytes());
    ihdr.extend([8, 6, 0, 0, 0]);
    chunk(b"IHDR", &ihdr);
    chunk(b"IDAT", &z);
    chunk(b"IEND", &[]);
    o
}

#[derive(Default)]
struct Glb { bin: Vec<u8>, views: Vec<String>, accs: Vec<String> }
impl Glb {
    fn view(&mut self, d: &[u8]) -> usize {
        while self.bin.len() % 4 != 0 { self.bin.push(0); }
        self.views.push(format!("{{\"buffer\":0,\"byteOffset\":{},\"byteLength\":{}}}", self.bin.len(), d.len()));
        self.bin.extend_from_slice(d);
        self.views.len() - 1
    }
    fn acc(&mut self, d: &[u8], ctype: u32, count: usize, ty: &str, extra: &str) -> usize {
        let v = self.view(d);
        self.accs.push(format!("{{\"bufferView\":{v},\"componentType\":{ctype},\"count\":{count},\"type\":\"{ty}\"{extra}}}"));
        self.accs.len() - 1
    }
    fn floats(&mut self, v: &[f32], ty: &str, n: usize, minmax: bool) -> usize {
        let b: Vec<u8> = v.iter().flat_map(|f| f.to_le_bytes()).collect();
        let mut extra = String::new();
        if minmax {
            let k = v.len() / n;
            let col = |i: usize| v.iter().skip(i).step_by(k);
            let lo: Vec<String> = (0..k).map(|i| format!("{}", col(i).fold(f32::MAX, |a, &b| a.min(b)))).collect();
            let hi: Vec<String> = (0..k).map(|i| format!("{}", col(i).fold(f32::MIN, |a, &b| a.max(b)))).collect();
            extra = format!(",\"min\":[{}],\"max\":[{}]", lo.join(","), hi.join(","));
        }
        self.acc(&b, 5126, n, ty, &extra)
    }
}

fn convert(x: &Xom, desc: usize, size: f32, feet: bool, wanted: &[&str]) -> Option<(Vec<u8>, String)> {
    let mut sc_ = Scene::default();
    let d = x.d(desc);
    let mut p = 0;
    vi(d, &mut p);
    p += 2;
    let graph = vi(d, &mut p);
    sc_.walk(x, graph, None, 0);
    let s = sc_;
    if s.parts.is_empty() { return None; }
    let lib = if s.lib != 0 { clips(x.d(s.lib), &x.s, &mut 0) } else { vec![] };
    let base = lib.iter().find(|c| c.name == "Base");
    let animated = s.parts.iter().any(|p| !p.skin.is_empty()) && !lib.is_empty();
    // "A+B": clip A layered over B (channels A lacks come from B, then Base)
    let chosen: Vec<Vec<&Clip>> = if !animated { vec![] } else {
        wanted.iter().map(|n| n.split('+').filter_map(|n| lib.iter().find(|c| c.name == n)).collect::<Vec<_>>()).filter(|l| !l.is_empty()).collect()
    };
    let animated = animated && !chosen.is_empty();

    // rest pose (first chosen clip at t = 0, else the stored transforms): baked vertices, normalisation box
    let rest = s.worlds(&s.locals(x, chosen.first().map(|c| (&c[..], base, 0.0))));
    let skin_rest = s.skinning(x, &rest);
    let place = |pt: &Part, v: [f32; 3], w: f32, i: usize| -> [f32; 3] {
        if pt.skin.is_empty() { return pt.group.map_or(v, |g| apply(&rest[g], v, w)); }
        let (j, wt) = pt.skin[i];
        let mut o = [0.0; 3];
        for c in 0..4 { if wt[c] > 0.0 { let q = apply(&skin_rest[j[c] as usize], v, w); for k in 0..3 { o[k] += q[k] * wt[c]; } } }
        o
    };
    let (mut lo, mut hi) = ([f32::MAX; 3], [f32::MIN; 3]);
    for pt in &s.parts { for (i, &v) in pt.pos.iter().enumerate() { let q = place(pt, v, 1.0, i); for k in 0..3 { lo[k] = lo[k].min(q[k]); hi[k] = hi[k].max(q[k]); } } }
    let ext = if feet { hi[1] - lo[1] } else { (0..3).map(|k| hi[k] - lo[k]).fold(0.0, f32::max) };
    let k = if size > 0.0 { size / ext.max(1e-6) } else { 1.0 };
    let c = [(lo[0] + hi[0]) / 2.0, if feet { lo[1] } else { (lo[1] + hi[1]) / 2.0 }, (lo[2] + hi[2]) / 2.0];
    let norm = if size > 0.0 { mul(&sc([k; 3]), &tr(c.map(|v| -v))) } else { ID };
    // extra joints without vertices: their pose is the locator's world matrix, where held meshes/hats attach
    const LOCATORS: &[&str] = &["WeaponLocator", "HatLocator"];
    let sockets: Vec<(usize, &str)> = LOCATORS.iter()
        .filter_map(|&loc| s.groups.iter().position(|g| animated && g.path.ends_with(loc)).map(|i| (i, loc))).collect();
    let skin_all = |w: &[M4]| { let mut m = s.skinning(x, w); m.extend(sockets.iter().map(|&(g, _)| w[g])); m };

    let mut g = Glb::default();
    let mut images: Vec<usize> = s.parts.iter().map(|p| p.img).filter(|&i| i != 0).collect();
    images.sort();
    images.dedup();
    let mut img_json = Vec::new();
    for &i in &images {
        let (w, h, rgba) = image(x, i).unwrap_or((1, 1, vec![255; 4]));
        let v = g.view(&png(w, h, &rgba));
        img_json.push(format!("{{\"bufferView\":{v},\"mimeType\":\"image/png\"}}"));
    }
    // static parts sharing a texture are merged (one draw call each), skinned ones kept as they are
    struct Prim { img: usize, pos: Vec<f32>, nrm: Vec<f32>, uv: Vec<f32>, idx: Vec<u16>, skin: Vec<([u8; 4], [f32; 4])>, rgba: Vec<u8> }
    let mut out: Vec<Prim> = Vec::new();
    for pt in &s.parts {
        let (pos, nrm): (Vec<f32>, Vec<f32>) = if animated {
            (pt.pos.iter().flatten().copied().collect(), pt.nrm.iter().flatten().copied().collect())
        } else {
            let pos = pt.pos.iter().enumerate().flat_map(|(i, &v)| apply(&norm, place(pt, v, 1.0, i), 1.0)).collect();
            let nrm = pt.nrm.iter().enumerate().flat_map(|(i, &v)| {
                let n = place(pt, v, 0.0, i);
                let l = (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]).sqrt().max(1e-6);
                n.map(|c| c / l)
            }).collect();
            (pos, nrm)
        };
        let n = pt.pos.len();
        let uv: Vec<f32> = pt.uv.iter().flatten().copied().collect();
        let skin = if !animated { vec![] } else if pt.skin.len() == n { pt.skin.clone() } else { vec![([0; 4], [1.0, 0.0, 0.0, 0.0]); n] };
        let rgba: Vec<u8> = if pt.rgba.len() == n { pt.rgba.iter().flatten().copied().collect() } else { vec![255; 4 * n] };
        if let Some(o) = out.iter_mut().find(|o| !animated && o.img == pt.img && o.pos.len() / 3 + n < 65536) {
            let base = (o.pos.len() / 3) as u16;
            o.idx.extend(pt.idx.iter().map(|&i| i + base));
            o.pos.extend(pos);
            o.nrm.extend(nrm);
            o.uv.extend(uv);
            o.rgba.extend(rgba);
        } else {
            out.push(Prim { img: pt.img, pos, nrm, uv, idx: pt.idx.clone(), skin, rgba });
        }
    }
    let mut prims = Vec::new();
    for pt in &out {
        let n = pt.pos.len() / 3;
        let a_pos = g.floats(&pt.pos, "VEC3", n, true);
        let a_nrm = g.floats(&pt.nrm, "VEC3", n, false);
        let a_uv = g.floats(&pt.uv, "VEC2", n, false);
        let ib: Vec<u8> = pt.idx.iter().flat_map(|i| i.to_le_bytes()).collect();
        let a_idx = g.acc(&ib, 5123, pt.idx.len(), "SCALAR", "");
        let mut attrs = format!("\"POSITION\":{a_pos},\"NORMAL\":{a_nrm},\"TEXCOORD_0\":{a_uv}");
        if pt.rgba.iter().any(|&c| c != 255) {
            let a_c = g.acc(&pt.rgba, 5121, n, "VEC4", ",\"normalized\":true");
            attrs += &format!(",\"COLOR_0\":{a_c}");
        }
        if animated {
            let j: Vec<u8> = pt.skin.iter().flat_map(|s| s.0).collect();
            let a_j = g.acc(&j, 5121, n, "VEC4", "");
            let a_w = g.floats(&pt.skin.iter().flat_map(|s| s.1).collect::<Vec<_>>(), "VEC4", n, false);
            attrs += &format!(",\"JOINTS_0\":{a_j},\"WEIGHTS_0\":{a_w}");
        }
        let mat = images.iter().position(|&i| i == pt.img).map_or(String::new(), |m| format!(",\"material\":{m}"));
        prims.push(format!("{{\"attributes\":{{{attrs}}},\"indices\":{a_idx}{mat}}}"));
    }
    let mats: Vec<String> = (0..images.len()).map(|i| format!("{{\"pbrMetallicRoughness\":{{\"baseColorTexture\":{{\"index\":{i}}},\"metallicFactor\":0}}}}")).collect();
    let texs: Vec<String> = (0..images.len()).map(|i| format!("{{\"source\":{i}}}")).collect();

    // joints: flat nodes holding the full skinning matrix (inverse bind = identity), so no shear-prone hierarchy
    let mut nodes = vec![format!("{{\"name\":\"mesh\",\"mesh\":0{}}}", if animated { ",\"skin\":0" } else { "" })];
    let mut extra = String::new();
    let mut clip_names = Vec::new();
    if animated {
        let trs = |m: &M4| decompose(&mul(&norm, m));
        for (bi, m) in skin_all(&rest).iter().enumerate() {
            let (t, r, sc) = trs(m);
            if bi >= s.bones.len() {
                let name = sockets[bi - s.bones.len()].1;
                nodes.push(format!("{{\"name\":\"{name}\",\"translation\":{t:?},\"rotation\":{r:?},\"scale\":{sc:?}}}"));
                continue;
            }
            // XBone: 2 matrices, affine string, set, bounds + mode, name
            let (d, mut q) = (x.d(s.bones[bi].0), 3 + 128);
            vi(d, &mut q);
            skip_set(d, &mut q);
            q += 20;
            nodes.push(format!("{{\"name\":\"{}\",\"translation\":{t:?},\"rotation\":{r:?},\"scale\":{sc:?}}}", x.str(vi(d, &mut q))));
        }
        let nb = s.bones.len() + sockets.len();
        let ibm: Vec<f32> = (0..nb).flat_map(|_| [1.0, 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.]).collect();
        let a_ibm = g.floats(&ibm, "MAT4", nb, false);
        let joints: Vec<String> = (1..=nb).map(|i| i.to_string()).collect();
        let mut anims = Vec::new();
        for layers in &chosen {
            let c = layers[0];
            let frames = ((c.dur * FPS).ceil() as usize).max(1) + 1;
            let times: Vec<f32> = (0..frames).map(|f| (f as f32 / FPS).min(c.dur)).collect();
            let a_t = g.floats(&times, "SCALAR", frames, true);
            let mut tv = vec![Vec::new(); nb];
            let mut rv = vec![Vec::new(); nb];
            let mut sv = vec![Vec::new(); nb];
            for &t in &times {
                let m = skin_all(&s.worlds(&s.locals(x, Some((&layers[..], base, t)))));
                for b in 0..nb {
                    let (t, mut r, sc) = trs(&m[b]);
                    // keep quaternions in one hemisphere so linear sampling doesn't spin
                    if let Some(prev) = rv[b].len().checked_sub(4).map(|i| [rv[b][i], rv[b][i + 1], rv[b][i + 2], rv[b][i + 3]]) {
                        if (0..4).map(|k| prev[k] * r[k]).sum::<f32>() < 0.0 { r = r.map(|v| -v); }
                    }
                    tv[b].extend(t);
                    rv[b].extend(r);
                    sv[b].extend(sc);
                }
            }
            let (mut samplers, mut channels) = (Vec::new(), Vec::new());
            for b in 0..nb {
                for (path, v, ty) in [("translation", &tv[b], "VEC3"), ("rotation", &rv[b], "VEC4"), ("scale", &sv[b], "VEC3")] {
                    let a = g.floats(v, ty, frames, false);
                    channels.push(format!("{{\"sampler\":{},\"target\":{{\"node\":{},\"path\":\"{path}\"}}}}", samplers.len(), b + 1));
                    samplers.push(format!("{{\"input\":{a_t},\"output\":{a},\"interpolation\":\"LINEAR\"}}"));
                }
            }
            anims.push(format!("{{\"name\":\"{}\",\"samplers\":[{}],\"channels\":[{}]}}", c.name, samplers.join(","), channels.join(",")));
            clip_names.push(format!("{} {:.2}s", c.name, c.dur));
        }
        extra = format!(",\"skins\":[{{\"joints\":[{}],\"inverseBindMatrices\":{a_ibm}}}],\"animations\":[{}]", joints.join(","), anims.join(","));
    }
    let scene_nodes: Vec<String> = (0..nodes.len()).map(|i| i.to_string()).collect();
    let json = format!(
        "{{\"asset\":{{\"version\":\"2.0\",\"generator\":\"w4m-models\"}},\"scene\":0,\"scenes\":[{{\"nodes\":[{}]}}],\"nodes\":[{}],\"meshes\":[{{\"primitives\":[{}]}}],\"materials\":[{}],\"textures\":[{}],\"images\":[{}],\"accessors\":[{}],\"bufferViews\":[{}],\"buffers\":[{{\"byteLength\":{}}}]{extra}}}",
        scene_nodes.join(","), nodes.join(","), prims.join(","), mats.join(","), texs.join(","), img_json.join(","), g.accs.join(","), g.views.join(","), g.bin.len()
    );
    let mut j = json.into_bytes();
    while j.len() % 4 != 0 { j.push(b' '); }
    while g.bin.len() % 4 != 0 { g.bin.push(0); }
    let mut o = b"glTF".to_vec();
    o.extend(2u32.to_le_bytes());
    o.extend(((12 + 8 + j.len() + 8 + g.bin.len()) as u32).to_le_bytes());
    o.extend((j.len() as u32).to_le_bytes());
    o.extend(b"JSON");
    o.extend(&j);
    o.extend((g.bin.len() as u32).to_le_bytes());
    o.extend(b"BIN\0");
    o.extend(&g.bin);
    let info = format!("{} parts, {} bones, {} images, {:.0}x{:.0}x{:.0} units -> scale {k:.4}{}{}", s.parts.len(), s.bones.len(), images.len(),
        hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2], if clip_names.is_empty() { "" } else { ", clips: " }, clip_names.join(", "));
    Some((o, info))
}

// Case-insensitive path lookup (game data uses Windows paths).
fn find_ci(dir: &Path, rel: &str) -> Option<PathBuf> {
    let mut cur = dir.to_path_buf();
    for part in rel.split(['\\', '/']).filter(|s| !s.is_empty()) {
        let e = fs::read_dir(&cur).ok()?.flatten().find(|e| e.file_name().to_string_lossy().eq_ignore_ascii_case(part))?;
        cur = e.path();
    }
    Some(cur)
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() == 3 && args[1] == "--list" {
        // debug: every XMeshDescriptor of one bundle with its parts and clips
        let b = fs::read(&args[2]).expect("read bundle");
        let x = Xom::read(&b).expect("unreadable bundle");
        for i in (1..x.c.len()).filter(|&i| x.t(i) == "XMeshDescriptor") {
            let mut p = 0;
            let name = x.str(vi(x.d(i), &mut p));
            match convert(&x, i, 0.0, false, &[]) { Some((_, info)) => println!("{name}: {info}"), None => println!("{name}: -") }
            let mut s = Scene::default();
            p += 2;
            s.walk(&x, vi(x.d(i), &mut p), None, 0);
            if s.lib != 0 { println!("  clips: {}", clips(x.d(s.lib), &x.s, &mut 0).iter().map(|c| format!("{} {:.2}s", c.name, c.dur)).collect::<Vec<_>>().join(", ")); }
            if std::env::var("W4M_GROUPS").is_ok_and(|v| v == name) {
                let rest = s.worlds(&s.locals(&x, None));
                for (gi, g) in s.groups.iter().enumerate() {
                    let n: usize = s.parts.iter().filter(|p| p.group == Some(gi)).map(|p| p.idx.len() / 3).sum();
                    println!("  {} [{}] tris {n} at {:?}", g.path, x.t(g.xf), [rest[gi][0][3], rest[gi][1][3], rest[gi][2][3]].map(|v| v.round()));
                }
            }
        }
        return;
    }
    if args.len() < 2 {
        eprintln!("usage: w4m-models <W4M install dir> [out dir = client/assets/models]");
        std::process::exit(1);
    }
    let data = find_ci(Path::new(&args[1]), "Data").unwrap_or_else(|| PathBuf::from(&args[1]));
    let out = PathBuf::from(args.get(2).map_or("client/assets/models", |s| s.as_str()));
    for d in ["hats", "frontend"] { fs::create_dir_all(out.join(d)).expect("create out dir"); }
    let mut bundles: Vec<PathBuf> = fs::read_dir(data.join("Bundles")).expect("Data/Bundles").flatten().map(|e| e.path()).collect();
    bundles.sort_by_key(|p| p.file_stem().and_then(|s| s.to_str()).and_then(|s| s.trim_start_matches(|c: char| !c.is_ascii_digit()).parse::<u32>().ok()).unwrap_or(u32::MAX));
    let mut todo: Vec<_> = MODELS.iter().collect();
    for path in bundles {
        if todo.is_empty() { break; }
        let Ok(b) = fs::read(&path) else { continue };
        let Some((s, _)) = strings(&b) else { continue };
        if !todo.iter().any(|m| s.iter().any(|x| x == m.1)) { continue; }
        let Some(x) = Xom::read(&b) else { println!("{}: unreadable", path.display()); continue };
        todo.retain(|&&(name, desc, size, feet, wanted)| {
            let Some(i) = (1..x.c.len()).find(|&i| x.t(i) == "XMeshDescriptor" && { let mut p = 0; x.str(vi(x.d(i), &mut p)) == desc }) else { return true };
            match convert(&x, i, size, feet, wanted) {
                Some((glb, info)) => {
                    fs::write(out.join(format!("{name}.glb")), &glb).expect("write glb");
                    println!("{name} ({desc}, {}): {info}, {} KB", path.file_name().unwrap().to_string_lossy(), glb.len() / 1024);
                }
                None => println!("{name} ({desc}): no geometry"),
            }
            false
        });
    }
    for m in todo { println!("{}: {} not found", m.0, m.1); }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn bezier_and_decompose() {
        // flat tangents: a 0 -> 1 ease through 0.5, held after the last key
        let k = [[1.0, 0.0, 1.0, 0.0, 0.0, 0.0], [1.0, 0.0, 1.0, 0.0, 1.0, 1.0]];
        assert!((eval(&k, 0.5) - 0.5).abs() < 1e-3);
        assert!(eval(&k, 0.0).abs() < 1e-3 && eval(&k, 2.0) == 1.0);
        let m = mul(&mul(&tr([1.0, 2.0, 3.0]), &rot([0.3, -0.5, 1.2])), &sc([2.0; 3]));
        let (t, q, s) = decompose(&m);
        assert!(t == [1.0, 2.0, 3.0] && (s[0] - 2.0).abs() < 1e-5);
        // rebuild the rotation from the quaternion and compare a column
        let [x, y, z, w] = q;
        assert!(((1.0 - 2.0 * (y * y + z * z)) * 2.0 - m[0][0]).abs() < 1e-4 && ((2.0 * (x * y + z * w)) * 2.0 - m[1][0]).abs() < 1e-4);
    }
}
