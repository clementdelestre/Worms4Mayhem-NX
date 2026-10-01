// w4m-maps <W4M dir> [out dir]: converts Worms 4 Mayhem landscapes (.xan poxels + .hmp heightmap)
// into Worms4NX maps (<name>.json + <name>.vox). Format notes: docs/w4m-formats.md.
use std::collections::{HashMap, HashSet};
use std::fs;
use std::path::{Path, PathBuf};
mod mesh;

const NX: usize = 320;
const NY: usize = 256;
const NZ: usize = 320;
const VOX: f32 = 0.25;
const BAND: f32 = 1.0; // exact distances are stored within 1 voxel of the surface (all surface nets reads)
const Q: f32 = 254.0; // engine density quantization (int8 = metres * Q)
const TEX_REPEAT: f32 = 4.0; // W4M units per texture repeat (poxel texture vectors are 0.25)
const WATER: f32 = 3.0; // our water height (m); W4M water assumed at y = 0
const HMP_EXTENT: f32 = 80.0; // .hmp covers [-80, 80] in x and z
const HMP_SCALE: f32 = 5.0; // .hmp height 0..1 -> W4M units (fitted, see docs)
const HMP_BASE: f32 = -1.5;
const MESH_UNIT: f32 = 0.05; // detail mesh units -> W4M world units (the 25-unit worm mesh is ~1.25 voxels tall)

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
fn u32le(d: &[u8], p: usize) -> u32 { d.get(p..p + 4).map_or(0, |s| u32::from_le_bytes(s.try_into().unwrap())) }
fn f32le(d: &[u8], p: usize) -> f32 { f32::from_bits(u32le(d, p)) }

struct Xom { ctn: Vec<(String, Vec<u8>)>, root: usize, s: Vec<String> }

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

// Containers are stored per type in header order, each starting with "CTNR".
fn read_xom(b: &[u8]) -> Option<Xom> {
    if b.get(0..4)? != b"MOIK" { return None; }
    let (s, start) = strings(b)?;
    let mut starts: Vec<usize> = (start..b.len().saturating_sub(3)).filter(|&i| &b[i..i + 4] == b"CTNR").collect();
    starts.push(b.len());
    let mut ctn = Vec::new();
    for t in 0..u32le(b, 24) as usize {
        let o = 64 + t * 64;
        let name = String::from_utf8_lossy(&b[o + 32..o + 64]).trim_end_matches('\0').to_string();
        for _ in 0..u32le(b, o + 8) {
            let k = ctn.len();
            if k + 1 >= starts.len() { return None; }
            ctn.push((name.clone(), b[starts[k] + 4..starts[k + 1]].to_vec()));
        }
    }
    Some(Xom { ctn, root: u32le(b, 32) as usize, s })
}

#[derive(Default)]
struct Poxel {
    pos: [f32; 3], rot: [f32; 3], scale: [f32; 3],
    l1: Vec<[f32; 2]>, l2: Vec<[f32; 2]>,
    size: [usize; 3], hm: Vec<f32>, visible: bool, vox: Vec<u32>, kids: Vec<usize>, dets: Vec<usize>,
    tex: [f32; 2], // floor X / wall Y texture vector lengths (texture repeats per local unit)
}

fn parse_poxel(d: &[u8]) -> Poxel {
    let mut p = 3;
    vi(d, &mut p); // name
    let f = |i: usize| f32le(d, p + 4 * i);
    let mut x = Poxel { pos: [f(0), f(1), f(2)], rot: [f(3), f(4), f(5)], scale: [f(6), f(7), f(8)], ..Default::default() };
    p += 36;
    if d.get(p + 0x4c).copied().unwrap_or(0) == 0 {
        // frame without voxels (root / groups): only the child list follows
        p += 0x60;
        let n = vi(d, &mut p);
        x.kids = (0..n).map(|_| vi(d, &mut p)).collect();
        return x;
    }
    let len = |i: usize| (0..3).map(|k| f32le(d, p + 4 * (i + k)).powi(2)).sum::<f32>().sqrt();
    x.tex = [len(0), len(9)];
    p += 76; // floor X/Y, wall X/Y texture vectors, floor/wall offsets, centre offset (always 0)
    for l in [&mut x.l1, &mut x.l2] {
        let n = vi(d, &mut p);
        *l = (0..n).map(|i| [f32le(d, p + 8 * i), f32le(d, p + 8 * i + 4)]).collect();
        p += 8 * n;
    }
    x.size = [d[p] as usize, d[p + 1] as usize, d[p + 2] as usize];
    p += 3;
    let n = vi(d, &mut p);
    x.hm = (0..n).map(|i| f32le(d, p + 4 * i)).collect();
    p += 4 * n;
    x.visible = d.get(p + 6) == Some(&1);
    p += 12;
    let n = vi(d, &mut p);
    x.vox = (0..n).map(|i| u32le(d, p + 4 * i)).collect();
    p += 4 * n;
    let n = vi(d, &mut p); // detail objects (DetailEntityStore refs)
    x.dets = (0..n).map(|_| vi(d, &mut p)).collect();
    let n = vi(d, &mut p);
    x.kids = (0..n).map(|_| vi(d, &mut p)).collect();
    x
}

type M4 = [[f32; 4]; 3];
fn mul(a: &M4, b: &M4) -> M4 {
    let mut r = [[0.0; 4]; 3];
    for i in 0..3 {
        for j in 0..4 {
            r[i][j] = (0..3).map(|k| a[i][k] * b[k][j]).sum::<f32>() + if j == 3 { a[i][3] } else { 0.0 };
        }
    }
    r
}
fn xform(m: &M4, p: [f32; 3]) -> [f32; 3] {
    [0, 1, 2].map(|i| m[i][0] * p[0] + m[i][1] * p[1] + m[i][2] * p[2] + m[i][3])
}
// T * Rz * Ry * Rx (* S), as XomView applies XOM euler orientations.
fn local(x: &Poxel, scaled: bool) -> M4 {
    let ([cx, cy, cz], [sx, sy, sz]) = (x.rot.map(f32::cos), x.rot.map(f32::sin));
    let r = [
        [cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx],
        [sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx],
        [-sy, cy * sx, cy * cx],
    ];
    let s = if scaled { x.scale } else { [1.0; 3] };
    [0, 1, 2].map(|i| [r[i][0] * s[0], r[i][1] * s[1], r[i][2] * s[2], x.pos[i]])
}

// Solid voxel as its 8 deformed lattice corners in W4M world space, plus theme material index.
struct Cell { c: [[f32; 3]; 8], mat: u8, tex: [f32; 2] }

// Detail entity reference with its poxel's world matrices (with / without the poxel's own scale).
struct DetRef { ctn: usize, w: M4, wn: M4 }

fn collect(px: &HashMap<usize, Poxel>, k: usize, parent: &M4, root: bool, out: &mut Vec<Cell>, dets: &mut Vec<DetRef>, depth: u32) {
    let Some(x) = px.get(&k) else { return };
    if depth > 64 { return; }
    let (w, wn) = if root { (*parent, *parent) } else { (mul(parent, &local(x, true)), mul(parent, &local(x, false))) };
    let [sx, sy, sz] = x.size;
    if x.visible && x.vox.len() == sx * sy * sz {
        let layer = |l: &Vec<[f32; 2]>, j: usize| *l.get(j).unwrap_or(&[0.0, 0.0]);
        let corner = |i: usize, j: usize, kk: usize| -> [f32; 3] {
            let (a, b) = (layer(&x.l1, j), layer(&x.l2, j));
            let px = a[0] + (sx as f32 + b[0] - a[0]) * i as f32 / sx as f32;
            let pz = a[1] + (sz as f32 + b[1] - a[1]) * kk as f32 / sz as f32;
            let hy = if j == sy { x.hm.get(kk * (sx + 1) + i).copied().unwrap_or(0.0) } else { 0.0 };
            let hy = if hy.is_finite() && hy.abs() < 64.0 { hy } else { 0.0 };
            xform(&w, [px - sx as f32 / 2.0, j as f32 + hy - sy as f32 / 2.0, pz - sz as f32 / 2.0])
        };
        for z in 0..sz {
            for xx in 0..sx {
                for y in 0..sy {
                    let v = x.vox[y + sy * (xx + sx * z)];
                    if v & 3 == 0 { continue; }
                    let mut c = [[0.0; 3]; 8];
                    for n in 0..8 { c[n] = corner(xx + (n & 1), y + ((n >> 1) & 1), z + (n >> 2)); }
                    out.push(Cell { c, mat: ((v >> 2) & 63) as u8, tex: x.tex });
                }
            }
        }
    }
    dets.extend(x.dets.iter().map(|&ctn| DetRef { ctn, w, wn }));
    for &kid in &x.kids { collect(px, kid, &wn, false, out, dets, depth + 1); }
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

struct Tex { w: usize, h: usize, rgb: Vec<u8>, avg: [u8; 3] }

// Top mip of every RGB8/ARGB8 XImage in the bundles, keyed by lowercase texture stem.
fn textures(bundles: &Path) -> HashMap<String, Tex> {
    let mut out = HashMap::new();
    let Ok(rd) = fs::read_dir(bundles) else { return out };
    for e in rd.flatten() {
        let Ok(b) = fs::read(e.path()) else { continue };
        let Some((s, start)) = strings(&b) else { continue };
        let mut i = start;
        while let Some(o) = b[i..].windows(4).position(|w| w == b"CTNR") {
            i += o + 4;
            let mut p = i + 3;
            let n = vi(&b, &mut p);
            let Some(name) = s.get(n).filter(|n| n.to_lowercase().ends_with(".tga")) else { continue };
            let (w, h) = (u16::from_le_bytes([b[p], b[p + 1]]) as usize, u16::from_le_bytes([b[p + 2], b[p + 3]]) as usize);
            let mut q = p + 8;
            q += 1 + 4 * b[q] as usize;
            q += 1 + 4 * b[q] as usize;
            let fmt = u32le(&b, q);
            q += 4;
            vi(&b, &mut q);
            let bpp = match fmt { 0 => 3, 1 | 2 => 4, _ => continue };
            let Some(px) = b.get(q..q + w * h * bpp).filter(|_| w > 0 && h > 0) else { continue };
            let rgb: Vec<u8> = px.chunks(bpp).flat_map(|c| [c[0], c[1], c[2]]).collect();
            let mut sum = [0u64; 3];
            for c in rgb.chunks(3) { for k in 0..3 { sum[k] += c[k] as u64; } }
            let stem = name.rsplit(['\\', '/']).next().unwrap().to_lowercase().trim_end_matches(".tga").to_string();
            out.insert(stem, Tex { w, h, rgb, avg: sum.map(|v| (v / (w * h) as u64) as u8) });
        }
    }
    out
}

// QOI (raylib loads it natively): small and a few dozen lines, unlike a PNG/deflate encoder.
fn qoi(t: &Tex) -> Vec<u8> {
    let mut o = b"qoif".to_vec();
    o.extend((t.w as u32).to_be_bytes());
    o.extend((t.h as u32).to_be_bytes());
    o.extend([3, 0]);
    let (mut index, mut prev, mut run) = ([[0u8; 3]; 64], [0u8; 3], 0u8);
    for px in t.rgb.chunks(3) {
        let p = [px[0], px[1], px[2]];
        if p == prev {
            run += 1;
            if run == 62 { o.push(0xbf + run); run = 0; }
            continue;
        }
        if run > 0 { o.push(0xbf + run); run = 0; }
        let h = (p[0] as usize * 3 + p[1] as usize * 5 + p[2] as usize * 7 + 255 * 11) % 64;
        if index[h] == p { o.push(h as u8); } else {
            index[h] = p;
            let [dr, dg, db] = [0, 1, 2].map(|i| p[i].wrapping_sub(prev[i]) as i8 as i32);
            let (rg, bg) = (dr - dg, db - dg);
            if [dr, dg, db].iter().all(|v| (-2..2).contains(v)) {
                o.push(0x40 | ((dr + 2) << 4 | (dg + 2) << 2 | (db + 2)) as u8);
            } else if (-32..32).contains(&dg) && (-8..8).contains(&rg) && (-8..8).contains(&bg) {
                o.extend([0x80 | (dg + 32) as u8, ((rg + 8) << 4 | (bg + 8)) as u8]);
            } else { o.extend([0xfe, p[0], p[1], p[2]]); }
        }
        prev = p;
    }
    if run > 0 { o.push(0xbf + run); }
    o.extend([0, 0, 0, 0, 0, 0, 0, 1]);
    o
}

fn theme_name(t: &str) -> &'static str {
    match t.to_uppercase().as_str() {
        "ARABIAN" => "arabian", "WILDWEST" => "wildwest", "CAMELOT" => "camelot", "PREHISTORIC" => "jurassic",
        "BUILDING" => "construction", "ARCTIC" => "arctic", "ENGLAND" => "england", "HORROR" => "horror",
        "LUNAR" => "lunar", "PIRATE" => "pirate", "WAR" => "war", _ => "",
    }
}

type V3 = [f32; 3];
fn sub(a: V3, b: V3) -> V3 { [a[0] - b[0], a[1] - b[1], a[2] - b[2]] }
fn dot(a: V3, b: V3) -> f32 { a[0] * b[0] + a[1] * b[1] + a[2] * b[2] }
fn cross(a: V3, b: V3) -> V3 { [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]] }
fn madd(a: V3, b: V3, t: f32) -> V3 { [a[0] + b[0] * t, a[1] + b[1] * t, a[2] + b[2] * t] }

// Distance from p to triangle abc (Ericson, Real-Time Collision Detection 5.1.5).
fn tri_dist(p: V3, a: V3, b: V3, c: V3) -> f32 {
    let (ab, ac, ap) = (sub(b, a), sub(c, a), sub(p, a));
    let (d1, d2) = (dot(ab, ap), dot(ac, ap));
    let q = if d1 <= 0.0 && d2 <= 0.0 { a } else {
        let bp = sub(p, b);
        let (d3, d4) = (dot(ab, bp), dot(ac, bp));
        let cp = sub(p, c);
        let (d5, d6) = (dot(ab, cp), dot(ac, cp));
        let (vc, vb, va) = (d1 * d4 - d3 * d2, d5 * d2 - d1 * d6, d3 * d6 - d5 * d4);
        if d3 >= 0.0 && d4 <= d3 { b } else if d6 >= 0.0 && d5 <= d6 { c }
        else if vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0 { madd(a, ab, d1 / (d1 - d3)) }
        else if vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0 { madd(a, ac, d2 / (d2 - d6)) }
        else if va <= 0.0 && d4 - d3 >= 0.0 && d5 - d6 >= 0.0 { madd(b, sub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6))) }
        else { let den = 1.0 / (va + vb + vc); madd(madd(a, ab, vb * den), ac, vc * den) }
    };
    let d = sub(p, q);
    dot(d, d).sqrt()
}

// Cell faces as corner cycles (corner bits: 1 = +x, 2 = +y, 4 = +z).
const FACES: [[usize; 4]; 6] = [[0, 2, 6, 4], [1, 3, 7, 5], [0, 1, 5, 4], [2, 3, 7, 6], [0, 1, 3, 2], [4, 5, 7, 6]];

// A poxel voxel in grid units, with the 12 triangle planes bounding it (outward normal, offset).
struct Hex { c: [V3; 8], mat: u8, planes: Vec<[f32; 4]>, lo: V3, hi: V3 }
impl Hex {
    fn new(c: [V3; 8], mat: u8) -> Hex {
        let cen = c.iter().fold([0.0; 3], |s, p| madd(s, *p, 0.125));
        let mut planes = Vec::new();
        for f in FACES {
            for t in [[f[0], f[1], f[2]], [f[0], f[2], f[3]]] {
                let n = cross(sub(c[t[1]], c[t[0]]), sub(c[t[2]], c[t[0]]));
                let l = dot(n, n).sqrt();
                if l < 1e-6 { continue; }
                let mut pl = [n[0] / l, n[1] / l, n[2] / l, dot(n, c[t[0]]) / l];
                if dot([pl[0], pl[1], pl[2]], cen) - pl[3] > 0.0 { pl = [-pl[0], -pl[1], -pl[2], -pl[3]]; }
                planes.push(pl);
            }
        }
        let lo = c.iter().fold([f32::MAX; 3], |m, p| [0, 1, 2].map(|i| m[i].min(p[i])));
        let hi = c.iter().fold([f32::MIN; 3], |m, p| [0, 1, 2].map(|i| m[i].max(p[i])));
        Hex { c, mat, planes, lo, hi }
    }
    fn inside(&self, p: V3) -> bool { self.planes.iter().all(|pl| pl[0] * p[0] + pl[1] * p[1] + pl[2] * p[2] - pl[3] <= 1e-4) }
}

// Grid points (integer voxel coordinates) inside the box lo..hi, clamped to the grid.
fn points(lo: V3, hi: V3) -> impl Iterator<Item = (usize, usize, usize)> {
    let r = |a: f32, b: f32, n: usize| (a.ceil().max(0.0) as usize)..((b.floor() + 1.0).clamp(0.0, n as f32) as usize);
    let (xr, yr, zr) = (r(lo[0], hi[0], NX), r(lo[1], hi[1], NY), r(lo[2], hi[2], NZ));
    zr.flat_map(move |z| { let xr = xr.clone(); yr.clone().flat_map(move |y| xr.clone().map(move |x| (x, y, z))) })
}
fn gi(x: usize, y: usize, z: usize) -> usize { (z * NY + y) * NX + x }

fn run(data: &Path, stem: &str, tex: &HashMap<String, Tex>, out_dir: &Path, written: &mut HashSet<String>, used_libs: &mut HashSet<String>) -> Result<String, String> {
    let maps = data.join("Maps");
    let xb = fs::read(maps.join(format!("{stem}.xan"))).map_err(|e| e.to_string())?;
    let xom = read_xom(&xb).ok_or("bad xom")?;
    let px: HashMap<usize, Poxel> = xom.ctn.iter().enumerate()
        .filter(|(_, (t, _))| t == "LandFrameStore").map(|(i, (_, d))| (i + 1, parse_poxel(d))).collect();
    let (mut cells, mut dets) = (Vec::new(), Vec::new());
    collect(&px, xom.root, &[[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0]], true, &mut cells, &mut dets, 0);

    // level databank: material file, theme, heightmap textures (value string precedes its key)
    // LP_/SPLP_/Multi_ variants share the databank of their base level
    let base = stem.trim_start_matches("SPLP_").trim_start_matches("LP_");
    let lvl = [stem, base, base.trim_start_matches("Multi_")].iter()
        .find_map(|n| find_ci(data, &format!("{n}.xom"))).and_then(|p| fs::read(p).ok()).and_then(|b| strings(&b).map(|s| s.0)).unwrap_or_default();
    let before = |key: &str| lvl.iter().position(|s| s == key).filter(|&i| i > 0).map(|i| lvl[i - 1].clone());
    let theme = before("Databank.Theme").unwrap_or_default();
    let matfile = before("Databank.MaterialFile").or_else(|| find_ci(&maps, &format!("{stem}.txt")).map(|_| format!("Maps\\{stem}.txt")));
    let txt = matfile.and_then(|m| find_ci(data, &m).or_else(|| find_ci(&data.join("Themes"), &m)))
        .and_then(|p| fs::read(p).ok()).map(|b| String::from_utf8_lossy(&b).into_owned()).unwrap_or_default();
    let lines: Vec<&str> = txt.lines().map(str::trim).collect();
    let find = |name: &str| Some(name.to_lowercase()).filter(|n| tex.contains_key(n));
    // 64 materials of 7 lines: top tex, side tex, ?, bump, surface sound, ?, blank; then the 2 heightmap textures
    let mut names: Vec<[Option<String>; 2]> = (0..64).map(|m| {
        let top = lines.get(m * 7).and_then(|n| find(n));
        let side = lines.get(m * 7 + 1).and_then(|n| find(n)).or(top.clone());
        [top.or(side.clone()), side]
    }).collect();
    for key in ["Heightmap.BaseTexture", "Heightmap.SecondTexture"] {
        let t = before(key).and_then(|n| find(&n));
        names.push([t.clone(), t]);
    }
    let pal: Vec<[u8; 6]> = names.iter().enumerate().map(|(m, [t, s])| {
        let def = if m >= 64 { [110, 150, 70] } else { [150, 140, 120] };
        let (t, s) = (t.as_ref().map_or(def, |n| tex[n].avg), s.as_ref().map_or(if m >= 64 { def } else { [120, 110, 100] }, |n| tex[n].avg));
        [t[0], t[1], t[2], s[0], s[1], s[2]]
    }).collect();

    let hmp = fs::read(maps.join(format!("{stem}.hmp"))).ok().filter(|b| b.len() == 50000 && b[..40000].iter().any(|&c| c != 0));
    let hval = |c: usize, r: usize| hmp.as_ref().map_or(0.0, |b| f32le(b, 4 * (r.min(99) * 100 + c.min(99))));
    // W4M world bounds of everything solid above the water (the seabed may be cropped)
    let (mut lo, mut hi) = ([f32::MAX; 3], [f32::MIN; 3]);
    let mut grow = |p: [f32; 3]| if p[1] > 0.0 { for i in 0..3 { lo[i] = lo[i].min(p[i]); hi[i] = hi[i].max(p[i]); } };
    for c in &cells { for p in &c.c { grow(*p); } }
    if hmp.is_some() {
        for r in 0..100 { for c in 0..100 {
            let h = hval(c, r);
            if h > 0.0 { grow([(c as f32 + 0.5) * 1.6 - HMP_EXTENT, h * HMP_SCALE + HMP_BASE, (r as f32 + 0.5) * 1.6 - HMP_EXTENT]); }
        } }
    }
    if lo[0] > hi[0] { return Err("no geometry above water".into()); }
    let span = [hi[0] - lo[0], hi[2] - lo[2]];
    let k = (78.0 / span[0]).min(78.0 / span[1]).min((NY as f32 * VOX - WATER - 1.0) / hi[1].max(1.0)).min(1.0);
    let (ox, oz) = (NX as f32 * VOX / 2.0 - (lo[0] + hi[0]) / 2.0 * k, NZ as f32 * VOX / 2.0 - (lo[2] + hi[2]) / 2.0 * k);
    let to_grid = |p: [f32; 3]| [(p[0] * k + ox) / VOX, (p[1] * k + WATER) / VOX, (p[2] * k + oz) / VOX];

    // heightmap surface per grid column (grid units), NaN = none
    let mut top = vec![f32::NAN; NX * NZ];
    let mut grid = vec![0u8; NX * NY * NZ];
    if let Some(hb) = &hmp {
        let mask = &hb[40000..];
        for z in 0..NZ { for x in 0..NX {
            // inverse of to_grid, then bilinear in the 100x100 heightmap
            let (wx, wz) = ((x as f32 * VOX - ox) / k, (z as f32 * VOX - oz) / k);
            let (fc, fr) = ((wx + HMP_EXTENT) / 1.6 - 0.5, (wz + HMP_EXTENT) / 1.6 - 0.5);
            if fc < 0.0 || fr < 0.0 || fc > 99.0 || fr > 99.0 { continue; }
            let (c, r, tc, tr) = (fc as usize, fr as usize, fc.fract(), fr.fract());
            let h = (hval(c, r) * (1.0 - tc) + hval(c + 1, r) * tc) * (1.0 - tr) + (hval(c, r + 1) * (1.0 - tc) + hval(c + 1, r + 1) * tc) * tr;
            let t = ((h * HMP_SCALE + HMP_BASE) * k + WATER) / VOX;
            // the coast keeps sloping under water; cut it 0.5 m down (no seabed geometry)
            if t * VOX < WATER - 0.5 { continue; }
            top[z * NX + x] = t;
            let m = if mask[r.min(99) * 100 + c.min(99)] > 127 { 66 } else { 65 };
            for y in 0..NY.min(t.max(0.0).ceil() as usize) { if (y as f32) < t { grid[gi(x, y, z)] = m; } }
        } }
    }
    let below_hm = |p: V3| {
        let (x, z) = (p[0].round(), p[2].round());
        x >= 0.0 && z >= 0.0 && (x as usize) < NX && (z as usize) < NZ && p[1] < top[z as usize * NX + x as usize]
    };

    // poxel cells in grid units, bucketed (8 voxels) for point-in-solid queries
    let hexes: Vec<Hex> = cells.iter().map(|c| Hex::new(c.c.map(to_grid), c.mat + 1))
        .filter(|h| h.hi[0] >= 0.0 && h.hi[1] >= 0.0 && h.hi[2] >= 0.0 && h.lo[0] < NX as f32 && h.lo[1] < NY as f32 && h.lo[2] < NZ as f32).collect();
    const B: usize = 8;
    let (bx, by, bz) = (NX / B, NY / B, NZ / B);
    let bidx = |p: V3| -> Option<usize> {
        let [x, y, z] = p.map(|v| (v / B as f32).floor());
        (x >= 0.0 && y >= 0.0 && z >= 0.0 && (x as usize) < bx && (y as usize) < by && (z as usize) < bz).then(|| (z as usize * by + y as usize) * bx + x as usize)
    };
    let mut buckets: Vec<Vec<u32>> = vec![Vec::new(); bx * by * bz];
    for (i, h) in hexes.iter().enumerate() {
        let r = |a: f32, b: f32, n: usize| ((a / B as f32).floor().max(0.0) as usize)..=((b / B as f32).floor().min(n as f32 - 1.0) as usize);
        for z in r(h.lo[2], h.hi[2], bz) { for y in r(h.lo[1], h.hi[1], by) { for x in r(h.lo[0], h.hi[0], bx) {
            buckets[(z * by + y) * bx + x].push(i as u32);
        } } }
    }
    let solid_at = |p: V3, skip: usize| below_hm(p) || bidx(p).map_or(false, |b| buckets[b].iter().any(|&i| i as usize != skip && hexes[i as usize].inside(p)));

    // occupancy at grid points; a cell missing every grid point (thin plank, cone tip) still claims its nearest one
    for h in &hexes {
        let mut any = false;
        for (x, y, z) in points(h.lo, h.hi) {
            if h.inside([x as f32, y as f32, z as f32]) { grid[gi(x, y, z)] = h.mat; any = true; }
        }
        let cen = h.c.iter().fold([0.0; 3], |s, p| madd(s, *p, 0.125)).map(|v| v.round());
        if !any && cen.iter().zip([NX, NY, NZ]).all(|(&v, n)| v >= 0.0 && v < n as f32) {
            grid[gi(cen[0] as usize, cen[1] as usize, cen[2] as usize)] = h.mat;
        }
    }

    // distance to the exposed faces (not covered by another cell or the heightmap), within BAND
    let mut dist = vec![BAND; NX * NY * NZ];
    let mut faces = 0;
    for (i, h) in hexes.iter().enumerate() {
        let cen = h.c.iter().fold([0.0; 3], |s, p| madd(s, *p, 0.125));
        for f in FACES {
            let q = f.map(|j| h.c[j]);
            let fc = q.iter().fold([0.0; 3], |s, p| madd(s, *p, 0.25));
            let mut n = cross(sub(q[2], q[0]), sub(q[3], q[1]));
            let l = dot(n, n).sqrt();
            if l < 1e-6 { continue; }
            if dot(n, sub(fc, cen)) < 0.0 { n = n.map(|v| -v); }
            if solid_at(madd(fc, n, 0.3 / l), i) { continue; }
            faces += 1;
            let lo = q.iter().fold([f32::MAX; 3], |m, p| [0, 1, 2].map(|i| m[i].min(p[i] - BAND)));
            let hi = q.iter().fold([f32::MIN; 3], |m, p| [0, 1, 2].map(|i| m[i].max(p[i] + BAND)));
            for (x, y, z) in points(lo, hi) {
                let p = [x as f32, y as f32, z as f32];
                let d = &mut dist[gi(x, y, z)];
                *d = d.min(tri_dist(p, q[0], q[1], q[2])).min(tri_dist(p, q[0], q[2], q[3]));
            }
        }
    }
    for z in 0..NZ { for x in 0..NX {
        let t = top[z * NX + x];
        if t.is_nan() { continue; }
        let g = |xx: usize, zz: usize| { let v = top[zz.min(NZ - 1) * NX + xx.min(NX - 1)]; if v.is_nan() { t } else { v } };
        let (gx, gz) = ((g(x + 1, z) - g(x.saturating_sub(1), z)) / 2.0, (g(x, z + 1) - g(x, z.saturating_sub(1))) / 2.0);
        let cos = 1.0 / (1.0 + gx * gx + gz * gz).sqrt();
        for y in ((t - 2.0).max(0.0) as usize)..NY.min((t + 2.0).max(0.0) as usize + 1) {
            let d = &mut dist[gi(x, y, z)];
            *d = d.min((t - y as f32).abs() * cos);
        }
    } }

    let solid = grid.iter().filter(|&&v| v != 0).count();
    if solid == 0 { return Err("empty after resampling".into()); }
    let dq = (BAND * VOX * Q).round() as i32;
    let mut vox = b"W4V2".to_vec();
    for d in [NX, NY, NZ] { vox.extend((d as u16).to_le_bytes()); }
    vox.push(dq as u8);
    // materials run-length: (value, run 1..255)
    let mut i = 0;
    while i < grid.len() {
        let mut r = 1;
        while i + r < grid.len() && r < 255 && grid[i + r] == grid[i] { r += 1; }
        vox.extend([grid[i], r as u8]);
        i += r;
    }
    // densities: skip codes (< 128) over voxels at the default +-dq, literal runs (128 + n - 1)
    let q: Vec<i8> = (0..grid.len()).map(|i| {
        let v = ((dist[i].min(BAND) * VOX * Q).round() as i32).clamp(1, dq);
        (if grid[i] != 0 { v } else { -v }) as i8
    }).collect();
    let mut i = 0;
    while i < q.len() {
        let def = |j: usize| q[j] as i32 == if grid[j] != 0 { dq } else { -dq };
        let mut r = 0;
        if def(i) {
            while i + r < q.len() && r < 127 && def(i + r) { r += 1; }
            vox.push(r as u8);
        } else {
            while i + r < q.len() && r < 128 && !def(i + r) { r += 1; }
            vox.push(127 + r as u8);
            vox.extend(q[i..i + r].iter().map(|&v| v as u8));
        }
        i += r;
    }
    fs::write(out_dir.join(format!("{stem}.vox")), &vox).map_err(|e| e.to_string())?;

    // texture repeat (m) per material and face kind: median over its cells of cell edge / texture vector
    let mut reps: Vec<[Vec<f32>; 2]> = vec![[Vec::new(), Vec::new()]; 66];
    for c in &cells {
        let e = |a: usize, b: usize| (0..3).map(|i| (c.c[a][i] - c.c[b][i]).powi(2)).sum::<f32>().sqrt();
        for (f, l) in [(0, e(0, 1)), (1, e(0, 2))] {
            if c.tex[f] > 1e-4 && l > 1e-3 { reps[c.mat as usize][f].push((l / c.tex[f] * k).clamp(0.25, 64.0)); }
        }
    }
    let median = |v: &mut Vec<f32>| if v.is_empty() { TEX_REPEAT * k } else { v.sort_by(f32::total_cmp); v[v.len() / 2] };
    // textures of the materials actually present, as tex/<stem>.qoi shared by every map
    let mut used = [false; 67];
    for &m in &grid { used[m as usize] = true; }
    fs::create_dir_all(out_dir.join("tex")).map_err(|e| e.to_string())?;
    let mut texs = Vec::new();
    for (m, pair) in names.iter().enumerate() {
        let f = pair.clone().map(|n| n.filter(|_| used[m + 1]).map(|n| {
            if written.insert(n.clone()) { let _ = fs::write(out_dir.join(format!("tex/{n}.qoi")), qoi(&tex[&n])); }
            format!("\"tex/{n}.qoi\"")
        }).unwrap_or("null".into()));
        let r = reps.get_mut(m).map_or([TEX_REPEAT * k; 2], |r| [median(&mut r[0]), median(&mut r[1])]);
        texs.push(format!("[{},{},{:.2},{:.2}]", f[0], f[1], r[0], r[1]));
    }
    // detail objects: "visible" entities whose library names a theme detail mesh (PREHISTORIC18...)
    let mut objs = Vec::new();
    for r in &dets {
        let Some((_, d)) = xom.ctn.get(r.ctn.wrapping_sub(1)).filter(|c| c.0 == "DetailEntityStore") else { continue };
        let mut p = 3;
        let (name, lib) = (xom.s.get(vi(d, &mut p)).cloned().unwrap_or_default(), xom.s.get(vi(d, &mut p)).cloned().unwrap_or_default());
        let n = name.to_lowercase();
        if !(n.starts_with("visible") || n.starts_with("visable")) { continue; }
        let f: Vec<f32> = (0..12).map(|i| f32le(d, p + 4 * i)).collect();
        let w = xform(&r.w, [f[0], f[1], f[2]]);
        let pos = [w[0] * k + ox, w[1] * k + WATER, w[2] * k + oz];
        if pos[1] < 0.0 || pos[0] < 0.0 || pos[2] < 0.0 || pos[0] > NX as f32 * VOX || pos[2] > NZ as f32 * VOX { continue; }
        // basis = poxel rotation * detail rotation * detail scale * k (row-major 3x3)
        let rs = local(&Poxel { rot: [f[3], f[4], f[5]], scale: [f[9], f[10], f[11]], ..Default::default() }, true);
        let m = mul(&r.wn, &rs);
        let b: Vec<String> = (0..9).map(|i| format!("{:.4}", m[i / 3][i % 3] * k)).collect();
        let lib = lib.to_lowercase();
        objs.push(format!("{{\"model\":\"{lib}\",\"pos\":[{:.2},{:.2},{:.2}],\"basis\":[{}]}}", pos[0], pos[1], pos[2], b.join(",")));
        used_libs.insert(lib);
    }
    let spawns = spawn_points(&grid);
    let palette: Vec<String> = pal.iter().map(|c| format!("[{}]", c.map(|v| v.to_string()).join(","))).collect();
    let json = format!(
        "{{\n  \"name\": \"{stem}\",\n  \"theme\": \"{}\",\n  \"base\": {{\"type\": \"none\"}},\n  \"voxels\": \"{stem}.vox\",\n  \"palette\": [{}],\n  \"textures\": [{}],\n  \"spawns\": [{}],\n  \"objects\": [\n    {}\n  ]\n}}\n",
        theme_name(&theme), palette.join(","), texs.join(","),
        spawns.iter().map(|p| format!("[{:.1},{:.1},{:.1}]", p[0], p[1], p[2])).collect::<Vec<_>>().join(","),
        objs.join(",\n    ")
    );
    fs::write(out_dir.join(format!("{stem}.json")), json).map_err(|e| e.to_string())?;
    Ok(format!("{} cells, {faces} faces, {} objects, scale {k:.2}, {solid} voxels, {} KB, theme {theme}, span {:.0}x{:.0}x{:.0}",
        cells.len(), objs.len(), vox.len() / 1024, span[0], hi[1] - lo[1], span[1]))
}

// Detail meshes named in `libs` (lowercase XMeshDescriptor names), written as <dir>/<name>.glb in W4M world units.
fn decor(bundles: &Path, dir: &Path, libs: &HashSet<String>) {
    let _ = fs::create_dir_all(dir);
    let mut todo = libs.clone();
    let Ok(rd) = fs::read_dir(bundles) else { return };
    let mut paths: Vec<PathBuf> = rd.flatten().map(|e| e.path()).collect();
    paths.sort();
    for path in paths {
        let Ok(b) = fs::read(&path) else { continue };
        let Some((s, _)) = strings(&b) else { continue };
        if !s.iter().any(|n| todo.contains(&n.to_lowercase())) { continue; }
        let Some(x) = mesh::Xom::read(&b) else { continue };
        for (name, i) in x.meshes() {
            let n = name.to_lowercase();
            if !todo.contains(&n) { continue; }
            match mesh::convert(&x, i, MESH_UNIT) {
                Some((glb, lo, hi)) => {
                    let _ = fs::write(dir.join(format!("{n}.glb")), &glb);
                    println!("decor {n} ({}): {:.1}..{:.1} x {:.1}..{:.1} x {:.1}..{:.1}, {} KB", path.file_name().unwrap().to_string_lossy(),
                        lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], glb.len() / 1024);
                }
                None => println!("decor {n}: no geometry"),
            }
            todo.remove(&n);
        }
    }
    let mut missing: Vec<_> = todo.into_iter().collect();
    missing.sort();
    println!("{} detail meshes, missing: {missing:?}", libs.len() - missing.len());
}

// 16 spread-out open-sky standing spots above the water: farthest-point sampling from the centre.
fn spawn_points(grid: &[u8]) -> Vec<[f32; 3]> {
    let mut cand = Vec::new();
    for z in (4..NZ - 4).step_by(6) {
        for x in (4..NX - 4).step_by(6) {
            let Some(y) = (0..NY).rev().find(|&y| grid[(z * NY + y) * NX + x] != 0) else { continue };
            // neighbours 0.5 m away within +-0.5 m, 2.5 m of headroom
            let flat = [(2, 0), (0, 2), (NX - 2, 0), (0, NZ - 2)].iter().all(|&(dx, dz)| {
                let (xx, zz) = ((x + dx) % NX, (z + dz) % NZ);
                (y.saturating_sub(2)..=(y + 2).min(NY - 1)).any(|yy| grid[(zz * NY + yy) * NX + xx] != 0)
                    && (y + 3..(y + 10).min(NY)).all(|yy| grid[(zz * NY + yy) * NX + xx] == 0)
            });
            if flat && (y + 1) as f32 * VOX > WATER + 1.5 && y + 10 < NY { cand.push([x as f32 * VOX, (y + 6) as f32 * VOX, z as f32 * VOX]); }
        }
    }
    let d2 = |a: &[f32; 3], b: &[f32; 3]| (a[0] - b[0]).powi(2) + (a[2] - b[2]).powi(2);
    let centre = [NX as f32 * VOX / 2.0, 0.0, NZ as f32 * VOX / 2.0];
    let mut out: Vec<[f32; 3]> = Vec::new();
    while out.len() < 16 && out.len() < cand.len() {
        let far = |c: &[f32; 3]| if out.is_empty() { -d2(c, &centre) } else { out.iter().map(|o| d2(c, o)).fold(f32::MAX, f32::min) };
        let best = cand.iter().copied().max_by(|a, b| far(a).total_cmp(&far(b))).unwrap();
        if !out.is_empty() && far(&best) < 1.0 { break; }
        out.push(best);
    }
    out
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 2 {
        eprintln!("usage: w4m-maps <W4M install dir> [out dir = client/assets/maps] [map stems...]");
        std::process::exit(1);
    }
    let data = find_ci(Path::new(&args[1]), "Data").unwrap_or_else(|| PathBuf::from(&args[1]));
    let out = PathBuf::from(args.get(2).map_or("client/assets/maps", |s| s.as_str()));
    fs::create_dir_all(&out).expect("create out dir");
    let tex = textures(&data.join("Bundles"));
    let (mut written, mut libs) = (HashSet::new(), HashSet::new());
    let mut stems: Vec<String> = args[3..].to_vec();
    if stems.is_empty() {
        stems = fs::read_dir(data.join("Maps")).expect("Data/Maps").flatten()
            .filter_map(|e| e.file_name().to_str().and_then(|n| n.strip_suffix(".xan")).map(String::from)).collect();
        stems.sort();
    }
    let mut ok = 0;
    for s in &stems {
        match run(&data, s, &tex, &out, &mut written, &mut libs) {
            Ok(msg) => { ok += 1; println!("{s}: {msg}"); }
            Err(e) => println!("{s}: FAILED {e}"),
        }
    }
    println!("{ok}/{} maps imported, {} textures", stems.len(), tex.len());
    decor(&data.join("Bundles"), &out.join("../models/decor"), &libs);
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn varint_and_euler() {
        let mut p = 0;
        assert_eq!((vi(&[0xb8, 0x02, 0x05], &mut p), p), (312, 2));
        // Rx(90deg) maps +y to +z
        let x = Poxel { rot: [std::f32::consts::FRAC_PI_2, 0.0, 0.0], scale: [1.0; 3], ..Default::default() };
        let q = xform(&local(&x, true), [0.0, 1.0, 0.0]);
        assert!(q[1].abs() < 1e-6 && (q[2] - 1.0).abs() < 1e-6);
    }
}
