// w4m-ui <W4M dir> [out dir]: exports Worms 4 Mayhem frontend/HUD art as PNG (loose TGAs + XImages of the UI bundles).
// XOM string/XImage reading as in tools/w4m-models (format notes: docs/w4m-formats.md).
use std::fs;
use std::path::{Path, PathBuf};

// (Data subdir of loose TGAs, output subdir)
const TGA_DIRS: &[(&str, &str)] = &[("Frontend/Levels", "levels"), ("HUD/Weapons", "weapons"), ("HUD/Flags", "flags"), ("Frontend/mechanics", "back")];
// UI bundles: all their XImages go to fe/<image name>.png
const BUNDLES: &[&str] = &["Bundl00", "Bundl06", "Bundl08", "Bundl10", "Bundl472", "Bundl03"];

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

// Every XImage of a bundle, found by scanning "CTNR" tags (bundles hold untagged types we cannot size):
// a container whose name string is a .tga and whose pixel size matches its header is an image.
fn ximages(b: &[u8]) -> Option<Vec<(String, Vec<u8>)>> {
    if b.get(0..4)? != b"MOIK" { return None; }
    let (s, start) = strings(b)?;
    let tags: Vec<usize> = b[start..].windows(4).enumerate().filter(|(_, w)| *w == b"CTNR").map(|(i, _)| start + i + 4).collect();
    let mut out = Vec::new();
    for (k, &q) in tags.iter().enumerate() {
        let d = &b[q..tags.get(k + 1).map_or(b.len(), |&e| e - 4)];
        let mut p = 3;
        let Some(name) = s.get(vi(d, &mut p)) else { continue };
        if !name.to_lowercase().ends_with(".tga") || d.len() < p + 10 { continue; }
        let (w, h) = (u16le(d, p), u16le(d, p + 2));
        let mut r = p + 8;
        r += 1 + 4 * d[r] as usize;
        let Some(&m) = d.get(r) else { continue };
        r += 1 + 4 * m as usize;
        let fmt = u32le(d, r);
        r += 4;
        let size = vi(d, &mut r);
        let want = match fmt { 0 => w * h * 3, 1 | 2 => w * h * 4, 9 => w.div_ceil(4) * h.div_ceil(4) * 8, 10 | 11 => w.div_ceil(4) * h.div_ceil(4) * 16, _ => 0 };
        if w == 0 || h == 0 || want == 0 || size < want || r + want > d.len() { continue; }
        out.push((name.clone(), d.to_vec()));
    }
    Some(out)
}

fn rgb565(c: u16) -> [u8; 3] {
    let (r, g, b) = ((c >> 11) & 31, (c >> 5) & 63, c & 31);
    [(r * 255 / 31) as u8, (g * 255 / 63) as u8, (b * 255 / 31) as u8]
}

// DXT1 (8 B/block) or DXT3/DXT5 (16 B/block: alpha first) to RGBA8.
fn dxt(w: usize, h: usize, d: &[u8], kind: u32) -> Option<Vec<u8>> {
    let bs = if kind == 1 { 8 } else { 16 };
    let mut o = vec![0u8; w * h * 4];
    let bw = w.div_ceil(4);
    for by in 0..h.div_ceil(4) {
        for bx in 0..bw {
            let b = d.get((by * bw + bx) * bs..(by * bw + bx + 1) * bs)?;
            let c = &b[bs - 8..];
            let (c0, c1) = (u16le(c, 0) as u16, u16le(c, 2) as u16);
            let (a, bb) = (rgb565(c0), rgb565(c1));
            let mix = |x: u8, y: u8, n: u16, m: u16| ((x as u16 * n + y as u16 * m) / (n + m)) as u8;
            let mut pal = [[a[0], a[1], a[2], 255], [bb[0], bb[1], bb[2], 255], [0; 4], [0; 4]];
            if c0 > c1 || kind != 1 {
                pal[2] = [mix(a[0], bb[0], 2, 1), mix(a[1], bb[1], 2, 1), mix(a[2], bb[2], 2, 1), 255];
                pal[3] = [mix(a[0], bb[0], 1, 2), mix(a[1], bb[1], 1, 2), mix(a[2], bb[2], 1, 2), 255];
            } else {
                pal[2] = [mix(a[0], bb[0], 1, 1), mix(a[1], bb[1], 1, 1), mix(a[2], bb[2], 1, 1), 255];
            }
            let bits = u32le(c, 4);
            let (a0, a1) = (b[0] as u32, b[1] as u32);
            let abits = b[2..8].iter().rev().fold(0u64, |v, &x| (v << 8) | x as u64);
            for i in 0..16 {
                let (x, y) = (bx * 4 + i % 4, by * 4 + i / 4);
                if x >= w || y >= h { continue; }
                let mut px = pal[((bits >> (2 * i)) & 3) as usize];
                if kind == 3 { px[3] = ((b[i / 2] >> (4 * (i % 2))) & 15) * 17; }
                if kind == 5 {
                    let k = ((abits >> (3 * i)) & 7) as u32;
                    px[3] = match k {
                        0 => a0, 1 => a1,
                        _ if a0 > a1 => ((8 - k) * a0 + (k - 1) * a1) / 7,
                        6 => 0, 7 => 255,
                        _ => ((6 - k) * a0 + (k - 1) * a1) / 5,
                    } as u8;
                }
                o[(y * w + x) * 4..][..4].copy_from_slice(&px);
            }
        }
    }
    Some(o)
}

// Top mip of an XImage as RGBA8.
fn image(d: &[u8]) -> Option<(usize, usize, Vec<u8>)> {
    let mut p = 3;
    vi(d, &mut p);
    let (w, h) = (u16le(d, p), u16le(d, p + 2));
    let mut q = p + 8;
    q += 1 + 4 * *d.get(q)? as usize;
    q += 1 + 4 * *d.get(q)? as usize;
    let fmt = u32le(d, q);
    q += 4;
    vi(d, &mut q);
    let px = d.get(q..)?;
    let rgba = match fmt {
        0 => px.get(..w * h * 3)?.chunks(3).flat_map(|c| [c[0], c[1], c[2], 255]).collect(),
        1 | 2 => px.get(..w * h * 4)?.to_vec(),
        9 => dxt(w, h, px, 1)?,
        10 => dxt(w, h, px, 3)?,
        11 => dxt(w, h, px, 5)?,
        _ => { eprintln!("  format {fmt} not supported"); return None }
    };
    let rgba: Vec<u8> = rgba.chunks(w * 4).rev().flatten().copied().collect();  // stored bottom row first (GL)
    Some((w, h, rgba))
}

// Uncompressed or RLE truecolour TGA (24/32 bpp) to RGBA8, top row first.
fn tga(b: &[u8]) -> Option<(usize, usize, Vec<u8>)> {
    let (ty, w, h, bpp, desc) = (*b.get(2)?, u16le(b, 12), u16le(b, 14), *b.get(16)? as usize / 8, *b.get(17)?);
    if !(ty == 2 || ty == 10) || !(bpp == 3 || bpp == 4) { return None; }
    let mut px = Vec::with_capacity(w * h * bpp);
    let mut p = 18 + *b.first()? as usize;
    while px.len() < w * h * bpp {
        if ty == 2 { px.extend_from_slice(b.get(p..p + w * h * bpp)?); break; }
        let c = *b.get(p)? as usize;
        p += 1;
        let n = (c & 0x7f) + 1;
        if c & 0x80 != 0 { let v = b.get(p..p + bpp)?; for _ in 0..n { px.extend_from_slice(v); } p += bpp; }
        else { px.extend_from_slice(b.get(p..p + n * bpp)?); p += n * bpp; }
    }
    let mut o = vec![0u8; w * h * 4];
    for y in 0..h {
        let sy = if desc & 0x20 != 0 { y } else { h - 1 - y };
        for x in 0..w {
            let s = &px[(sy * w + x) * bpp..];
            o[(y * w + x) * 4..][..4].copy_from_slice(&[s[2], s[1], s[0], if bpp == 4 { s[3] } else { 255 }]);
        }
    }
    Some((w, h, o))
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

// Case-insensitive path lookup (game data uses Windows paths).
fn find_ci(dir: &Path, rel: &str) -> Option<PathBuf> {
    let mut cur = dir.to_path_buf();
    for part in rel.split(['\\', '/']).filter(|s| !s.is_empty()) {
        let e = fs::read_dir(&cur).ok()?.flatten().find(|e| e.file_name().to_string_lossy().eq_ignore_ascii_case(part))?;
        cur = e.path();
    }
    Some(cur)
}

// Output file stem: lowercase, path stripped, spaces -> '_'.
fn stem(name: &str) -> String {
    let n = name.rsplit(['\\', '/']).next().unwrap_or(name);
    n.trim_end_matches(".tga").trim_end_matches(".TGA").to_lowercase().replace(' ', "_")
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 2 {
        eprintln!("usage: w4m-ui <W4M install dir> [out dir = client/assets/ui]");
        std::process::exit(1);
    }
    let data = find_ci(Path::new(&args[1]), "Data").unwrap_or_else(|| PathBuf::from(&args[1]));
    let out = PathBuf::from(args.get(2).map_or("client/assets/ui", |s| s.as_str()));
    let mut n = 0;
    for (src, dst) in TGA_DIRS {
        let Some(dir) = find_ci(&data, src) else { println!("{src}: missing"); continue };
        fs::create_dir_all(out.join(dst)).expect("create out dir");
        for e in fs::read_dir(dir).expect("read dir").flatten() {
            let Ok(b) = fs::read(e.path()) else { continue };
            let f = e.file_name().to_string_lossy().into_owned();
            match tga(&b) {
                Some((w, h, px)) => { fs::write(out.join(dst).join(format!("{}.png", stem(&f))), png(w, h, &px)).expect("write"); n += 1; }
                None => println!("{src}/{f}: unsupported TGA"),
            }
        }
    }
    fs::create_dir_all(out.join("fe")).expect("create out dir");
    for bundle in BUNDLES {
        let Some(b) = find_ci(&data, &format!("Bundles/{bundle}.xom")).and_then(|p| fs::read(p).ok()) else { println!("{bundle}: missing"); continue };
        for (name, d) in ximages(&b).unwrap_or_default() {
            if name.contains("ExportedTGAS") { continue; }  // hashed names: model textures
            if let Some((w, h, px)) = image(&d) {
                fs::write(out.join("fe").join(format!("{}.png", stem(&name))), png(w, h, &px)).expect("write");
                n += 1;
            }
        }
    }
    println!("{n} images -> {}", out.display());
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn tga_bottom_up_bgr() {
        let mut b = vec![0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 2, 0, 24, 0];
        b.extend([1, 2, 3, 4, 5, 6]);  // bottom row then top row, BGR
        assert_eq!(tga(&b).unwrap().2, vec![6, 5, 4, 255, 3, 2, 1, 255]);
    }
    #[test]
    fn dxt1_solid() {
        let blk = [0x00, 0xf8, 0x00, 0xf8, 0, 0, 0, 0];  // c0 = c1 = pure red, all indices 0
        assert_eq!(&dxt(4, 4, &blk, 1).unwrap()[..4], &[255, 0, 0, 255]);
    }
}
