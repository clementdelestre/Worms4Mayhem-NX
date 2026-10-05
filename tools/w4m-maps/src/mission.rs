// W4M story missions, challenges and deathmatches -> Worms4NX mission JSON (format: docs/missions.md).
// Level list: Data/Tweak/SCRIPTS.XOM; teams, worms, inventories, crates, triggers: the level databank; setup and
// win/lose conditions: a static read of the level's Lua script. Positions stay W4M marker names (map JSON "markers").
use crate::lua::{self, Val};
use crate::{find_ci, read_xom, u32le, f32le, vi, Xom};
use std::collections::{HashMap, HashSet};
use std::fs;
use std::path::Path;

// WeaponInventory byte order (3 header bytes, then one count per slot, 0xff = infinite). Slots 29..35 and 39+ are not
// identified; Girder, Bridge Kit, Redbull have no Worms4NX weapon (Redbull -> Jetpack, the closest flyer).
const INVENTORY: [&str; 39] = ["Bazooka", "Grenade", "Cluster Grenade", "Airstrike", "Dynamite", "Holy Hand Grenade", "Banana Bomb", "Landmine",
    "Shotgun", "Baseball Bat", "Prod", "Fire Punch", "Homing Missile", "Flood", "Sheep", "Gas Canister", "Old Woman", "Concrete Donkey",
    "Super Sheep", "", "", "Ninja Rope", "Parachute", "Teleport", "Jetpack", "Skip Go", "Surrender", "Change Worm", "Jetpack",
    "", "", "", "", "", "", "", "Poison Arrow", "Sentry Gun", "Sniper Rifle"];

// kWeapon*/kUtility* crate contents -> weapon name ("" = none)
fn content(c: &str) -> String {
    let k = c.trim_start_matches("kWeapon").trim_start_matches("kUtility");
    let alias = [("Fatkins", "Fatkins Strike"), ("Scouser", "Inflatable Scouser"), ("Redbull", "Jetpack"), ("SkipGo", "Skip Go"), ("Girder", "Ninja Rope"), ("BridgeKit", "Ninja Rope")];
    if let Some((_, v)) = alias.iter().find(|(a, _)| *a == k) { return v.to_string(); }
    INVENTORY.iter().chain(["Starburst", "Alien Abduction", "Super Airstrike"].iter())
        .find(|n| !n.is_empty() && n.replace(' ', "").eq_ignore_ascii_case(k)).map_or(String::new(), |s| s.to_string())
}

fn esc(s: &str) -> String {
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

struct Level { name: String, brief: String, preview: String, file: String, script: String, kind: u32, index: u32, par: u32 }

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
    x.ctn.iter().filter(|c| c.0 == "WXFE_LevelDetails").map(|(_, d)| {
        let mut p = 3;
        let mut s: Vec<String> = (0..6).map(|_| x.s.get(vi(d, &mut p)).cloned().unwrap_or_default()).collect();
        let (index, kind) = (u32le(d, p), u32le(d, p + 4));
        p += 8;
        vi(d, &mut p); // unlock key
        Level { par: u32le(d, p + 8), name: std::mem::take(&mut s[0]), brief: std::mem::take(&mut s[1]), preview: std::mem::take(&mut s[2]),
                file: std::mem::take(&mut s[3]), script: std::mem::take(&mut s[4]), kind, index }
    }).collect()
}

struct Bank { x: Xom, named: HashMap<String, usize> }
impl Bank {
    fn get(&self, name: &str, ty: &str) -> Option<&[u8]> {
        let &i = self.named.get(name)?;
        self.x.ctn.get(i).filter(|c| c.0 == ty).map(|c| c.1.as_slice())
    }
    fn s(&self, i: usize) -> String { self.x.s.get(i).cloned().unwrap_or_default() }
}

struct Worm { name: String, team: u32, hp: u32, spawn: String, turns: bool }
// WormDataContainer: name, fixed fields (TeamIndex u32 @139, Energy u32 @161 after the name), SfxBankName, Spawn, IsParachuteSpawn, IsAllowedToTakeTurn
fn worm(b: &Bank, d: &[u8]) -> Worm {
    let mut p = 3;
    let name = b.s(vi(d, &mut p));
    let base = p;
    let mut q = base + 177;
    vi(d, &mut q);
    let spawn = b.s(vi(d, &mut q));
    Worm { name, team: u32le(d, base + 139), hp: u32le(d, base + 161), spawn, turns: d.get(q + 1) != Some(&0) }
}

fn inventory(d: &[u8]) -> String {
    let mut m: Vec<(String, i32)> = Vec::new();
    for (i, n) in INVENTORY.iter().enumerate() {
        let v = d.get(3 + i).copied().unwrap_or(0);
        if v == 0 || n.is_empty() { continue; }
        let c = if v == 0xff { -1 } else { v as i32 };
        match m.iter_mut().find(|e| e.0 == *n) { Some(e) => e.1 = if e.1 < 0 || c < 0 { -1 } else { e.1 + c }, None => m.push((n.to_string(), c)) }
    }
    format!("{{{}}}", m.iter().map(|(n, c)| format!("{}: {c}", esc(n))).collect::<Vec<_>>().join(", "))
}

// Does `f` (transitively) send `msg`?
fn reaches(fs: &HashMap<String, lua::Func>, f: &str, msg: &str, seen: &mut HashSet<String>) -> bool {
    if !seen.insert(f.to_string()) { return false; }
    let Some(func) = fs.get(f) else { return false };
    func.calls.iter().any(|(c, a)| (c == "SendMessage" && a.first() == Some(&Val::Str(msg.into()))) || reaches(fs, c, msg, seen))
}

fn mission(data: &Path, lv: &Level, lang: &HashMap<String, String>, maps: &Path, order: usize) -> Result<String, String> {
    let stem = fs::read_dir(data.join("Maps")).map_err(|e| e.to_string())?.flatten()
        .filter_map(|e| e.file_name().to_str().and_then(|n| n.strip_suffix(".xan")).map(String::from))
        .find(|n| n.eq_ignore_ascii_case(&lv.file)).ok_or("no map")?;
    if !maps.join(format!("{stem}.json")).exists() { return Err("map not imported".into()); }
    let script = find_ci(&data.join("scripts"), &format!("{}.lub", lv.script)).and_then(|p| fs::read(p).ok()).ok_or("no script")?;
    let funcs = lua::functions(&script).ok_or("bad script")?;
    let x = find_ci(data, &format!("{}.XOM", lv.file)).and_then(|p| fs::read(p).ok()).and_then(|b| read_xom(&b)).ok_or("no databank")?;
    let mut named = HashMap::new();
    for (_, d) in x.ctn.iter().filter(|c| c.0 == "XContainerResourceDetails") {
        let mut p = 3;
        let (r, n) = (vi(d, &mut p), vi(d, &mut p));
        if let Some(n) = x.s.get(n) { named.insert(n.clone(), r.wrapping_sub(1)); }
    }
    let bank = Bank { x, named };
    let init = funcs.get("Initialise").ok_or("no Initialise")?;
    let arg = |a: &[Val], i: usize| match a.get(i) { Some(Val::Str(s)) => s.clone(), Some(Val::Num(n)) => n.to_string(), _ => String::new() };
    let num = |a: &[Val], i: usize| match a.get(i) { Some(Val::Num(n)) => Some(*n), _ => None };
    let set = |k: &str| init.calls.iter().rev().find(|(c, a)| c == "SetData" && arg(a, 0) == k).and_then(|(_, a)| num(a, 1));

    // teams and worms in setup order; worms that never take a turn (accuracy dummies) go to an idle team
    let mut teams: Vec<(String, Vec<String>, String, bool)> = Vec::new(); // (name, worm JSON, inventory JSON, idle)
    let mut team_of: HashMap<u32, usize> = HashMap::new();
    let (mut idle, mut ai) = (None::<usize>, 2);
    for (c, a) in &init.calls {
        if c == "CopyContainer" { if let Some(l) = arg(a, 0).strip_prefix("AIParams.CPU").and_then(|l| l.parse().ok()) { ai = l; } }
    }
    let mut kill_names = Vec::new();
    for (c, a) in &init.calls {
        match c.as_str() {
            "lib_SetupTeam" => if let (Some(i), Some(d)) = (num(a, 0), bank.get(&arg(a, 1), "TeamDataContainer")) {
                let mut p = 3;
                let n = bank.s(vi(d, &mut p));
                team_of.insert(i as u32, teams.len());
                teams.push((if n.is_empty() { "Heroes".into() } else { n }, Vec::new(), "{}".into(), false));
            },
            "lib_SetupWorm" => if let Some(d) = bank.get(&arg(a, 1), "WormDataContainer") {
                let w = worm(&bank, d);
                if w.spawn.is_empty() { continue; }
                let t = if !w.turns && w.team == 0 {
                    *idle.get_or_insert_with(|| { teams.push(("Captives".into(), Vec::new(), "{}".into(), true)); teams.len() - 1 })
                } else {
                    let n = teams.len();
                    *team_of.entry(w.team).or_insert_with(|| { teams.push((format!("Team {}", n + 1), Vec::new(), "{}".into(), false)); n })
                };
                if t != 0 { kill_names.push(w.name.clone()); }
                let name = if w.name.is_empty() || w.name == "\"\"" { String::new() } else { format!("\"name\": {}, ", esc(&w.name)) };
                teams[t].1.push(format!("{{{name}\"hp\": {}, \"pos\": {}}}", w.hp.clamp(1, 999), esc(&w.spawn)));
            },
            "lib_SetupTeamInventory" => if let (Some(i), Some(d)) = (num(a, 0), bank.get(&arg(a, 1), "WeaponInventory")) {
                if let Some(&t) = team_of.get(&(i as u32)) { teams[t].2 = inventory(d); }
            },
            _ => {}
        }
    }
    teams.retain(|t| !t.1.is_empty());
    if teams.is_empty() || teams.len() > 4 { return Err(format!("{} teams", teams.len())); }

    // objects: crates/triggers spawned at start, or the first of a SpawnNext* sequence
    let crate_obj = |name: &str| -> Option<(String, bool)> {
        let d = bank.get(name, "CrateDataContainer")?;
        let mut p = 3;
        let (ty, c) = (bank.s(vi(d, &mut p)).to_lowercase(), bank.s(vi(d, &mut p)));
        p += 20;
        let spawn = bank.s(vi(d, &mut p));
        if spawn.is_empty() { return None; }
        let target = ty == "target";
        let w = if ty == "health" { "health".into() } else { content(&c) };
        Some((if target { format!("{{\"type\": \"target\", \"pos\": {}}}", esc(&spawn)) }
              else { format!("{{\"type\": \"crate\", \"pos\": {}, \"weapon\": {}}}", esc(&spawn), esc(if w.is_empty() { "health" } else { &w })) }, target))
    };
    let trigger = |name: &str| -> Option<(String, f32)> {
        let d = bank.get(name, "TriggerDataContainer")?;
        let mut p = 3;
        let spawn = bank.s(vi(d, &mut p));
        Some((spawn, (f32le(d, p) / 20.0).clamp(1.0, 6.0))).filter(|s| !s.0.is_empty())
    };
    let mut objs: Vec<(String, bool)> = Vec::new();
    let mut trigs: Vec<(String, f32)> = Vec::new();
    for (c, a) in &init.calls {
        if c == "lib_SpawnCrate" { objs.extend(crate_obj(&arg(a, 0))); }
        if c == "lib_SpawnTrigger" { trigs.extend(trigger(&arg(a, 0))); }
    }
    // lib_SpawnCrate(list[i]) outside Initialise: one at a time, from that function's crate names (or Initialise's table)
    let mut sequence = false;
    let mut names: Vec<&String> = funcs.iter().filter(|(n, f)| *n != "Initialise" && f.calls.iter().any(|(c, a)| c == "lib_SpawnCrate" && !matches!(a.first(), Some(Val::Str(_)))))
        .map(|(n, _)| n).collect();
    names.sort();
    if let Some(f) = names.first().and_then(|n| funcs.get(*n)) {
        let spawned: HashSet<String> = init.calls.iter().filter(|(c, _)| c == "lib_SpawnCrate").map(|(_, a)| arg(a, 0)).collect();
        let mut list: Vec<(String, bool)> = f.strings.iter().filter_map(|s| crate_obj(s)).collect();
        if list.is_empty() { list = init.strings.iter().filter(|s| !spawned.contains(*s)).filter_map(|s| crate_obj(s)).collect(); }
        if !list.is_empty() && objs.is_empty() { objs = list; sequence = true; }
    }
    let place = init.calls.iter().any(|(c, a)| c == "SendMessage" && arg(a, 0) == "GameLogic.PlaceObjects");

    // win / lose: which engine callbacks lead to Success / Failure
    let kind = if lv.kind == 4 { "Mission" } else { "Challenge" };
    let win = |cb: &str| reaches(&funcs, cb, &format!("GameLogic.{kind}.Success"), &mut HashSet::new());
    let lose = |cb: &str| reaches(&funcs, cb, &format!("GameLogic.{kind}.Failure"), &mut HashSet::new());
    let targets = objs.iter().filter(|o| o.1).count();
    let crates = objs.len() - targets;
    let total = |set: Option<f32>, n: usize| set.map_or(n, |v| v as usize).max(1);
    let counter = init.sets.iter().find(|(k, _)| k.starts_with("Total") || k.starts_with("Targets")).and_then(|(_, v)| if let Val::Num(n) = v { Some(*n) } else { None });
    let mut goals = Vec::new();
    if win("Crate_Destroyed") && targets > 0 { goals.push(format!("{{\"type\": \"destroy\", \"count\": {}}}", total(counter, targets).min(targets))); }
    else if win("Crate_Collected") && crates > 0 { goals.push(format!("{{\"type\": \"collect\", \"count\": {}}}", total(counter, crates).min(crates))); }
    else if (win("Trigger_Destroyed") || funcs.contains_key("Trigger_Destroyed") && !win("Trigger_Collected")) && !trigs.is_empty() {
        objs.extend(trigs.drain(..).map(|(s, _)| (format!("{{\"type\": \"target\", \"pos\": {}}}", esc(&s)), true)));
        goals.push(format!("{{\"type\": \"destroy\", \"count\": {}}}", objs.iter().filter(|o| o.1).count()));
    } else if win("Trigger_Collected") && !trigs.is_empty() {
        let (s, r) = &trigs[0];
        goals.push(format!("{{\"type\": \"reach\", \"pos\": {}, \"radius\": {r:.1}}}", esc(s)));
    } else if win("Timer_GameTimedOut") {
        goals.push(format!("{{\"type\": \"survive\", \"seconds\": {}}}", (set("RoundTime").unwrap_or(300000.0) / 1000.0) as i32));
    } else if lv.script.contains("Accuracy") || kill_names.is_empty() && idle.is_some() {
        goals.push("{\"type\": \"poison_all\"}".into());
    } else if !kill_names.is_empty() { goals.push("{\"type\": \"kill_all\"}".into()); }
    else if !trigs.is_empty() {
        objs.extend(trigs.drain(..).map(|(s, _)| (format!("{{\"type\": \"target\", \"pos\": {}}}", esc(&s)), true)));
        goals.push(format!("{{\"type\": \"destroy\", \"count\": {}}}", objs.iter().filter(|o| o.1).count()));
    }
    if goals.is_empty() { return Err("no objective found".into()); }
    let mut fails = Vec::new();
    if lose("Worm_Damaged_Current") { fails.push("{\"type\": \"hurt\"}".to_string()); }
    let round = (set("RoundTime").unwrap_or(600000.0) / 1000.0) as i32;
    if lose("Timer_GameTimedOut") && !win("Timer_GameTimedOut") && round > 0 { fails.push(format!("{{\"type\": \"time\", \"seconds\": {round}}}")); }

    let t = |k: &str| lang.get(k).cloned().unwrap_or_default();
    let name = Some(t(&lv.name)).filter(|s| !s.is_empty()).unwrap_or(lv.file.clone());
    let secs = |k: &str, def: f32| set(k).map_or(def, |v| v / 1000.0).clamp(0.0, 255.0) as i32;
    let turn = secs("TurnTime", 45.0);
    let (campaign, kindj) = match lv.kind { 4 => ("W4M Story", "mission"), 9 => ("W4M Deathmatch", "challenge"), _ => ("W4M Challenges", "challenge") };
    let team_json: Vec<String> = teams.iter().enumerate().map(|(i, (n, w, inv, idle))| {
        let cpu = if i == 0 || *idle { 0 } else if lv.kind == 4 { 1 + (lv.index / 20).min(2) } else { ai };
        format!("    {{\"name\": {}, \"cpu\": {cpu}, {}\"weapons\": {inv},\n     \"worms\": [{}]}}", esc(n), if *idle { "\"idle\": true, " } else { "" }, w.join(", "))
    }).collect();
    let preview = format!("levels/{}", lv.preview.to_lowercase().trim_end_matches(".tga"));
    // Lua Initialise may force the weather odds (FlowControlService reads them first, 0x4e73c0)
    let rain = set("Particle.Rain.Prob").map_or(String::new(), |p| format!("  \"rain_prob\": {p},\n"));
    let done = t(if lv.kind == 4 { "FETXT.MissionCompleteBody" } else { "FETXT.ChallengeCompleteBody" });
    Ok(format!(
        "{{\n  \"name\": {},\n  \"kind\": \"{kindj}\",\n  \"campaign\": \"{campaign}\",\n  \"order\": {order},\n  \"map\": {},\n  \"preview\": {},\n  \"par\": {},\n  \"brief\": {},\n  \"success\": {},\n  \"turn_time\": {turn},\n  \"retreat_time\": {},\n  \"hot_seat\": {},\n  \"wind\": {},\n  \"crate_chance\": 0,\n  \"place_objects\": {place},\n  \"sequence\": {sequence},\n{rain}  \"teams\": [\n{}\n  ],\n  \"objects\": [{}],\n  \"objectives\": [{}],\n  \"fail\": [{}]\n}}\n",
        esc(&name), esc(&stem), esc(&preview), lv.par, esc(&t(&lv.brief)), esc(&done), secs("RetreatTime", 3.0).min(10), secs("HotSeatTime", 0.0).min(10),
        if set("Wind.Speed") == Some(0.0) || turn == 0 { 0 } else { 1 }, team_json.join(",\n"),
        objs.iter().map(|o| o.0.clone()).collect::<Vec<_>>().join(", "), goals.join(", "), fails.join(", ")))
}

// Writes <out>/<file>.json for every story mission (W4M type 4), challenge (8) and deathmatch (9) whose map was imported.
pub fn import(data: &Path, maps: &Path, out: &Path) {
    let lang = language(data);
    let mut list: Vec<Level> = levels(data).into_iter().filter(|l| matches!(l.kind, 4 | 8 | 9) && !l.file.to_lowercase().ends_with("-w3d")).collect();
    list.sort_by_key(|l| (l.kind, l.index));
    let _ = fs::create_dir_all(out);
    let (mut ok, mut seen) = (0, HashSet::new());
    for (i, lv) in list.iter().enumerate() {
        if !seen.insert(lv.file.to_lowercase()) { continue; }
        match mission(data, lv, &lang, maps, i) {
            Ok(j) => { ok += 1; let _ = fs::write(out.join(format!("{}.json", lv.file)), j); }
            Err(e) => println!("mission {}: skipped ({e})", lv.file),
        }
    }
    println!("{ok}/{} missions imported", list.len());
}
