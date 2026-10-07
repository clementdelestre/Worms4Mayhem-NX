// Raw DEFLATE (RFC 1951) with dynamic Huffman blocks: greedy matches over hash chains. The client inflates with raylib's sinflate.

const LBASE: [usize; 29] = [3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258];
const LEXT: [u32; 29] = [0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0];
const DBASE: [usize; 30] = [1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577];
const DEXT: [u32; 30] = [0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13];
const CLORDER: [usize; 19] = [16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15];

struct Bits { out: Vec<u8>, acc: u64, n: u32 }
impl Bits {
    fn put(&mut self, v: usize, n: u32) {
        self.acc |= (v as u64) << self.n;
        self.n += n;
        while self.n >= 8 { self.out.push(self.acc as u8); self.acc >>= 8; self.n -= 8; }
    }
    fn code(&mut self, c: (u32, u32)) { self.put(c.0 as usize, c.1); }
}

// Huffman code lengths capped at `max` (frequencies halved until the tree fits); at least two codes so every tree is complete
fn lengths(freq: &[u32], max: u32) -> Vec<u32> {
    let mut f: Vec<u64> = freq.iter().map(|&v| v as u64).collect();
    for i in 0..f.len() { if f.iter().filter(|&&v| v > 0).count() >= 2 { break; } if f[i] == 0 { f[i] = 1; } }
    loop {
        let mut nodes: Vec<(u64, Vec<usize>)> = f.iter().enumerate().filter(|(_, &v)| v > 0).map(|(i, &v)| (v, vec![i])).collect();
        let mut len = vec![0u32; f.len()];
        while nodes.len() > 1 {
            nodes.sort_by(|a, b| b.0.cmp(&a.0).then(b.1[0].cmp(&a.1[0])));
            let (a, b) = (nodes.pop().unwrap(), nodes.pop().unwrap());
            for &s in a.1.iter().chain(b.1.iter()) { len[s] += 1; }
            nodes.push((a.0 + b.0, [a.1, b.1].concat()));
        }
        if len.iter().all(|&l| l <= max) { return len; }
        for v in f.iter_mut() { if *v > 0 { *v = (*v >> 1) | 1; } }
    }
}

// canonical codes, bit-reversed for the LSB-first stream: (code, length)
fn codes(len: &[u32]) -> Vec<(u32, u32)> {
    let mut count = [0u32; 16];
    for &l in len { if l > 0 { count[l as usize] += 1; } }
    let (mut next, mut c) = ([0u32; 16], 0u32);
    for b in 1..16 { c = (c + count[b - 1]) << 1; next[b] = c; }
    len.iter().map(|&l| {
        if l == 0 { return (0, 0); }
        let v = next[l as usize];
        next[l as usize] += 1;
        (v.reverse_bits() >> (32 - l), l)
    }).collect()
}

enum Tok { Lit(u8), Match(usize, usize) }

pub fn deflate(d: &[u8]) -> Vec<u8> {
    let hash = |i: usize| ((d[i] as usize) << 10 ^ (d[i + 1] as usize) << 5 ^ d[i + 2] as usize) & 0x7fff;
    let (mut head, mut prev) = (vec![usize::MAX; 1 << 15], vec![usize::MAX; d.len()]);
    let mut toks = Vec::new();
    let mut i = 0;
    while i < d.len() {
        let (mut best, mut dist, mut j, mut chain) = (0, 0, if i + 3 <= d.len() { head[hash(i)] } else { usize::MAX }, 128);
        while j != usize::MAX && i - j <= 32768 && chain > 0 {
            let max = 258.min(d.len() - i);
            let l = (0..max).find(|&l| d[j + l] != d[i + l]).unwrap_or(max);
            if l > best { best = l; dist = i - j; if l == max { break; } }
            j = prev[j];
            chain -= 1;
        }
        if best < 3 { best = 1; toks.push(Tok::Lit(d[i])); } else { toks.push(Tok::Match(best, dist)); }
        for k in i..i + best { if k + 3 <= d.len() { let h = hash(k); prev[k] = head[h]; head[h] = k; } }
        i += best;
    }
    let mut w = Bits { out: Vec::new(), acc: 0, n: 0 };
    let blocks: Vec<&[Tok]> = if toks.is_empty() { vec![&toks[..]] } else { toks.chunks(1 << 16).collect() };
    for (b, blk) in blocks.iter().enumerate() {
        let (mut lf, mut df) = (vec![0u32; 286], vec![0u32; 30]);
        let sym = |t: &Tok| match *t {
            Tok::Lit(v) => (v as usize, None),
            Tok::Match(l, dd) => (257 + LBASE.iter().rposition(|&x| x <= l).unwrap(), Some(DBASE.iter().rposition(|&x| x <= dd).unwrap())),
        };
        for t in blk.iter() { let (s, ds) = sym(t); lf[s] += 1; if let Some(k) = ds { df[k] += 1; } }
        lf[256] = 1;
        let (ll, dl) = (lengths(&lf, 15), lengths(&df, 15));
        let hlit = 257.max(ll.iter().rposition(|&l| l > 0).unwrap() + 1);
        let hdist = 1.max(dl.iter().rposition(|&l| l > 0).map_or(1, |p| p + 1));
        // code lengths, run-length coded (16: repeat previous 3-6, 17: zeros 3-10, 18: zeros 11-138)
        let all: Vec<u32> = ll[..hlit].iter().chain(dl[..hdist].iter()).copied().collect();
        let mut rl: Vec<(u32, u32)> = Vec::new();
        let mut k = 0;
        while k < all.len() {
            let v = all[k];
            let run = all[k..].iter().take_while(|&&x| x == v).count();
            if v == 0 && run >= 3 { let r = run.min(138); rl.push(if r <= 10 { (17, r as u32 - 3) } else { (18, r as u32 - 11) }); k += r; }
            else if v != 0 && run >= 4 { rl.push((v, 0)); let r = (run - 1).min(6); rl.push((16, r as u32 - 3)); k += 1 + r; }
            else { rl.push((v, 0)); k += 1; }
        }
        let mut cf = vec![0u32; 19];
        for &(s, _) in &rl { cf[s as usize] += 1; }
        let cl = lengths(&cf, 7);
        let hclen = 4.max(CLORDER.iter().rposition(|&s| cl[s] > 0).unwrap() + 1);
        w.put((b + 1 == blocks.len()) as usize, 1);
        w.put(2, 2);
        w.put(hlit - 257, 5);
        w.put(hdist - 1, 5);
        w.put(hclen - 4, 4);
        for &s in &CLORDER[..hclen] { w.put(cl[s] as usize, 3); }
        let cc = codes(&cl);
        for &(s, extra) in &rl {
            w.code(cc[s as usize]);
            match s { 16 => w.put(extra as usize, 2), 17 => w.put(extra as usize, 3), 18 => w.put(extra as usize, 7), _ => {} }
        }
        let (lc, dc) = (codes(&ll), codes(&dl));
        for t in blk.iter() {
            match *t {
                Tok::Lit(v) => w.code(lc[v as usize]),
                Tok::Match(l, dd) => {
                    let li = LBASE.iter().rposition(|&x| x <= l).unwrap();
                    w.code(lc[257 + li]);
                    w.put(l - LBASE[li], LEXT[li]);
                    let di = DBASE.iter().rposition(|&x| x <= dd).unwrap();
                    w.code(dc[di]);
                    w.put(dd - DBASE[di], DEXT[di]);
                }
            }
        }
        w.code(lc[256]);
    }
    if w.n > 0 { w.put(0, 8 - w.n); }
    w.out
}
