// Lua 5.0 bytecode (Data/scripts/*.lub, little endian, 4-byte float numbers): just enough to list, per global
// function, the calls it makes with their constant arguments.
use std::collections::HashMap;

#[derive(Clone, Debug, PartialEq)]
pub enum Val { Str(String), Num(f32), Global(String), Other }

#[derive(Default, Debug)]
pub struct Func { pub calls: Vec<(String, Vec<Val>)>, pub sets: Vec<(String, Val)> }

struct Proto { k: Vec<Val>, protos: Vec<Proto>, code: Vec<u32> }

struct R<'a> { b: &'a [u8], p: usize }
impl R<'_> {
    fn take(&mut self, n: usize) -> Option<&[u8]> { let s = self.b.get(self.p..self.p + n)?; self.p += n; Some(s) }
    fn u8(&mut self) -> Option<u8> { Some(self.take(1)?[0]) }
    fn i32(&mut self) -> Option<i32> { Some(i32::from_le_bytes(self.take(4)?.try_into().ok()?)) }
    fn str(&mut self) -> Option<String> {
        let n = self.i32()? as usize;
        Some(String::from_utf8_lossy(self.take(n)?.strip_suffix(&[0]).unwrap_or(&[])).into_owned())
    }
    fn proto(&mut self, depth: u32) -> Option<Proto> {
        if depth > 32 { return None; }
        self.str()?;
        self.take(4 + 4)?; // line defined, nups, numparams, is_vararg, maxstacksize
        let n = self.i32()? as usize;
        self.take(4 * n)?; // line info
        for _ in 0..self.i32()? { self.str()?; self.take(8)?; } // locals
        for _ in 0..self.i32()? { self.str()?; } // upvalue names
        let mut k = Vec::new();
        for _ in 0..self.i32()? {
            k.push(match self.u8()? {
                3 => Val::Num(f32::from_le_bytes(self.take(4)?.try_into().ok()?)),
                4 => Val::Str(self.str()?),
                _ => Val::Other,
            });
        }
        let protos = (0..self.i32()?).map(|_| self.proto(depth + 1)).collect::<Option<Vec<_>>>()?;
        let n = self.i32()? as usize;
        let code = self.take(4 * n)?.chunks(4).map(|c| u32::from_le_bytes(c.try_into().unwrap())).collect();
        Some(Proto { k, protos, code })
    }
}

// Symbolic pass: registers hold constants / global names; CALL records the callee and its arguments.
fn walk(p: &Proto) -> Func {
    let mut f = Func::default();
    let mut reg: HashMap<u32, Val> = HashMap::new();
    let rk = |reg: &HashMap<u32, Val>, x: u32| if x >= 250 { p.k.get((x - 250) as usize).cloned().unwrap_or(Val::Other) } else { reg.get(&x).cloned().unwrap_or(Val::Other) };
    for &i in &p.code {
        let (op, a, b, c, bx) = (i & 63, i >> 24, (i >> 15) & 511, (i >> 6) & 511, (i >> 6) & 0x3ffff);
        match op {
            0 => { let v = rk(&reg, b); reg.insert(a, v); }                    // MOVE
            1 => { reg.insert(a, p.k.get(bx as usize).cloned().unwrap_or(Val::Other)); } // LOADK
            5 => { if let Some(Val::Str(s)) = p.k.get(bx as usize) { reg.insert(a, Val::Global(s.clone())); } } // GETGLOBAL
            7 => { if let Some(Val::Str(s)) = p.k.get(bx as usize) { f.sets.push((s.clone(), rk(&reg, a))); } } // SETGLOBAL
            25 | 26 => {                                                         // CALL, TAILCALL
                if let Some(Val::Global(name)) = reg.get(&a) {
                    let args = if b == 0 { Vec::new() } else { (a + 1..a + b).map(|x| rk(&reg, x)).collect() };
                    f.calls.push((name.clone(), args));
                }
                for x in a..a + c.max(1) { reg.insert(x, Val::Other); }
            }
            _ => { reg.insert(a, Val::Other); }
        }
    }
    f
}

// Global functions of a compiled chunk: main's `CLOSURE n` followed by `SETGLOBAL name`.
pub fn functions(b: &[u8]) -> Option<HashMap<String, Func>> {
    if b.get(..5)? != b"\x1bLua\x50" { return None; }
    let main = R { b, p: 18 }.proto(0)?;
    let mut out = HashMap::new();
    let mut pending: Option<usize> = None;
    for &i in &main.code {
        match i & 63 {
            34 => pending = Some(((i >> 6) & 0x3ffff) as usize),
            7 => if let (Some(n), Some(Val::Str(name))) = (pending.take(), main.k.get(((i >> 6) & 0x3ffff) as usize)) {
                if let Some(pr) = main.protos.get(n) { out.insert(name.clone(), walk(pr)); }
            },
            _ => {}
        }
    }
    out.insert("main".into(), walk(&main));
    Some(out)
}
