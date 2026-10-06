// Mission script data for the client's Lua runtime (docs/missions.md "Scripts"): the .lub chunks, the data keys and containers of
// the Tweak files (scripts/data.json) and, per level, its databank's keys, containers and the Critical events of its movies.
use crate::mission::esc;
use crate::schema::CLASSES;
use crate::{find_ci, read_xom, u32le, vi, Xom};
use std::fs;
use std::path::Path;

// (name, type, array, absent from some files)
fn class(t: &str) -> Option<Vec<(&'static str, &'static str, bool, bool)>> {
    let (_, f) = CLASSES.iter().find(|c| c.0 == t)?;
    Some(f.split_whitespace().map(|x| {
        let (n, ty) = x.split_once(':').unwrap();
        let opt = ty.ends_with('?');
        let ty = ty.trim_end_matches('?');
        (n, ty.trim_end_matches("[]"), ty.ends_with("[]"), opt)
    }).collect())
}

fn num(v: f32) -> String { if v.is_finite() { format!("{v:?}") } else { "0.0".into() } }  // a float keeps its '.': the client tells int from float

// One field value as JSON; None: not exported (refs)
fn value(x: &Xom, d: &[u8], p: &mut usize, ty: &str) -> Option<String> {
    let i = *p;
    let n = match ty { "bool" | "u8" | "i8" => 1, "u16" => 2, "i32" | "u32" | "enum" | "f32" | "rgba8" => 4, "vec3" => 12, _ => 0 };
    *p += n;
    let f = |k: usize| num(f32::from_bits(u32le(d, i + 4 * k)));
    Some(match ty {
        "bool" => (d.get(i) == Some(&1)).to_string(),
        "u8" => d.get(i).copied().unwrap_or(0).to_string(),
        "i8" => (d.get(i).copied().unwrap_or(0) as i8).to_string(),
        "u16" => d.get(i..i + 2).map_or(0, |b| u16::from_le_bytes([b[0], b[1]])).to_string(),
        "i32" => (u32le(d, i) as i32).to_string(),
        "u32" | "enum" => u32le(d, i).to_string(),
        "f32" => f(0),
        "vec3" => format!("[{}, {}, {}]", f(0), f(1), f(2)),
        "rgba8" => format!("[{}]", d.get(i..i + 4).unwrap_or(&[0; 4]).iter().map(|b| b.to_string()).collect::<Vec<_>>().join(", ")),
        "str" => esc(x.s.get(vi(d, p)).map_or("", |s| s.as_str())),
        _ => { vi(d, p); return None; }  // ref
    })
}

// Decoded with every field, else without those some files lack (xom.py does the same)
fn container(x: &Xom, i: usize) -> Option<String> {
    let (t, d) = x.ctn.get(i)?;
    let fields = class(t)?;
    [false, true].iter().find_map(|&skip| {
        let mut p = 3;
        let mut out = vec![format!("\"_type\": {}", esc(t))];
        for (n, ty, arr, _) in fields.iter().filter(|f| !(skip && f.3)) {
            if *arr {
                let v: Vec<String> = (0..vi(d, &mut p)).filter_map(|_| value(x, d, &mut p, ty)).collect();
                if *ty != "ref" { out.push(format!("{}: [{}]", esc(n), v.join(", "))); }
            } else if let Some(v) = value(x, d, &mut p, ty) {
                out.push(format!("{}: {v}", esc(n)));
            }
        }
        (p == d.len()).then(|| format!("{{{}}}", out.join(", ")))
    })
}

// A movie's Critical events (acting.md §19: the skip fires only those) in the player's order: a cursor per track fires events in
// file order while Time <= now, now += 10 ms per step. Each: [type, fields...]; types the client does not run keep their name.
fn movie(x: &Xom, i: usize) -> Option<String> {
    let d = &x.ctn.get(i)?.1;
    let mut p = 3;
    vi(d, &mut p);  // Tag
    let mut ev = Vec::new();  // (step, track, file order, JSON)
    for tr in 0..vi(d, &mut p) {
        let (_, td) = x.ctn.get(vi(d, &mut p).checked_sub(1)?)?;
        let mut q = 3;
        vi(td, &mut q);
        let mut at = 0u32;
        for k in 0..vi(td, &mut q) {
            let e = vi(td, &mut q).checked_sub(1)?;
            let (t, ed) = x.ctn.get(e)?;
            let n = ed.len();
            at = at.max(u32le(ed, n.checked_sub(5)?));  // base fields last: Tag, Time u32, Critical bool
            if ed[n - 1] != 1 { continue; }
            let mut f = 3;
            let fields: Vec<String> = class(t).map_or(Vec::new(), |c| c.iter().take(c.len() - 3).filter_map(|f2| value(x, ed, &mut f, f2.1)).collect());
            let name = t.trim_start_matches("EFMV_").split("Event").next().unwrap_or(t);
            ev.push((at.div_ceil(10), tr, k, format!("[{}]", std::iter::once(esc(name)).chain(fields).collect::<Vec<_>>().join(", "))));
        }
    }
    ev.sort_by_key(|e| (e.0, e.1, e.2));
    Some(format!("[{}]", ev.into_iter().map(|e| e.3).collect::<Vec<_>>().join(", ")))
}

// Named resources: data keys (XInt/Uint/Float/String/Vector/ColorResourceDetails: Value, Name, Flags) and containers
fn resources(x: &Xom, keys: &mut Vec<String>, ctns: &mut Vec<String>, movies: &mut Vec<String>) {
    for (t, d) in &x.ctn {
        let Some(kind) = t.strip_prefix('X').and_then(|t| t.strip_suffix("ResourceDetails")) else { continue };
        let mut p = 3;
        let ty = match kind { "Int" => "i32", "Uint" => "u32", "Float" => "f32", "String" => "str", "Vector" => "vec3", "Color" => "rgba8", "Container" => "ref", _ => continue };
        if ty == "ref" {
            let r = vi(d, &mut p).wrapping_sub(1);
            let name = x.s.get(vi(d, &mut p)).cloned().unwrap_or_default();
            match x.ctn.get(r).map(|c| c.0.as_str()) {
                Some("EFMV_MovieContainer") => movies.extend(movie(x, r).map(|m| format!("{}: {m}", esc(&name)))),
                Some(_) => ctns.extend(container(x, r).map(|c| format!("{}: {c}", esc(&name)))),
                None => {}
            }
        } else if let Some(v) = value(x, d, &mut p, ty) {
            let name = x.s.get(vi(d, &mut p)).cloned().unwrap_or_default();
            keys.push(format!("{}: {v}", esc(&name)));
        }
    }
}

fn write(out: &Path, x: &[Xom]) -> std::io::Result<()> {
    let (mut keys, mut ctns, mut movies) = (Vec::new(), Vec::new(), Vec::new());
    for x in x { resources(x, &mut keys, &mut ctns, &mut movies); }
    fs::write(out, format!("{{\n\"keys\": {{\n{}\n}},\n\"containers\": {{\n{}\n}},\n\"movies\": {{\n{}\n}}\n}}\n", keys.join(",\n"), ctns.join(",\n"), movies.join(",\n")))
}

fn xom(p: Option<std::path::PathBuf>) -> Option<Xom> { p.and_then(|p| fs::read(p).ok()).and_then(|b| read_xom(&b)) }

// The Tweak files' data (LOCAL, LVLSETUP, TWEAK, WEAPTWK, AITWK, HUDTWK, CAMTWK, DEFSAVE) and stdlib / lib_help
pub fn export_common(data: &Path, out: &Path) {
    let _ = fs::create_dir_all(out);
    let x: Vec<Xom> = ["LOCAL", "LVLSETUP", "TWEAK", "WEAPTWK", "AITWK", "HUDTWK", "CAMTWK", "DEFSAVE"].iter()
        .filter_map(|f| xom(find_ci(&data.join("Tweak"), &format!("{f}.XOM")))).collect();
    if let Err(e) = write(&out.join("data.json"), &x) { println!("scripts: data.json {e}"); }
    for s in ["stdlib", "lib_help"] { copy(data, s, out); }
}

fn copy(data: &Path, script: &str, out: &Path) -> bool {
    find_ci(&data.join("scripts"), &format!("{script}.lub")).and_then(|p| fs::copy(p, out.join(format!("{script}.lub"))).ok()).is_some()
}

// A level's script chunk and databank (<out>/<script>.lub, <out>/<file>.json)
pub fn export_level(data: &Path, file: &str, script: &str, out: &Path) -> Result<(), String> {
    if !copy(data, script, out) { return Err("no script".into()); }
    let x = xom(find_ci(data, &format!("{file}.XOM"))).ok_or("no databank")?;
    write(&out.join(format!("{file}.json")), &[x]).map_err(|e| e.to_string())
}
