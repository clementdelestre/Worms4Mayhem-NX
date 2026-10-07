// <stem>.cells "W4C1": the exact land per 0.25 m cell (client sharp.h, format docs/w4m/formats.md). Each cell a surface crosses
// lists the primitives that reach it: a W4M poxel cell (with the mask of its planes cutting the cell), or the heightmap patch.
use super::{cross, dot, sub, voxel, FACES, NX, NY, NZ, V3};
use std::collections::HashMap;

const HEX: u32 = 0;
const HM: u32 = 1 << 29;
const FULL: u32 = 2 << 29;
const KIND: u32 = 7 << 29;
const ID: u32 = (1 << 29) - 1;

fn cid(x: usize, y: usize, z: usize) -> usize { (z * NY + y) * NX + x }

struct Land { planes: Vec<[f32; 4]>, p0: Vec<usize>, top: Vec<f32>, lists: HashMap<u32, u32>, pool: Vec<u32> }

impl Land {
    // bilinear heightmap top (m), -1e9 none; a missing corner takes the nearest one's height
    fn hm(&self, x: f32, z: f32) -> f32 {
        let (fx, fz) = (x / voxel(), z / voxel());
        let (ix, iz) = (fx.floor() as i32, fz.floor() as i32);
        if ix < 0 || iz < 0 || ix >= NX as i32 - 1 || iz >= NZ as i32 - 1 { return -1e9; }
        let (ix, iz, tx, tz) = (ix as usize, iz as usize, fx - ix as f32, fz - iz as f32);
        let mut v = [self.top[iz * NX + ix], self.top[iz * NX + ix + 1], self.top[(iz + 1) * NX + ix], self.top[(iz + 1) * NX + ix + 1]];
        let near = v[(if tz < 0.5 { 0 } else { 2 }) + (if tx < 0.5 { 0 } else { 1 })];
        if near.is_nan() { return -1e9; }
        for a in v.iter_mut() { if a.is_nan() { *a = near; } }
        (v[0] * (1.0 - tx) + v[1] * tx) * (1.0 - tz) + (v[2] * (1.0 - tx) + v[3] * tx) * tz
    }
    // density (> 0 land, m, +-0.5) in a listed cell, as SharpLand::eval
    fn eval(&self, p: V3, c: usize) -> f32 {
        let o = &self.pool[self.lists[&(c as u32)] as usize..];
        let (n, mut s, mut i) = (o[0] as usize, -0.5f32, 1);
        while i <= n {
            let k = o[i] & KIND;
            if k == HEX {
                let p0 = self.p0[(o[i] & ID) as usize];
                i += 1;
                let (mut mask, mut m) = (o[i], 1e9f32);
                while mask != 0 {
                    let pl = self.planes[p0 + mask.trailing_zeros() as usize];
                    mask &= mask - 1;
                    let v = pl[3] - (pl[0] * p[0] + pl[1] * p[1] + pl[2] * p[2]);
                    if v < m { m = v; if m + 1e-5 <= s { break; } }
                }
                if m + 1e-5 > s { s = m + 1e-5; }
            } else if k == HM {
                s = s.max(self.hm(p[0], p[2]) - p[1]);
            }
            i += 1;
        }
        s.clamp(-0.5, 0.5)
    }
}

// hexes: poxel cell corners, grid units; top: heightmap per grid column, grid units (NaN none); q: the .vox densities,
// whose signs at grid points away from the importer's band are made exact (dq: the band's value)
pub fn build(hexes: &[[V3; 8]], top: &[f32], q: &mut [i8], dq: i8) -> (Vec<u8>, String) {
    let mut planes: Vec<[f32; 4]> = Vec::new();
    let mut boxes = Vec::new();  // per hexahedron: first plane, plane count, lo, hi (m)
    let (mut twisted, mut out) = (0, Vec::new());
    for c in hexes {
        let p = c.map(|v| v.map(|a| a * voxel()));
        let cen = p.iter().fold([0.0f32; 3], |s, v| [s[0] + v[0] * 0.125, s[1] + v[1] * 0.125, s[2] + v[2] * 0.125]);
        let lo = p.iter().fold(p[0], |m, v| [0, 1, 2].map(|i| m[i].min(v[i])));
        let hi = p.iter().fold(p[0], |m, v| [0, 1, 2].map(|i| m[i].max(v[i])));
        let p0 = planes.len();
        let mut flags = 0u32;  // the client rebuilds the planes without deciding: bit t triangle kept, 12 + t flipped, 24 twisted
        for (t, tri) in FACES.iter().flat_map(|f| [[f[0], f[1], f[2]], [f[0], f[2], f[3]]]).enumerate() {
            let n = cross(sub(p[tri[1]], p[tri[0]]), sub(p[tri[2]], p[tri[0]]));
            let l = dot(n, n).sqrt();
            if l < 1e-6 * voxel() * voxel() { continue; }
            let n = n.map(|v| v * (1.0 / l));
            let d = dot(n, p[tri[0]]);
            let flip = dot(n, cen) - d > 0.0;
            flags |= 1 << t | (flip as u32) << (12 + t);
            planes.push(if flip { [-n[0], -n[1], -n[2], -d] } else { [n[0], n[1], n[2], d] });
        }
        // a twisted cell's planes reach past its corners: bounded by its box too
        if !planes[p0..].iter().all(|pl| p.iter().all(|v| pl[0] * v[0] + pl[1] * v[1] + pl[2] * v[2] - pl[3] <= 1e-4)) {
            planes.extend([[1.0, 0.0, 0.0, hi[0]], [-1.0, 0.0, 0.0, -lo[0]], [0.0, 1.0, 0.0, hi[1]], [0.0, -1.0, 0.0, -lo[1]], [0.0, 0.0, 1.0, hi[2]], [0.0, 0.0, -1.0, -lo[2]]]);
            twisted += 1;
            flags |= 1 << 24;
        }
        for v in p { for a in v { out.extend(a.to_le_bytes()); } }
        out.extend(flags.to_le_bytes());
        boxes.push((p0, planes.len() - p0, lo, hi));
    }
    // (cell << 32 | op, plane mask): every cell each primitive reaches
    let mut pairs: Vec<(u64, u32)> = Vec::new();
    let span = |a: f32, b: f32, n: usize| ((a - 1e-4) / voxel()).floor().max(0.0) as usize..=(((b + 1e-4) / voxel()).floor() as i64).min(n as i64 - 2) as usize;
    for (h, &(p0, np, lo, hi)) in boxes.iter().enumerate() {
        if hi[0] < 0.0 || hi[1] < 0.0 || hi[2] < 0.0 { continue; }
        for z in span(lo[2], hi[2], NZ) { for y in span(lo[1], hi[1], NY) { for x in span(lo[0], hi[0], NX) {
            let mut mask = 0u32;
            let sep = planes[p0..p0 + np].iter().enumerate().any(|(k, pl)| {
                let v: Vec<f32> = (0..8).map(|j| {
                    let c = [(x + (j & 1)) as f32 * voxel(), (y + (j >> 1 & 1)) as f32 * voxel(), (z + (j >> 2)) as f32 * voxel()];
                    pl[0] * c[0] + pl[1] * c[1] + pl[2] * c[2] - pl[3]
                }).collect();
                // a plane on the cell's border is kept: the face it holds is entered from this cell
                if v.iter().any(|&a| a > -1e-5) { mask |= 1 << k; }
                v.iter().all(|&a| a > 1e-5)
            });
            if !sep { pairs.push(((cid(x, y, z) as u64) << 32 | (if mask != 0 { HEX | h as u32 } else { FULL }) as u64, mask)); }
        } } }
    }
    let top: Vec<f32> = top.iter().map(|t| t * voxel()).collect();
    for z in 0..NZ - 1 { for x in 0..NX - 1 {
        let v = [top[z * NX + x], top[z * NX + x + 1], top[(z + 1) * NX + x], top[(z + 1) * NX + x + 1]];
        let hi = v.iter().filter(|a| !a.is_nan()).fold(-1e9f32, |m, &a| m.max(a));
        if hi < -1e8 { continue; }
        let lo = if v.iter().any(|a| a.is_nan()) { -1e9 } else { v.iter().fold(1e9f32, |m, &a| m.min(a)) };
        let mut y = 0;
        while y < NY - 1 && (y as f32) * voxel() < hi {
            pairs.push(((cid(x, y, z) as u64) << 32 | (if (y + 1) as f32 * voxel() < lo { FULL } else { HM }) as u64, 0));
            y += 1;
        }
    } }
    pairs.sort_unstable_by_key(|p| p.0);
    // lists: count, then the ops (a hexahedron's followed by its mask); a cell some primitive fills is left to the .vox
    let mut land = Land { planes, p0: boxes.iter().map(|b| b.0).collect(), top, lists: HashMap::new(), pool: Vec::new() };
    let mut full = vec![0u64; (NX * NY * NZ + 63) / 64];
    let mut i = 0;
    while i < pairs.len() {
        let c = (pairs[i].0 >> 32) as usize;
        let mut j = i;
        let mut o = Vec::new();
        while j < pairs.len() && (pairs[j].0 >> 32) as usize == c {
            let op = pairs[j].0 as u32;
            if op == FULL { full[c >> 6] |= 1 << (c & 63); }
            o.push(op);
            if op & KIND == HEX { o.push(pairs[j].1); }
            j += 1;
        }
        if full[c >> 6] >> (c & 63) & 1 == 0 {
            land.lists.insert(c as u32, land.pool.len() as u32);
            land.pool.push(o.len() as u32);
            land.pool.extend(o);
        }
        i = j;
    }
    drop(pairs);
    // grid point signs: from a listed cell around it, else solid when a filled cell touches it
    let mut fixed = 0;
    for z in 0..NZ { for y in 0..NY { for x in 0..NX {
        let i = cid(x, y, z);
        if q[i] == dq || q[i] == -dq { continue; }
        let around = (0..8).filter_map(|k| {
            let (a, b, e) = (x as i64 - (k & 1), y as i64 - (k >> 1 & 1), z as i64 - (k >> 2));
            (a >= 0 && b >= 0 && e >= 0 && a < NX as i64 - 1 && b < NY as i64 - 1 && e < NZ as i64 - 1).then(|| cid(a as usize, b as usize, e as usize))
        });
        let p = [x as f32 * voxel(), y as f32 * voxel(), z as f32 * voxel()];
        let state = match around.clone().find(|c| land.lists.contains_key(&(*c as u32))) {
            Some(c) => land.eval(p, c) > 0.0,
            None => around.into_iter().any(|c| full[c >> 6] >> (c & 63) & 1 != 0),
        };
        if (q[i] > 0) != state { q[i] = if state { 1 } else { -1 }; fixed += 1; }
    } } }
    // identical lists stored once; varints: a list is its op count, then per op kind | id delta << 3 (a hexahedron's
    // delta from the list's previous one, then its mask); a cell is its index delta - 1, then its list's number
    let mut cells: Vec<(u32, u32)> = land.lists.into_iter().collect();
    cells.sort_unstable();
    let (mut ids, mut lists, mut cs, mut nl) = (HashMap::new(), Vec::new(), Vec::new(), 0u32);
    let mut prev = -1i64;
    for &(c, o) in &cells {
        let l = &land.pool[o as usize + 1..o as usize + 1 + land.pool[o as usize] as usize];
        let id = *ids.entry(l).or_insert_with(|| {
            let (mut ops, mut last, mut j) = (Vec::new(), 0, 0);
            while j < l.len() {
                let (k, id) = (l[j] >> 29, l[j] & ID);
                if k == 0 { ops.extend([k | (id - last) << 3, l[j + 1]]); last = id; j += 2; } else { ops.push(k | id << 3); j += 1; }
            }
            varint(&mut lists, l.len() as u32);
            for w in ops { varint(&mut lists, w); }
            nl += 1;
            nl - 1
        });
        varint(&mut cs, (c as i64 - prev - 1) as u32);
        varint(&mut cs, id);
        prev = c as i64;
    }
    let has_top = land.top.iter().any(|t| !t.is_nan());
    let mut b = Vec::new();
    for n in [hexes.len(), if has_top { NX * NZ } else { 0 }, cells.len(), nl as usize, lists.len(), cs.len()] { b.extend((n as u32).to_le_bytes()); }
    b.extend(out);
    if has_top { for v in &land.top { b.extend(v.to_le_bytes()); } }
    b.extend(lists);
    b.extend(cs);
    let mut f = b"W4C1".to_vec();
    f.extend((b.len() as u32).to_le_bytes());
    f.extend(deflate(&b));
    let msg = format!("{} exact cells ({} twisted hexahedra), {fixed} signs fixed, {} KB", cells.len(), twisted, f.len() / 1024);
    (f, msg)
}

// raw DEFLATE (RFC 1951): one fixed-Huffman block, greedy matches over hash chains (the client inflates it with raylib's sinflate)
fn deflate(d: &[u8]) -> Vec<u8> {
    const LBASE: [usize; 29] = [3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258];
    const LEXT: [u32; 29] = [0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0];
    const DBASE: [usize; 30] = [1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577];
    const DEXT: [u32; 30] = [0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13];
    let (mut out, mut acc, mut nb) = (Vec::new(), 0u64, 0u32);
    let mut bits = |v: usize, n: u32| {
        acc |= (v as u64) << nb;
        nb += n;
        while nb >= 8 { out.push(acc as u8); acc >>= 8; nb -= 8; }
    };
    let code = |s: usize| match s { 0..=143 => (0x30 + s, 8), 144..=255 => (0x190 + s - 144, 9), 256..=279 => (s - 256, 7), _ => (0xc0 + s - 280, 8) };
    let rev = |c: usize, n: u32| (c as u32).reverse_bits() as usize >> (32 - n);
    bits(3, 3);  // last block, fixed codes
    let hash = |i: usize| ((d[i] as usize) << 10 ^ (d[i + 1] as usize) << 5 ^ d[i + 2] as usize) & 0x7fff;
    let (mut head, mut prev) = (vec![usize::MAX; 1 << 15], vec![usize::MAX; d.len()]);
    let link = |i: usize, head: &mut Vec<usize>, prev: &mut Vec<usize>| if i + 3 <= d.len() { let h = hash(i); prev[i] = head[h]; head[h] = i; };
    let mut i = 0;
    while i < d.len() {
        let (mut best, mut dist, mut j, mut chain) = (0, 0, if i + 3 <= d.len() { head[hash(i)] } else { usize::MAX }, 64);
        while j != usize::MAX && i - j <= 32768 && chain > 0 {
            let max = 258.min(d.len() - i);
            let l = (0..max).find(|&l| d[j + l] != d[i + l]).unwrap_or(max);
            if l > best { best = l; dist = i - j; if l == max { break; } }
            j = prev[j];
            chain -= 1;
        }
        if best < 3 { best = 1; let (c, n) = code(d[i] as usize); bits(rev(c, n), n); }
        else {
            let li = LBASE.iter().rposition(|&b| b <= best).unwrap();
            let (c, n) = code(257 + li);
            bits(rev(c, n), n);
            bits(best - LBASE[li], LEXT[li]);
            let di = DBASE.iter().rposition(|&b| b <= dist).unwrap();
            bits(rev(di, 5), 5);
            bits(dist - DBASE[di], DEXT[di]);
        }
        for k in i..i + best { link(k, &mut head, &mut prev); }
        i += best;
    }
    let (c, n) = code(256);
    bits(rev(c, n), n);
    bits(0, 7);
    out
}

fn varint(out: &mut Vec<u8>, mut v: u32) {
    while v >= 128 { out.push(v as u8 | 128); v >>= 7; }
    out.push(v as u8);
}
