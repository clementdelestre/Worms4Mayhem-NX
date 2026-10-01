// w4m-ui <W4M dir> [out dir]: exports Worms 4 Mayhem frontend/HUD art as PNG (loose TGAs + XImages of the UI bundles)
// and the English / French frontend strings to <out dir>/../lang.
// XOM string/XImage reading as in tools/w4m-models (format notes: docs/w4m-formats.md).
use std::fs;
use std::path::{Path, PathBuf};

// (Data subdir of loose TGAs, output subdir)
const TGA_DIRS: &[(&str, &str)] = &[("Frontend/Levels", "levels"), ("HUD/Weapons", "weapons"), ("HUD/Flags", "flags"), ("Frontend/mechanics", "back")];
// UI bundles: all their XImages go to fe/<image name>.png
const BUNDLES: &[&str] = &["Bundl00", "Bundl06", "Bundl08", "Bundl10", "Bundl472", "Bundl03"];
// Sky/water bundles, one per (theme, time): their *Sky*/*Water* XImages go to sky/<image name>.png.
// Names embed the time as a numeric suffix (01 day, 02 evening, 03 night), e.g. C_Sky02.tga -> sky/c_sky02.png.
// In-match HUD (also holds particles): images go to hud/<image name>.png
const HUD_BUNDLE: &str = "Bundl09";
// Frontend menu art -> fe2/<out>.png: (bundle, image name, nth image of that name in the bundle, out). Most are
// textures of the menu's flat illustration meshes (WX.Mesh.SinglePlayer, NetOptions, CustomiseOptions, Options...).
const FE2: &[(&str, &str, usize, &str)] = &[
    ("Bundl10", "maya:file16/-1", 0, "paper_strip"),     // torn paper ticker strip
    ("Bundl10", "maya:file1/-1", 0, "art_local"),        // TV robot (Partie locale)
    ("Bundl10", "maya:file1/-1", 5, "art_help"),         // question mark (Aide et options)
    ("Bundl06", "maya:file7/-1", 1, "art_local_static"), // its screen noise
    ("Bundl06", "maya:file5/-1", 2, "art_network"),      // globe
    ("Bundl06", "maya:paint_bits/-1", 0, "art_myworms"), // brushes + paint (Mes Worms)
    ("Bundl474", "Nav Normal.tga", 0, "nav_normal"),     // 2x2: grenade, tick, back arrow, cross
    ("Bundl05", "maya:file8/-1", 0, "loading_worm"),    // loading screen's round worm (LoadingIcon)
];
// Frontend strings -> lang/<code>.txt ("key<TAB>value", \n = newline): (code, Data/Language/PC files)
const LANGS: &[(&str, &[&str])] = &[("en", &["EngFE.xom", "English.xom", "EngLoading.xom"]), ("fr", &["FreFE.xom", "French.xom", "FreLoading.xom"])];
const SKY_BUNDLES: &[&str] = &[
    "Bundl93", "Bundl94", "Bundl95", "Bundl96", "Bundl97", "Bundl98", "Bundl99", "Bundl100", "Bundl101", "Bundl102",
    "Bundl103", "Bundl104", "Bundl105", "Bundl106", "Bundl107", "Bundl108", "Bundl109", "Bundl110", "Bundl111", "Bundl112",
    "Bundl113", "Bundl114", "Bundl115", "Bundl116", "Bundl117", "Bundl118", "Bundl119", "Bundl120", "Bundl121", "Bundl122",
    "Bundl123", "Bundl124", "Bundl125",
];

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

// Every XImage of a bundle (.tga or Maya "maya:fileN/-1" names), found by trying each byte offset as a container
// (many are untagged): its name string, header and pixel size must all match. Matches are skipped whole.
fn ximages(b: &[u8]) -> Option<Vec<(String, Vec<u8>)>> {
    if b.get(0..4)? != b"MOIK" { return None; }
    let (s, start) = strings(b)?;
    let at = |d: &[u8]| -> Option<(String, usize)> {
        let mut p = 3;
        let name = s.get(vi(d, &mut p))?;
        if !(name.to_lowercase().ends_with(".tga") || name.starts_with("maya:")) { return None; }
        let (w, h) = (u16le(d, p), u16le(d, p + 2));
        let mut r = p + 8;
        let ns = *d.get(r)? as usize;
        r += 1 + 4 * ns;
        let m = *d.get(r)? as usize;
        r += 1 + 4 * m;
        let fmt = u32le(d, r);
        r += 4;
        let size = vi(d, &mut r);
        let want = match fmt { 0 => w * h * 3, 1 | 2 => w * h * 4, 9 => w.div_ceil(4) * h.div_ceil(4) * 8, 10 | 11 => w.div_ceil(4) * h.div_ceil(4) * 16, _ => 0 };
        let ok = w > 0 && h > 0 && w <= 4096 && h <= 4096 && (1..=16).contains(&ns) && (1..=16).contains(&m);
        (ok && want > 0 && size >= want && size <= 2 * want && r + size <= d.len()).then(|| (name.clone(), r + size))
    };
    let (mut out, mut q) = (Vec::new(), start);
    while q + 16 < b.len() {
        match at(&b[q..]) { Some((n, end)) => { out.push((n, b[q..q + end].to_vec())); q += end; } None => q += 1 }
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

type Glyph = (u32, usize, usize, Vec<u8>, i32, i32, i32);  // cp, w, h, rgba, xoff, yoff, adv

// Two copies of glyph g shrunk to sw x sh (nearest), the second shifted right by ~half a chevron.
fn guillemet(cp: u32, g: &Glyph, sw: usize, sh: usize) -> Glyph {
    let step = sw * 11 / 20;
    let w = sw + step;
    let mut rgba = vec![0u8; w * sh * 4];
    for k in 0..2 {
        for y in 0..sh {
            for x in 0..sw {
                let s = &g.3[((y * g.2 / sh) * g.1 + x * g.1 / sw) * 4..][..4];
                let d = &mut rgba[(y * w + x + k * step) * 4..][..4];
                if s[3] > d[3] { d.copy_from_slice(s); }
            }
        }
    }
    (cp, w, sh, rgba, g.4, g.5 + (g.2 - sh) as i32 / 2, g.6 + w as i32 - g.1 as i32)
}

// FE.Font (Bundl03) -> BMFont text + one PNG atlas (Latin glyphs only). Format: docs/w4m-formats.md.
const FONT_EM: f32 = 50.0;  // atlas px per em
fn font(b: &[u8]) -> Option<(String, usize, usize, Vec<u8>)> {
    let nt = u32le(b, 24) as usize;
    let count = |t: usize| u32le(b, 64 + t * 64 + 8) as usize;
    let name = |t: usize| { let s = &b[64 + t * 64 + 32..64 + t * 64 + 64]; String::from_utf8_lossy(&s[..s.iter().position(|&c| c == 0).unwrap_or(32)]).into_owned() };
    let ty = (0..nt).find(|&t| name(t) == "XMultiTexFontPage")?;
    let objects: usize = (0..nt).map(count).sum();
    let (_, start) = strings(b)?;
    let tags: Vec<usize> = b[start..].windows(4).enumerate().filter(|(_, w)| *w == b"CTNR").map(|(i, _)| start + i + 4).collect();
    let skip = objects - tags.len();  // leading objects without a CTNR tag; refs are 1-based object indices
    let obj = |r: usize| -> Option<&[u8]> { let k = r.checked_sub(1 + skip)?; Some(&b[*tags.get(k)?..tags.get(k + 1).map_or(b.len(), |&e| e - 4)]) };
    let first: usize = (0..ty).map(count).sum();
    let (pad, line, base) = (8usize, 56i32, 42i32);
    let mut glyphs: Vec<Glyph> = Vec::new();
    for r in first + 1..=first + count(ty) {
        let d = obj(r)?;
        let mut p = 3;
        let texmap = obj(vi(d, &mut p))?;
        let (w, _, px) = image(obj(vi(texmap, &mut 23))?)?;
        let n = vi(d, &mut p);
        let cps: Vec<u32> = (0..n).map(|i| u16le(d, p + 2 * i) as u32).collect();
        p += 2 * n;
        let mut arr = |k: usize| { let c = vi(d, &mut p); let v: Vec<f32> = (0..c * k).map(|i| f32::from_bits(u32le(d, p + 4 * i))).collect(); p += 4 * c * k; v };
        let (uv, sz, ctr, adv) = (arr(2), arr(2), arr(2), arr(1));
        for (i, &cp) in cps.iter().enumerate() {
            let keep = (32..0x250).contains(&cp) || (0x2010..=0x203a).contains(&cp) || cp == 0x20ac || cp == 0x2122;
            if !keep || glyphs.iter().any(|g| g.0 == cp) || i >= adv.len() { continue; }
            let (x, y) = ((uv[2 * i] * 512.0).round() as usize + pad, (uv[2 * i + 1] * 512.0).round() as usize + pad);
            let (gw, gh) = (((sz[2 * i] * 512.0).round() as usize).saturating_sub(2 * pad).max(1), ((sz[2 * i + 1] * 512.0).round() as usize).saturating_sub(2 * pad).max(1));
            if x + gw > w || y + gh > px.len() / (w * 4) || gh > 64 { continue; }  // taller: pad button icons on ½¼»...
            let rgba: Vec<u8> = (0..gh).flat_map(|r| px[((y + r) * w + x) * 4..((y + r) * w + x + gw) * 4].iter().copied()).collect();
            // centre (em, y up from the baseline) -> BMFont offsets from the line top
            let xo = (ctr[2 * i] * FONT_EM - gw as f32 / 2.0).round() as i32;
            let yo = base - (ctr[2 * i + 1] * FONT_EM + gh as f32 / 2.0).round() as i32;
            glyphs.push((cp, gw, gh, rgba, xo, yo, (adv[i] * FONT_EM).round() as i32));
        }
    }
    if glyphs.is_empty() { return None; }
    // « » hold pad icons: build them from two 3/4-size '<' / '>'
    for (cp, src) in [(0xab, 0x3c), (0xbb, 0x3e)] {
        let Some(g) = glyphs.iter().find(|g| g.0 == src).cloned() else { continue };
        glyphs.push(guillemet(cp, &g, g.1 * 3 / 4, g.2 * 3 / 4));
    }
    // shelf packing, 4 px gaps (mipmapped)
    let aw = 1024;
    let (mut x, mut y, mut row) = (0, 0, 0);
    let mut at = Vec::new();
    for g in &glyphs {
        if x + g.1 > aw { x = 0; y += row + 4; row = 0; }
        at.push((x, y));
        x += g.1 + 4;
        row = row.max(g.2);
    }
    let ah = (y + row).next_power_of_two();
    let mut atlas = vec![0u8; aw * ah * 4];
    let mut fnt = format!("info face=\"W4M FE.Font\" size={line}\ncommon lineHeight={line} base={base} scaleW={aw} scaleH={ah} pages=1\npage id=0 file=\"w4m.png\"\nchars count={}\n", glyphs.len());
    for (g, &(x, y)) in glyphs.iter().zip(&at) {
        for r in 0..g.2 { atlas[((y + r) * aw + x) * 4..][..g.1 * 4].copy_from_slice(&g.3[r * g.1 * 4..][..g.1 * 4]); }
        fnt += &format!("char id={} x={x} y={y} width={} height={} xoffset={} yoffset={} xadvance={} page=0\n", g.0, g.1, g.2, g.4, g.5, g.6);
    }
    Some((fnt, aw, ah, atlas))
}

// The stored paper is blue; W4M shows its popups teal (#1e5a6e body).
fn teal(px: &mut [u8]) {
    for c in px.chunks_mut(4) {
        let (g, b) = (c[1] as f32, c[2] as f32);
        c[0] = (c[0] as f32 + b * 0.23).min(255.0) as u8;
        c[1] = (g * 0.96) as u8;
        c[2] = (b * 0.84) as u8;
    }
}

// XStringResourceDetails: CTNR + 3 header bytes, varint value, varint key (UTF-8; "/*NL*/" = newline).
fn lang(b: &[u8]) -> Option<Vec<(String, String)>> {
    let (s, start) = strings(b)?;
    let mut out = Vec::new();
    for (i, w) in b[start..].windows(4).enumerate() {
        if w != b"CTNR" { continue; }
        let mut p = start + i + 7;
        let (v, k) = (vi(b, &mut p), vi(b, &mut p));
        let (Some(v), Some(k)) = (s.get(v), s.get(k)) else { continue };
        out.push((k.clone(), v.replace("/*NL*/", "\\n").replace('\n', "\\n").replace(['\u{a0}', '\t', '\r'], " ")));
    }
    Some(out)
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
    fs::create_dir_all(out.join("sky")).expect("create out dir");
    fs::create_dir_all(out.join("hud")).expect("create out dir");
    for bundle in BUNDLES.iter().chain(SKY_BUNDLES).chain([&HUD_BUNDLE]) {
        let sky = SKY_BUNDLES.contains(bundle);
        let dir = if sky { "sky" } else if *bundle == HUD_BUNDLE { "hud" } else { "fe" };
        let Some(b) = find_ci(&data, &format!("Bundles/{bundle}.xom")).and_then(|p| fs::read(p).ok()) else { println!("{bundle}: missing"); continue };
        if let Some((fnt, w, h, px)) = font(&b) {
            fs::create_dir_all(out.join("font")).expect("create out dir");
            fs::write(out.join("font/w4m.fnt"), fnt).expect("write");
            fs::write(out.join("font/w4m.png"), png(w, h, &px)).expect("write");
            println!("{bundle}: FE.Font -> font/w4m.fnt");
        }
        for (name, d) in ximages(&b).unwrap_or_default() {
            if name.contains("ExportedTGAS") || name.starts_with("maya:") { continue; }  // model textures
            let l = name.to_lowercase();
            if sky && !(l.contains("sky") || l.contains("water")) { continue; }
            if let Some((w, h, mut px)) = image(&d) {
                if l.contains("paperpopup") { teal(&mut px); }
                fs::write(out.join(dir).join(format!("{}.png", stem(&name))), png(w, h, &px)).expect("write");
                n += 1;
            }
        }
    }
    fs::create_dir_all(out.join("fe2")).expect("create out dir");
    let mut last: (&str, Vec<(String, Vec<u8>)>) = ("", Vec::new());
    for &(bundle, name, nth, dst) in FE2 {
        if last.0 != bundle {
            let b = find_ci(&data, &format!("Bundles/{bundle}.xom")).and_then(|p| fs::read(p).ok()).unwrap_or_default();
            last = (bundle, ximages(&b).unwrap_or_default());
        }
        match last.1.iter().filter(|(nm, _)| nm == name).nth(nth).and_then(|(_, d)| image(d)) {
            Some((w, h, px)) => { fs::write(out.join(format!("fe2/{dst}.png")), png(w, h, &px)).expect("write"); n += 1; }
            None => println!("{bundle} {name} #{nth}: missing"),
        }
    }
    fs::create_dir_all(out.join("../lang")).expect("create out dir");
    for (code, files) in LANGS {
        let mut txt = String::new();
        for f in *files {
            let Some(b) = find_ci(&data, &format!("Language/PC/{f}")).and_then(|p| fs::read(p).ok()) else { println!("{f}: missing"); continue };
            for (k, v) in lang(&b).unwrap_or_default() { txt += &format!("{k}\t{v}\n"); }
        }
        fs::write(out.join(format!("../lang/{code}.txt")), txt).expect("write");
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
    fn lang_string() {
        let mut b = b"MOIK".to_vec();
        b.resize(80, 0);
        b.extend(b"STRS");
        for v in [2u32, 17, 0, 10] { b.extend(v.to_le_bytes()); }
        b.extend(b"Oui/*NL*/\0FE.Yes\0CTNR\0\0\0\x00\x01");
        assert_eq!(lang(&b).unwrap(), vec![("FE.Yes".to_string(), "Oui\\n".to_string())]);
    }
    #[test]
    fn dxt1_solid() {
        let blk = [0x00, 0xf8, 0x00, 0xf8, 0, 0, 0, 0];  // c0 = c1 = pure red, all indices 0
        assert_eq!(&dxt(4, 4, &blk, 1).unwrap()[..4], &[255, 0, 0, 255]);
    }
}
