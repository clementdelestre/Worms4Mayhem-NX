// XAnimClipLibrary reader and the exe's key curve, trimmed copy of tools/w4m-models (decor "Go" / "GoSync" clips).
use crate::{u32le, vi};
use std::collections::HashMap;

fn u16le(d: &[u8], p: usize) -> usize { d.get(p..p + 2).map_or(0, |s| u16::from_le_bytes([s[0], s[1]]) as usize) }
fn f32le(d: &[u8], p: usize) -> f32 { f32::from_bits(u32le(d, p)) }
fn fl<const N: usize>(d: &[u8], p: usize) -> [f32; N] { std::array::from_fn(|i| f32le(d, p + 4 * i)) }

// in-tangent x, y, out-tangent x, y, time, value; [6]: the channel's runtime flags (2 weighted, 4 static), pre / post infinity << 8 / << 11
pub type Key = [f32; 7];
pub struct Clip { pub name: String, pub dur: f32, pub ch: HashMap<(String, u32), Vec<Key>> }

// XAnimClipLibrary: key types (u32 type, object) then clips (duration, name, channels of keys).
pub fn clips(b: &[u8], s: &[String], p: &mut usize) -> Vec<Clip> {
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
        let n = u16le(b, *p);
        let expanded = n == 256 || n == 257;
        let n = if expanded { nk } else { *p += 4; u32le(b, *p - 4) as usize };
        let mut ch = HashMap::new();
        for k in 0..n {
            if u16le(b, *p) == 256 { *p += 16; continue; }
            let fl4 = (if b.get(*p + 2) == Some(&0) { 0.0 } else { 4.0 }) + (if b.get(*p + 3) == Some(&0) { 0.0 } else { 2.0 });
            *p += 4;
            let ki = if expanded { k } else { *p += 2; u16le(b, *p - 2) };
            let inf = (u32le(b, *p) & 7) << 8 | (u32le(b, *p + 4) & 7) << 11;
            *p += 8;
            let fl4 = fl4 + inf as f32;
            let nkf = u32le(b, *p) as usize;
            *p += 4;
            let kf: Vec<Key> = (0..nkf).map(|i| { let k = fl::<6>(b, *p + 24 * i); [k[0], k[1], k[2], k[3], k[4], k[5], fl4] }).collect();
            *p += 24 * nkf;
            if let Some(key) = keys.get(ki) { ch.insert(key.clone(), kf); }
        }
        out.push(Clip { name, dur, ch });
    }
    out
}

// The exe's key curve (Maya engine, 0x7abb1c; keys from loader 0x7b01ec): a zero out-tangent holds the key (step), a static
// channel keeps its first value; unweighted channels are Hermite on the tangents' slopes (0x7aa7df), weighted ones Bezier with
// handles at key +- tangent / 3, x kept monotonic (0x7ab931, 0x7ab6f1, 0x7aa8f4).
// Outside its keys a channel follows its infinity (0x7abb1c -> 0x7ab3d1, Maya's engine): 1 linear (the end tangent), 2 cycle,
// 3 cycle relative (offset by the value range per cycle), 4 oscillate; 0 holds the end key
pub fn eval(k: &[Key], t: f32) -> f32 {
    let (f, n) = (k[0][6] as u32, k.len() - 1);
    let (t0, t1, pre) = (k[0][4], k[n][4], t < k[0][4]);
    let ty = if pre { f >> 8 & 7 } else if t > t1 { f >> 11 & 7 } else { 0 };
    if ty == 0 { return curve(k, t); }
    let range = t1 - t0;
    if range == 0.0 { return k[0][5]; }
    if ty == 1 {
        let (e, dt) = if pre { (k[0], t0 - t) } else { (k[n], t - t1) };
        let (tx, tyv) = if pre { (e[0], e[1]) } else { (e[2], e[3]) };
        return if tx != 0.0 { e[5] + if pre { -dt * tyv / tx } else { dt * tyv / tx } } else { e[5] };
    }
    let x = (t - if pre { t0 } else { t1 }) / range;
    let (rem, cycles) = ((x - x.trunc()).abs(), x.trunc().abs() + 1.0);
    let ft = range * rem;
    let odd = (cycles / 2.0).fract() != 0.0;
    let at = match (ty, pre) {
        (4, true) => if odd { t0 + ft } else { t1 - ft },
        (4, false) => if odd { t1 - ft } else { t0 + ft },
        (2 | 3, true) => t1 - ft,
        (2 | 3, false) => t0 + ft,
        _ => ft,
    };
    let v = curve(k, at);
    if ty == 3 { v + if pre { -1.0 } else { 1.0 } * cycles * (k[n][5] - k[0][5]) } else { v }
}

fn curve(k: &[Key], t: f32) -> f32 {
    let n = k.len() - 1;
    let i = if t < k[0][4] { 0 } else { (0..n).find(|&j| k[j][4] <= t && t < k[j + 1][4]).unwrap_or(n) };
    let (a, b) = (k[i], k[(i + 1).min(n)]);
    if k[0][6] as u32 & 4 != 0 { return k[0][5]; }
    if t < k[0][4] || i == n || a[4] == b[4] || (a[2] == 0.0 && a[3] == 0.0) { return a[5]; }
    let dx = b[4] - a[4];
    if k[0][6] as u32 & 2 == 0 {
        let slope = |x: f32, y: f32| if x != 0.0 { y / x } else { 5.72958e6 };  // vertical: the exe's big slope (0x8b58b0)
        let (m0, m1, dy, s) = (slope(a[2], a[3]), slope(b[0], b[1]), b[5] - a[5], t - a[4]);
        let c3 = (m0 * dx + m1 * dx - 2.0 * dy) / (dx * dx * dx);
        let c2 = (3.0 * dy - 2.0 * m0 * dx - m1 * dx) / (dx * dx);
        return ((c3 * s + c2) * s + m0) * s + a[5];
    }
    let (o1, o2) = ((a[2] / 3.0) / dx, 1.0 - (b[0] / 3.0) / dx);
    let (mut u1, mut u2) = (o1.max(0.0), o2.min(1.0));
    if u1 > 1.0 || u2 < 0.0 { monotonic(&mut u1, &mut u2); }
    let (mut y1, mut y2) = (a[5] + a[3] / 3.0, b[5] - b[1] / 3.0);
    if u1 != o1 && o1 != 0.0 { y1 = a[5] + (y1 - a[5]) * u1 / o1; }  // the handle keeps its slope (0x7aba16)
    if u2 != o2 && o2 != 1.0 { y2 = b[5] - (b[5] - y2) * (1.0 - u2) / (1.0 - o2); }
    let (x1, x2) = (a[4] + u1 * dx, a[4] + u2 * dx);
    let bz = |u: f32, p0: f32, p1: f32, p2: f32, p3: f32| {
        let v = 1.0 - u;
        v * v * v * p0 + 3.0 * u * v * v * p1 + 3.0 * u * u * v * p2 + u * u * u * p3
    };
    let (mut lo, mut hi) = (0.0, 1.0);
    let mut u = 0.5;
    for _ in 0..30 {
        u = (lo + hi) / 2.0;
        let x = bz(u, a[4], x1, x2, b[4]);
        if (x - t).abs() < 1e-6 { break; }
        if x > t { hi = u; } else { lo = u; }
    }
    bz(u, a[5], y1, y2, b[5])
}

// checkMonotonic / constrainInsideBounds (0x7ab6f1, 0x7aa8f4): handle fractions u1, u2 of the segment, x(u) kept increasing
fn monotonic(u1: &mut f32, u2: &mut f32) {
    let eps = f32::EPSILON;
    let mut x2 = (1.0 - *u2).max(0.0);
    *u1 = u1.max(0.0);
    if (*u1 > 1.0 || x2 > 1.0) && *u1 * (*u1 - 2.0 + x2) + x2 * (x2 - 2.0) + 1.0 + eps > 0.0 {
        if *u1 + eps < 4.0 / 3.0 {
            let (bt, ct) = (*u1 - 2.0, *u1 - 1.0);
            let d = (bt * bt - 4.0 * ct * ct).max(0.0).sqrt();
            let t = (d - bt) * 0.5;
            if x2 + eps > t { x2 = t - eps; } else {
                let t = (-bt - d) * 0.5;
                if x2 < t + eps { x2 = t + eps; }
            }
        } else {
            *u1 = 4.0 / 3.0 - eps;
            x2 = 1.0 / 3.0 - eps;
        }
    }
    *u2 = 1.0 - x2;
}
