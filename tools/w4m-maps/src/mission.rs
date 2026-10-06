// W4M story missions, challenges and deathmatches -> Worms4NX mission JSON (format: docs/missions.md): the level list
// (Data/Tweak/SCRIPTS.XOM), names and briefs; the level itself runs from its Lua script and databank (script.rs).
use crate::lua::{self, Val};
use crate::{find_ci, read_xom, u32le, vi};
use std::collections::{HashMap, HashSet};
use std::fs;
use std::path::Path;

pub fn esc(s: &str) -> String {
    let s = s.replace(['\u{2018}', '\u{2019}'], "'").replace(['\u{201c}', '\u{201d}'], "\"").replace('\u{2026}', "...").replace(['\u{2013}', '\u{2014}'], "-");
    let mut o = String::from("\"");
    for c in s.replace("/*NL*/", "\n").chars() {
        match c { '"' => o += "\\\"", '\\' => o += "\\\\", '\n' => o += "\\n", c if (c as u32) < 32 => {}, c if (c as u32) > 255 => o.push('?'), c => o.push(c) }
    }
    o + "\""
}

// Language strings (XStringResourceDetails: 3 header bytes, varint value, varint key).
fn language(data: &Path) -> HashMap<String, String> {
    let mut out = HashMap::new();
    for f in ["English.xom", "EngFE.xom", "EngLS.xom"] {
        let Some(x) = find_ci(&data.join("Language/PC"), f).and_then(|p| fs::read(p).ok()).and_then(|b| read_xom(&b)) else { continue };
        for (_, d) in x.ctn.iter().filter(|c| c.0 == "XStringResourceDetails") {
            let mut p = 3;
            let (v, k) = (vi(d, &mut p), vi(d, &mut p));
            if let (Some(k), Some(v)) = (x.s.get(k), x.s.get(v)) { out.insert(k.clone(), v.clone()); }
        }
    }
    out
}

struct Level { id: String, name: String, brief: String, preview: String, file: String, script: String, objectives: String, kind: u32, index: u32, par: u32 }

// WXFE_LevelDetails of a level file (`script` holds Level_FileName), with a Frontend_Image: the versus entry (kind 0) wins.
// Demo (kinds 12, 13) and outtake (16) entries reuse another level's name and image.
fn level_of(data: &Path, stem: &str) -> Option<&'static Level> {
    static L: std::sync::OnceLock<Vec<Level>> = std::sync::OnceLock::new();
    let n = stem.to_lowercase();
    let m: Vec<&Level> = L.get_or_init(|| levels(data)).iter()
        .filter(|l| l.script.to_lowercase() == n && !l.preview.is_empty() && !matches!(l.kind, 12 | 13 | 16)).collect();
    m.iter().find(|l| l.kind == 0).or(m.first()).copied()
}

// Frontend_Image of a level file; "LP_"/"SPLP_" loading variants share their base level's
pub fn preview_of(data: &Path, stem: &str) -> Option<String> {
    let low = stem.to_lowercase();
    let base = low.strip_prefix("splp_").or_else(|| low.strip_prefix("lp_")).unwrap_or(&low);
    let l = level_of(data, &low).or_else(|| level_of(data, base))?;
    Some(l.preview.to_lowercase().trim_end_matches(".tga").replace(' ', "_"))
}

// Frontend_Name text key of the level file itself (the variants keep their own name)
pub fn title_of(data: &Path, stem: &str) -> Option<String> {
    level_of(data, stem).map(|l| l.name.clone()).filter(|n| !n.is_empty())
}

fn levels(data: &Path) -> Vec<Level> {
    let Some(x) = find_ci(&data.join("Tweak"), "SCRIPTS.XOM").and_then(|p| fs::read(p).ok()).and_then(|b| read_xom(&b)) else { return Vec::new() };
    // resource names (XContainerResourceDetails: container ref, name): Story.DinerMight, the Lock.T.<name> key
    let ids: HashMap<usize, String> = x.ctn.iter().filter(|c| c.0 == "XContainerResourceDetails").filter_map(|(_, d)| {
        let mut p = 3;
        let r = vi(d, &mut p).wrapping_sub(1);
        x.s.get(vi(d, &mut p)).map(|n| (r, n.clone()))
    }).collect();
    x.ctn.iter().enumerate().filter(|c| c.1.0 == "WXFE_LevelDetails").map(|(i, (_, d))| {
        let mut p = 3;
        let mut s: Vec<String> = (0..6).map(|_| x.s.get(vi(d, &mut p)).cloned().unwrap_or_default()).collect();
        let (index, kind) = (u32le(d, p), u32le(d, p + 4));
        p += 8;
        vi(d, &mut p); // unlock key
        Level { id: ids.get(&i).cloned().unwrap_or_default(), par: u32le(d, p + 8), name: std::mem::take(&mut s[0]), brief: std::mem::take(&mut s[1]), preview: std::mem::take(&mut s[2]),
                file: std::mem::take(&mut s[3]), script: std::mem::take(&mut s[4]), objectives: std::mem::take(&mut s[5]), kind, index }
    }).collect()
}

fn mission(data: &Path, lv: &Level, lang: &HashMap<String, String>, maps: &Path, order: usize, scripts: &Path) -> Result<String, String> {
    let stem = fs::read_dir(data.join("Maps")).map_err(|e| e.to_string())?.flatten()
        .filter_map(|e| e.file_name().to_str().and_then(|n| n.strip_suffix(".xan")).map(String::from))
        .find(|n| n.eq_ignore_ascii_case(&lv.file)).ok_or("no map")?;
    if !maps.join(format!("{stem}.json")).exists() { return Err("map not imported".into()); }
    crate::script::export_level(data, &lv.file, &lv.script, scripts)?;
    let script = find_ci(&data.join("scripts"), &format!("{}.lub", lv.script)).and_then(|p| fs::read(p).ok()).ok_or("no script")?;
    let funcs = lua::functions(&script).ok_or("bad script")?;
    let init = funcs.get("Initialise").ok_or("no Initialise")?;
    let set = |k: &str| init.calls.iter().rev().find(|(c, a)| c == "SetData" && a.first() == Some(&Val::Str(k.into())))
        .and_then(|(_, a)| if let Some(Val::Num(n)) = a.get(1) { Some(*n) } else { None });
    let t = |k: &str| lang.get(k).cloned().unwrap_or_default();
    let name = Some(t(&lv.name)).filter(|s| !s.is_empty()).unwrap_or(lv.file.clone());
    let (campaign, kindj) = match lv.kind { 4 => ("W4M Story", "mission"), 9 => ("W4M Deathmatch", "challenge"), _ => ("W4M Challenges", "challenge") };
    let preview = format!("levels/{}", lv.preview.to_lowercase().trim_end_matches(".tga"));
    // Lua Initialise may force the weather odds (FlowControlService reads them first, 0x4e73c0): render only, read before the match
    let rain = set("Particle.Rain.Prob").map_or(String::new(), |p| format!("  \"rain_prob\": {p},\n"));
    let done = if lv.kind == 4 { "FETXT.MissionCompleteBody" } else { "FETXT.ChallengeCompleteBody" };
    // the *_id keys: the client shows the language's text (assets/lang), the English one above as the fallback
    Ok(format!(
        "{{\n  \"name\": {},\n  \"kind\": \"{kindj}\",\n  \"campaign\": \"{campaign}\",\n  \"order\": {order},\n  \"level\": {},\n  \"map\": {},\n  \"preview\": {},\n  \"par\": {},\n  \"brief\": {},\n  \"success\": {},\n  \"name_id\": {},\n  \"brief_id\": {},\n  \"success_id\": \"{done}\",\n  \"objectives\": {},\n{rain}  \"script\": {},\n  \"bank\": {}\n}}\n",
        esc(&name), esc(&lv.id), esc(&stem), esc(&preview), lv.par, esc(&t(&lv.brief)), esc(&t(done)), esc(&lv.name), esc(&lv.brief), esc(&lv.objectives), esc(&lv.script), esc(&lv.file)))
}

// Writes <out>/<file>.json for every story mission (W4M type 4), challenge (8) and deathmatch (9) whose map was imported.
pub fn import(data: &Path, maps: &Path, out: &Path) {
    let lang = language(data);
    let mut list: Vec<Level> = levels(data).into_iter().filter(|l| matches!(l.kind, 4 | 8 | 9) && !l.file.to_lowercase().ends_with("-w3d")).collect();
    list.sort_by_key(|l| (l.kind, l.index));
    let _ = fs::create_dir_all(out);
    let scripts = out.join("../scripts");
    crate::script::export_common(data, &scripts);
    let (mut ok, mut seen) = (0, HashSet::new());
    for (i, lv) in list.iter().enumerate() {
        if !seen.insert(lv.file.to_lowercase()) { continue; }
        match mission(data, lv, &lang, maps, i, &scripts) {
            Ok(j) => { ok += 1; let _ = fs::write(out.join(format!("{}.json", lv.file)), j); }
            Err(e) => println!("mission {}: skipped ({e})", lv.file),
        }
    }
    println!("{ok}/{} missions imported", list.len());
}
