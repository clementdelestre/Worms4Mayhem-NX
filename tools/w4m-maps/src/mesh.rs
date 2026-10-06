// Static XOM mesh -> glTF binary, for map detail objects. Trimmed copy of tools/w4m-models (no skins, no clips,
// no normalisation: vertices stay in mesh units around the model origin). Format notes: docs/w4m-formats.md.
use crate::{strings, u32le, vi};

fn u16le(d: &[u8], p: usize) -> usize { d.get(p..p + 2).map_or(0, |s| u16::from_le_bytes([s[0], s[1]]) as usize) }
fn f32le(d: &[u8], p: usize) -> f32 { f32::from_bits(u32le(d, p)) }
fn fl<const N: usize>(d: &[u8], p: usize) -> [f32; N] { std::array::from_fn(|i| f32le(d, p + 4 * i)) }
fn skip_set(d: &[u8], p: &mut usize) { for _ in 0..vi(d, p) { vi(d, p); } }

// End of a container for types sized by their content (XomView ReadXContainer); None = runs to the next "CTNR".
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
        "XGraphSet" => { for _ in 0..v(&mut p) { p += 16; v(&mut p); v(&mut p); } p }
        "XAnimClipLibrary" => { skip_clips(b, &mut p); p }
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

// XAnimClipLibrary layout (see w4m-models `clips`), only walked to find its end.
fn skip_clips(b: &[u8], p: &mut usize) {
    vi(b, p);
    let nk = u32le(b, *p) as usize;
    *p += 4;
    for _ in 0..nk { *p += 4; vi(b, p); }
    let nc = u32le(b, *p) as usize;
    *p += 4;
    for _ in 0..nc {
        *p += 4;
        vi(b, p);
        let n = u16le(b, *p);
        let expanded = n == 256 || n == 257;
        let n = if expanded { nk } else { *p += 4; u32le(b, *p - 4) as usize };
        for _ in 0..n {
            if u16le(b, *p) == 256 { *p += 16; continue; }
            *p += 4 + if expanded { 0 } else { 2 } + 8;
            let nkf = u32le(b, *p) as usize;
            *p += 4 + 24 * nkf;
        }
    }
}

pub struct Xom { s: Vec<String>, c: Vec<(String, Vec<u8>)> } // c[0] = null reference

impl Xom {
    pub fn read(b: &[u8]) -> Option<Xom> {
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
    // XMeshDescriptor names -> container index
    pub fn meshes(&self) -> Vec<(String, usize)> {
        (1..self.c.len()).filter(|&i| self.t(i) == "XMeshDescriptor").map(|i| { let mut p = 0; (self.str(vi(self.d(i), &mut p)), i) }).collect()
    }
}

type M4 = [[f32; 4]; 4];
const ID: M4 = [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]];
fn mul(a: &M4, b: &M4) -> M4 { std::array::from_fn(|i| std::array::from_fn(|j| (0..4).map(|k| a[i][k] * b[k][j]).sum())) }
fn tr(v: [f32; 3]) -> M4 { let mut m = ID; for i in 0..3 { m[i][3] = v[i]; } m }
fn sc(v: [f32; 3]) -> M4 { let mut m = ID; for i in 0..3 { m[i][i] = v[i]; } m }
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

// XSimpleShader render attributes of a part (XBlendModeGL, XAlphaTest, XZBufferWriteEnable, XCullFace, XLightingEnable,
// XMaterial emissive); absent ones stay at -1 / default.
#[derive(Clone, Copy, PartialEq)]
struct State { blend: [i32; 2], test: [i32; 2], zwrite: bool, cull: i32, lit: bool, emit: [u8; 3] }
impl Default for State { fn default() -> Self { State { blend: [-1; 2], test: [-1; 2], zwrite: true, cull: -1, lit: true, emit: [0; 3] } } }

struct Part { pos: Vec<[f32; 3]>, nrm: Vec<[f32; 3]>, uv: Vec<[f32; 2]>, idx: Vec<u16>, img: usize, st: State, node: String, world: M4 }

// Static scene walk: rest-pose world matrix per shape (texture-animation selectors keep their first child).
fn walk(x: &Xom, i: usize, w: M4, out: &mut Vec<Part>, depth: u32, group: &str) {
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
                walk(x, r, w, out, depth + 1, group);
            }
        }
        "XInteriorNode" => for r in refs(&mut p) { walk(x, r, w, out, depth + 1, group) },
        "XGroup" | "XSkeletonRoot" | "XBinModifier" => {
            let bin = x.t(i) == "XBinModifier";
            if bin { p += 2; }
            let xf = vi(d, &mut p);
            let mut kids = refs(&mut p);
            p += 20;
            let name = if bin { group.to_string() } else { x.str(vi(d, &mut p)) };
            if x.t(xf) == "XChildSelector" { kids.truncate(1); }
            let w = mul(&w, &local(x, xf));
            for r in kids { walk(x, r, w, out, depth + 1, &name); }
        }
        "XSkin" => { let root = vi(d, &mut p); walk(x, root, w, out, depth + 1, group); for r in refs(&mut p) { walk(x, r, w, out, depth + 1, group); } }
        "XShape" => { p += 4; let (sh, geo) = (vi(d, &mut p), vi(d, &mut p)); part(x, sh, geo, w, group, out); }
        "XSkinShape" => { skip_set(d, &mut p); p += 4; let (sh, geo) = (vi(d, &mut p), vi(d, &mut p)); part(x, sh, geo, w, group, out); }
        _ => {}
    }
}

fn local(x: &Xom, xf: usize) -> M4 {
    let d = x.d(xf);
    match x.t(xf) {
        "XJointTransform" => {
            let f = fl::<15>(d, 3);
            let (ro, r, pos, jo, s) = ([f[0], f[1], f[2]], [f[3], f[4], f[5]], [f[6], f[7], f[8]], [f[9], f[10], f[11]], [f[12], f[13], f[14]]);
            mul(&mul(&mul(&mul(&tr(pos), &rot(r)), &rot(jo)), &rot(ro)), &sc(s))
        }
        "XTransform" => { let f = fl::<9>(d, 3); mul(&mul(&tr([f[0], f[1], f[2]]), &rot([f[3], f[4], f[5]])), &sc([f[6], f[7], f[8]])) }
        "XMatrix" => {
            let f = fl::<12>(d, 3);
            let mut m = ID;
            for c in 0..4 { for r in 0..3 { m[r][c] = f[c * 3 + r]; } }
            m
        }
        _ => ID,
    }
}

fn part(x: &Xom, shader: usize, geo: usize, world: M4, shape: &str, out: &mut Vec<Part>) {
    if x.t(geo) != "XIndexedTriangleSet" { return; }
    let d = x.d(geo);
    let mut p = 3;
    let iset = vi(d, &mut p);
    p += 8;
    let [cs, ns, _, ts, _] = [0; 5].map(|_| vi(d, &mut p));
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
    let (ni, q) = arr(iset, 2);
    let idx: Vec<u16> = (0..ni).map(|i| u16le(x.d(iset), q + 2 * i) as u16).filter(|&v| (v as usize) < n).collect();
    if x.t(iset) != "XIndexSet" || idx.len() != ni || ni % 3 != 0 { return; }
    let sd = x.d(shader);
    let mut sp = 3;
    let stages: Vec<usize> = if x.t(shader) == "XSimpleShader" { (0..vi(sd, &mut sp)).map(|_| vi(sd, &mut sp)).collect() } else { vec![] };
    let img = stages.first().filter(|&&s| x.t(s) == "XOglTextureMap").map_or(0, |&s| { let mut q = 23; vi(x.d(s), &mut q) });
    let mut st = State::default();
    if x.t(shader) == "XSimpleShader" {
        for _ in 0..vi(sd, &mut sp) {
            let a = vi(sd, &mut sp);
            let d = x.d(a);
            match x.t(a) {
                "XBlendModeGL" => st.blend = [u32le(d, 3) as i32, u32le(d, 7) as i32],
                "XAlphaTest" if d.get(3) == Some(&1) => st.test = [u32le(d, 4) as i32, f32le(d, 8).round() as i32],
                "XZBufferWriteEnable" => st.zwrite = d.get(3) == Some(&1),
                "XCullFace" => st.cull = u32le(d, 3) as i32,
                "XLightingEnable" => st.lit = d.get(3) == Some(&1),
                "XMaterial" => st.emit = fl::<3>(d, 3 + 48).map(|v| (v.clamp(0.0, 1.0) * 255.0).round() as u8),
                _ => {}
            }
        }
    }
    sp += 4;
    let node = format!("{shape}_{}", x.str(vi(sd, &mut sp)));  // texture offset channels name "<shape>_<shader>"
    out.push(Part { pos, nrm, uv, idx, img: if x.t(img) == "XImage" { img } else { 0 }, st, node, world });
}

// Top mip of an XImage as RGBA8 (formats 0 = RGB8, 1/2 = RGBA8).
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

// glb of the mesh scaled by `unit` (mesh units -> W4M world units), plus its bounding box (lo, hi) after scaling.
pub fn convert(x: &Xom, desc: usize, unit: f32) -> Option<(Vec<u8>, String, [f32; 3], [f32; 3])> {
    let d = x.d(desc);
    let mut p = 0;
    vi(d, &mut p);
    p += 2;
    let mut parts = Vec::new();
    let graph = vi(d, &mut p);
    walk(x, graph, sc([unit; 3]), &mut parts, 0, "");
    if parts.is_empty() { return None; }
    // W4M 0x5cd38e: a detail plays its "Go" clip looped from a random time, else "GoSync" from 0; another clip only on an
    // EFMV AnimateDetail (Detail.PlayAnim 0x5ccc80: once, from 0), the first one kept
    let gd = x.d(graph);
    let mut q = 0;
    let lib = if x.t(graph) != "XGraphSet" { None } else {
        (0..vi(gd, &mut q)).find_map(|_| { q += 16; let r = vi(gd, &mut q); vi(gd, &mut q); (x.t(r) == "XAnimClipLibrary").then_some(r) })
    };
    let all = lib.map_or(vec![], |l| crate::anim::clips(x.d(l), &x.s, &mut 0));
    let clip = all.iter().find(|c| c.name == "Go").or_else(|| all.iter().find(|c| c.name == "GoSync")).or_else(|| all.first());
    const UV_STEPS: usize = 64;
    let mut tracks: Vec<Vec<[f32; 2]>> = Vec::new();  // texture offset (U, V) at UV_STEPS + 1 even times over the clip
    let part_track: Vec<Option<usize>> = parts.iter().map(|pt| {
        let c = clip?;
        let (u, v) = (c.ch.get(&(pt.node.clone(), 0x401)), c.ch.get(&(pt.node.clone(), 0x1000401)));
        if u.is_none() && v.is_none() { return None; }
        let at = |k: Option<&Vec<crate::anim::Key>>, t: f32| k.filter(|k| !k.is_empty()).map_or(0.0, |k| crate::anim::eval(k, t));
        tracks.push((0..=UV_STEPS).map(|i| { let t = c.dur * i as f32 / UV_STEPS as f32; [at(u, t), at(v, t)] }).collect());
        Some(tracks.len() - 1)
    }).collect();
    let motion = clip.map_or(false, |c| c.ch.keys().any(|(_, t)| matches!(t & 0xffffff, 0x102 | 0x103 | 0x104 | 0x904)));
    let (mut lo, mut hi) = ([f32::MAX; 3], [f32::MIN; 3]);
    let mut g = Glb::default();
    let mut images: Vec<usize> = parts.iter().map(|p| p.img).filter(|&i| i != 0).collect();
    images.sort();
    images.dedup();
    let mut keys: Vec<(usize, State, Option<usize>)> = Vec::new();
    for (pt, &tk) in parts.iter().zip(&part_track) { if pt.img != 0 && !keys.contains(&(pt.img, pt.st, tk)) { keys.push((pt.img, pt.st, tk)); } }
    let mut img_json = Vec::new();
    for &i in &images {
        let (w, h, rgba) = image(x, i).unwrap_or((1, 1, vec![255; 4]));
        let v = g.view(&png(w, h, &rgba));
        img_json.push(format!("{{\"bufferView\":{v},\"mimeType\":\"image/png\"}}"));
    }
    let mut prims = Vec::new();
    for (pt, &tk) in parts.iter().zip(&part_track) {
        let pos: Vec<f32> = pt.pos.iter().flat_map(|&v| apply(&pt.world, v, 1.0)).collect();
        for q in pos.chunks(3) { for k in 0..3 { lo[k] = lo[k].min(q[k]); hi[k] = hi[k].max(q[k]); } }
        let nrm: Vec<f32> = pt.nrm.iter().flat_map(|&v| {
            let n = apply(&pt.world, v, 0.0);
            let l = (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]).sqrt().max(1e-6);
            n.map(|c| c / l)
        }).collect();
        let n = pt.pos.len();
        let a_pos = g.floats(&pos, "VEC3", n, true);
        let a_nrm = g.floats(&nrm, "VEC3", n, false);
        let a_uv = g.floats(&pt.uv.iter().flatten().copied().collect::<Vec<_>>(), "VEC2", n, false);
        let ib: Vec<u8> = pt.idx.iter().flat_map(|i| i.to_le_bytes()).collect();
        let a_idx = g.acc(&ib, 5123, pt.idx.len(), "SCALAR", "");
        let mat = keys.iter().position(|&k| k == (pt.img, pt.st, tk)).map_or(String::new(), |m| format!(",\"material\":{m}"));
        prims.push(format!("{{\"attributes\":{{\"POSITION\":{a_pos},\"NORMAL\":{a_nrm},\"TEXCOORD_0\":{a_uv}}},\"indices\":{a_idx}{mat}}}"));
    }
    let mats: Vec<String> = keys.iter().map(|k| {
        let i = images.iter().position(|&m| m == k.0).unwrap();
        format!("{{\"pbrMetallicRoughness\":{{\"baseColorTexture\":{{\"index\":{i}}},\"metallicFactor\":0}}}}")
    }).collect();
    // sidecar: "clip <name> <s> <motion>" if any, then one line per glb material: blend src dst (-1 -1 = none), alpha test function
    // and ref (0..255), z write, cull mode, lit, emissive rgb, then its texture offset track if animated (count, u v pairs)
    let mut side = clip.map_or(String::new(), |c| format!("clip {} {} {}\n", c.name, c.dur, motion as u8));
    for (_, s, tk) in &keys {
        side += &format!("{} {} {} {} {} {} {} {} {} {}", s.blend[0], s.blend[1], s.test[0], s.test[1], s.zwrite as u8, s.cull, s.lit as u8, s.emit[0], s.emit[1], s.emit[2]);
        if let Some(t) = tk.map(|i| &tracks[i]) { side += &format!(" {}", t.len()); for [u, v] in t { side += &format!(" {u:.4} {v:.4}"); } }
        side += "\n";
    }
    let texs: Vec<String> = (0..images.len()).map(|i| format!("{{\"source\":{i}}}")).collect();
    let json = format!(
        "{{\"asset\":{{\"version\":\"2.0\",\"generator\":\"w4m-maps\"}},\"scene\":0,\"scenes\":[{{\"nodes\":[0]}}],\"nodes\":[{{\"mesh\":0}}],\"meshes\":[{{\"primitives\":[{}]}}],\"materials\":[{}],\"textures\":[{}],\"images\":[{}],\"accessors\":[{}],\"bufferViews\":[{}],\"buffers\":[{{\"byteLength\":{}}}]}}",
        prims.join(","), mats.join(","), texs.join(","), img_json.join(","), g.accs.join(","), g.views.join(","), g.bin.len()
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
    Some((o, side, lo, hi))
}
