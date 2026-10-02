// Converts Worms 4 Mayhem / Ultimate Mayhem FMOD banks (FSB4) into the client's assets/ layout.
use std::collections::HashMap;
use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Mutex;

struct Sample<'a> {
    name: String,
    samples: u32,
    mode: u32,
    freq: u32,
    channels: u16,
    data: &'a [u8],
}

impl Sample<'_> {
    fn secs(&self) -> f32 { self.samples as f32 / self.freq as f32 }
}

fn u16le(b: &[u8], o: usize) -> u16 { u16::from_le_bytes([b[o], b[o + 1]]) }
fn u32le(b: &[u8], o: usize) -> u32 { u32::from_le_bytes(b[o..o + 4].try_into().unwrap()) }

fn parse_fsb4(b: &[u8]) -> Result<Vec<Sample<'_>>, String> {
    if b.len() < 48 || &b[0..4] != b"FSB4" { return Err("not FSB4".into()); }
    let (n, shdr) = (u32le(b, 4) as usize, u32le(b, 8) as usize);
    if u32le(b, 20) & 0x02 != 0 { return Err("basic headers unsupported".into()); }
    let (mut h, mut d) = (48, 48 + shdr);
    let mut out = Vec::with_capacity(n);
    for _ in 0..n {
        let hdr = b.get(h..h + 80).ok_or("truncated header")?;
        let comp = u32le(hdr, 36) as usize;
        out.push(Sample {
            name: String::from_utf8_lossy(&hdr[2..32]).trim_end_matches('\0').to_string(),
            samples: u32le(hdr, 32),
            mode: u32le(hdr, 48),
            freq: u32le(hdr, 52),
            channels: u16le(hdr, 62),
            data: b.get(d..d + comp).ok_or("truncated data")?,
        });
        h += u16le(hdr, 0) as usize;
        d = (d + comp + 31) & !31; // each subsound starts 32-byte aligned
    }
    Ok(out)
}

fn codec(mode: u32) -> &'static str {
    match mode {
        m if m & 0x200 != 0 => if m & 0x40000 != 0 { "mp2" } else { "mp3" },
        m if m & 0x400000 != 0 => "ima",
        m if m & 0x1000000 != 0 => "xma",
        m if m & 0x8 != 0 => "pcm8",
        _ => "pcm16",
    }
}

// FSB "MPEG padded" banks pad each frame with zeros to a 2/4-byte boundary; drop the padding.
fn mpeg_frames(b: &[u8]) -> Vec<u8> {
    const BR1: [u32; 16] = [0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0]; // MPEG1 L2
    const BR13: [u32; 16] = [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0]; // MPEG1 L3
    const BR2: [u32; 16] = [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0]; // MPEG2 L2/L3
    let mut out = Vec::with_capacity(b.len());
    let mut i = 0;
    while i + 4 <= b.len() {
        if b[i] != 0xFF || b[i + 1] & 0xE0 != 0xE0 { i += 1; continue; }
        let (v1, l3) = (b[i + 1] & 0x08 != 0, (b[i + 1] >> 1) & 3 == 1);
        let br = if !v1 { BR2 } else if l3 { BR13 } else { BR1 }[(b[i + 2] >> 4) as usize] * 1000;
        let sr = [44100, 48000, 32000, 0][((b[i + 2] >> 2) & 3) as usize] >> (!v1 as u32);
        if br == 0 || sr == 0 { i += 1; continue; }
        let len = (if l3 && !v1 { 72 } else { 144 } * br / sr + ((b[i + 2] >> 1) & 1) as u32) as usize;
        out.extend_from_slice(&b[i..(i + len).min(b.len())]);
        i += len;
    }
    out
}

fn wav(s: &Sample) -> Vec<u8> {
    let (ch, rate, n) = (s.channels as u32, s.freq, s.data.len() as u32);
    let mut w = Vec::with_capacity(44 + s.data.len());
    for part in [&b"RIFF"[..], &(36 + n).to_le_bytes(), b"WAVEfmt ", &16u32.to_le_bytes(), &1u16.to_le_bytes(),
        &(ch as u16).to_le_bytes(), &rate.to_le_bytes(), &(rate * ch * 2).to_le_bytes(), &((ch * 2) as u16).to_le_bytes(),
        &16u16.to_le_bytes(), b"data", &n.to_le_bytes(), s.data] {
        w.extend_from_slice(part);
    }
    w
}

// Returns the subsound as a standalone file ffmpeg can read, with its extension.
fn extract(s: &Sample) -> Option<(Vec<u8>, &'static str)> {
    match codec(s.mode) {
        c @ ("mp2" | "mp3") => Some((mpeg_frames(s.data), c)),
        "pcm16" => Some((wav(s), "wav")),
        _ => None,
    }
}

// Lowercased file name -> path, since bank names differ in case between dirs (voBuild.fsb vs speech/vobuild).
fn index(dir: &Path) -> HashMap<String, PathBuf> {
    fs::read_dir(dir).into_iter().flatten().flatten()
        .map(|e| (e.file_name().to_string_lossy().to_lowercase(), e.path())).collect()
}

const SFX: &[(&str, &str, &[&str])] = &[
    ("explosion", "global", &["ExplosionRegular1", "ExplosionRegular2", "ExplosionRegular3"]),
    ("big_explosion", "weapons", &["ExplosionLarge1", "ExplosionBoxed1"]),
    ("fire", "weapons", &["RocketRelease"]),
    ("bounce", "weapons", &["GrenadeImpact1", "GrenadeImpact2", "GrenadeImpact3"]),
    ("splash", "weapons", &["SplashHeavy1", "SplashHeavy2", "SplashHeavy3"]),
    ("sheep", "weapons", &["SheepBaa"]),
    ("holy", "weapons", &["Hallelujah"]),
    ("holy_boom", "weapons", &["HolyGrenadeEx"]),
    ("holy_held", "weapons", &["HolyGrenadeHeld"]),
    ("bomb_whistle", "weapons", &["BombWhistle"]),
    ("throw", "weapons", &["Throw"]),
    ("secret_launch", "weapons", &["SecretWeapLaunch"]),
    ("cow_fall", "weapons", &["CowFall", "CowFall2", "CowFall3"]),
    ("lock_on", "weapons", &["TargetAquired"]),
    ("power_rocket", "weapons", &["RocketPowerUp"]),
    ("power_homing", "weapons", &["HomingMissilePowerUp"]),
    ("power_bow", "weapons", &["BowCreak"]),
    ("equip_air", "weapons", &["AirEquip"]),
    ("equip_bazooka", "weapons", &["BazookaEquip"]),
    ("equip_bubble", "weapons", &["BubbleEquip"]),
    ("equip_default", "weapons", &["DefaultEquip"]),
    ("equip_potion", "weapons", &["PotionEquip"]),
    ("equip_scouser", "weapons", &["ScouserArm"]),
    ("equip_shotgun", "weapons", &["ShotgunEquip"]),
    ("equip_sniper", "weapons", &["SniperEquip"]),
    ("equip_umbrella", "weapons", &["UmbrellaOpen"]),
    ("held_sheep", "weapons", &["SheepBaa"]),
    ("held_sentry", "weapons", &["SentryGunHeld"]),
    ("held_scouser", "weapons", &["ScouserHeld1", "ScouserHeld2", "ScouserHeld3"]),
    ("held_old_woman", "weapons", &["OldWomenHeld1", "OldWomanHeld2", "OldWomanHeld3"]),
    ("turn_start", "weapons", &["HudAlert"]),
    ("tick", "weapons", &["ClockFast"]),
    ("shotgun", "weapons", &["Shotgun1", "Shotgun2"]),
    ("airstrike", "weapons", &["Bomber"]),
    ("donkey", "weapons", &["DonkeyBray"]),
    ("rope", "weapons", &["NinjaRopeFire"]),
    ("teleport", "weapons", &["TeleportOut"]),
    ("bat_swing", "weapons", &["BaseballBatSwing"]),
    ("fire_punch", "weapons", &["FirePunch"]),
    ("prod", "weapons", &["Prod"]),
    ("sniper", "weapons", &["SniperFire"]),
    ("bow", "weapons", &["BomTwang"]),
    ("homing", "weapons", &["MissileWhistle"]),
    ("old_woman", "weapons", &["OldWomanLaunch", "OldWomanMutter1", "OldWomanMutter2", "OldWomanMutter3", "OldWomanMutter4", "OldWomanMutter5"]),
    ("scouser", "weapons", &["ScouserLaunch", "ScouserJump1", "ScouserJump2", "ScouserJump3"]),
    ("sentry_place", "weapons", &["SentryGunHeld"]),
    ("sentry_fire", "weapons", &["SentryGunLoop"]),
    ("dynamite", "weapons", &["Fuse"]),
    ("gas", "weapons", &["GasLoop"]),
    ("abduction", "weapons", &["AlienUFOBeamStart"]),
    ("ufo_appearing", "weapons", &["AlienUFOAppearing"]),
    ("ufo_active", "weapons", &["AlienUFOActive"]),
    ("ufo_beam", "weapons", &["AlienUFOBeamLoop"]),
    ("ufo_engine", "weapons", &["AlienUFOEngine"]),
    ("ufo_takeoff", "weapons", &["AlienUFOTakeOff"]),
    ("bat_impact", "weapons", &["BaseballBatImpact"]),  // WXP_AbdTelep_Central's EmitterSoundFX
    ("bubble_inflate", "weapons", &["BubbleMachinePlace"]),  // weapons/BubbleMachineInflate
    ("bubble_wobble", "weapons", &["BubbleMachineWobble"]),
    ("bubble_loop", "weapons", &["Bubble1", "Bubble2", "Bubble3", "Bubble4", "Bubble5", "Bubble6"]),  // weapons/BubbleMachineLoop
    ("flood", "weapons", &["RainLoopAmb"]),
    ("parachute", "weapons", &["ParachuteOpen"]),
    ("mine_beep", "weapons", &["MineArmLoop"]),
    ("crate_land", "weapons", &["CrateSpawn"]),
    ("crate_impact_health", "weapons", &["CrateHitHealth"]),
    ("crate_impact_weapon", "weapons", &["CrateImpactWeapons"]),
    ("crate_impact_util", "weapons", &["CrateImpactUtil"]),
    ("cheer", "cheer", &["CrowdCheer"]),
    ("pickup", "weapons", &["PickUpAmmo", "PickUpHealth", "PickUpUtility"]),
    ("super_sheep", "weapons", &["WingFlap1", "WingFlap2", "WingFlap3"]),
    // W4M has no worm walk event: the Old Woman's soft footsteps stand in for the shuffle
    ("step", "weapons", &["OldWomenFootstep1", "OldWomenFootstep2", "OldWomenFootstep3", "OldWomenFootStep4", "OldWomenFootstep5"]),
    ("land", "weapons", &["Thud1", "Thud2", "Thud3", "Thud4"]),
    ("hp_tick", "global", &["Click3"]),
    // frontend: WormsMayhem.exe kAUDIO_* (WXFE_ControlAudioEnum) -> frontendsfx/ or global/ events
    ("fe_highlight", "global", &["Highlight"]),
    ("fe_change", "global", &["Click2"]),
    ("fe_click", "frontendsfx", &["Click"]),
    ("fe_cancel", "frontendsfx", &["Cancel"]),
    ("fe_error", "global", &["FEError"]),
    ("fe_type", "global", &["Typewriter"]),
    ("fe_page", "frontendsfx", &["PageTurn"]),
    ("fe_popup_in", "global", &["In_ScaleY"]),
    ("fe_popup_out", "frontendsfx", &["Out_ScaleY"]),
    ("fe_next_in", "frontendsfx", &["In_Next"]),
    ("fe_next_out", "frontendsfx", &["Out_Next"]),
    ("fe_prev_in", "frontendsfx", &["In_Prev"]),
    ("fe_prev_out", "frontendsfx", &["Out_Prev"]),
    ("fe_bounce", "frontendsfx", &["In_Bigbounce"]),
    ("fe_slide", "frontendsfx", &["In_SlideX"]),
    ("fe_net", "frontendsfx", &["In_Net"]),
    ("fe_custom", "frontendsfx", &["In_Custom"]),
    ("fe_soundvid", "frontendsfx", &["In_SoundVid"]),
    ("fe_controller", "global", &["In_Controller"]),
    ("fe_factory", "frontendsfx", &["In_WeaponFactory"]),
    ("fe_book_in", "frontendsfx", &["In_Book"]),
    ("fe_book_out", "frontendsfx", &["Out_Book"]),
    ("fe_grenade", "frontendsfx", &["Grenade"]),
    ("fe_wormpot", "frontendsfx", &["In_WormPot"]),
    ("wormpot_spin", "frontendsfx", &["WormPotLoop"]),
    ("wormpot_stop", "frontendsfx", &["WormPotStop"]),
];
// our voice file -> W4M speech category (Data/Audio/Speech/<bank>.lsd)
const VOICES: &[(&str, &str)] = &[
    ("fire", "WeaponFired"), ("hurt", "FireDamage"), ("death", "FriendlyDeath"),
    ("victory", "Victory"), ("jump", "Jump"), ("idle", "StartTurn"),
    // WORMACTING.XOM scene lines (docs/worm-reactions.md)
    ("startled", "Startled"), ("grenade", "GrenadeLanded"), ("shriek", "Shriek"), ("gasp", "Gasp"), ("shakefist", "ShakeFist"),
    ("titter", "Titter"), ("disbelief", "Disbelief"), ("incoming", "Incoming"), ("missed", "Missed"), ("mistake", "Mistake"),
    ("traitor", "Traitor"), ("damage", "DamageInflictedA"), ("firstblood", "FirstBlood"), ("enemydeath", "EnemyDeath"),
    ("sadsigh", "SadSigh"), ("yawn", "Yawn"), ("sneeze", "Sneeze"), ("clutchchest", "ClutchChest"), ("nooo", "Nooo"),
    ("bounce", "WormBounce"), ("taunt", "Taunt"), ("waiting", "Waiting"), ("shortontime", "ShortOnTime"), ("skipgo", "SkipGo"),
    ("collect", "Collect"), ("cratedrop", "CrateDrop"), ("drown", "ShallowDrown"),
    ("revenge", "Revenge"), ("punch", "Punch"), ("damageb", "DamageInflictedB"), ("nodamagea", "NoDamageA"), ("nodamageb", "NoDamageB"),
    ("maxdamage", "MaxDamage"), ("pointandlaugh", "PointAndLaugh"),
];
// W4M music bank -> our file; map themes use their docs/maps.md name
const MUSIC: &[(&str, &str)] = &[
    ("frontendmusic", "theme"), ("muprehistoric", "jurassic"), ("mucamelot", "camelot"), ("muarabian", "arabian"),
    ("muwildwest", "wildwest"), ("muconstruction", "construction"), ("muarctic", "arctic"), ("muengland", "england"),
    ("muhorror", "horror"), ("mulunar", "lunar"), ("mupirate", "pirate"), ("muwar", "war"),
    ("musuddendeath", "suddendeath"), ("muvictory", "victory"),
];

struct Job { input: Vec<u8>, fmt: &'static str, out: PathBuf, secs: f32, rate: u32 }

// category -> subsound indices, via <bank>.lsd (category -> line hashes) and LIP.txt (hash -> line name).
fn speech_categories(lsd: &str, lip: &str, subs: &[Sample]) -> HashMap<String, Vec<usize>> {
    let names: HashMap<&str, &str> = lip.lines().filter_map(|l| l.strip_prefix('#')?.split_once(' '))
        .map(|(h, n)| (h, n.trim().trim_end_matches(".txt"))).collect();
    let mut cats: HashMap<String, Vec<usize>> = HashMap::new();
    let mut cur = String::new();
    for l in lsd.lines().map(str::trim) {
        if let Some(n) = l.strip_prefix("<name>").and_then(|n| n.strip_suffix("</name>")) {
            cur = n.rsplit('/').next().unwrap().to_string();
        } else if let Some(line) = names.get(l) {
            // FSB names are cut to 29 bytes
            let short = &line.as_bytes()[..line.len().min(29)];
            if let Some(i) = subs.iter().position(|s| s.name.as_bytes().eq_ignore_ascii_case(short)) {
                let v = cats.entry(cur.clone()).or_default();
                if !v.contains(&i) { v.push(i); }
            }
        }
    }
    cats
}

fn variant(dir: &Path, name: &str, i: usize) -> PathBuf {
    dir.join(if i == 0 { format!("{name}.ogg") } else { format!("{name}_{}.ogg", i + 1) })
}

fn safe(name: &str) -> String {
    name.trim().chars().map(|c| if c.is_alphanumeric() || " _-()!.".contains(c) { c } else { '_' }).collect()
}

fn to_ogg(j: &Job) -> Result<(), String> {
    let tmp = j.out.with_extension("tmp.ogg");
    // ffmpeg's mp3 probe wants two frames: a one-frame blip (global/Highlight, Click2) is fed twice, -t cuts the copy
    let once = j.fmt != "wav" && j.secs < 0.05;
    let mut cmd = Command::new("ffmpeg");
    cmd.args(["-y", "-loglevel", "error", "-f", if j.fmt == "wav" { "wav" } else { "mp3" }, "-i", "pipe:0", "-c:a", "libvorbis", "-q:a", "4", "-ar"])
        .arg(j.rate.to_string());
    if once { cmd.arg("-t").arg(j.secs.to_string()); }
    let mut p = cmd.arg(&tmp).stdin(Stdio::piped()).spawn().map_err(|e| format!("ffmpeg: {e}"))?;
    let input = if once { [&j.input[..], &j.input[..]].concat() } else { j.input.clone() };
    p.stdin.take().unwrap().write_all(&input).map_err(|e| e.to_string())?;
    if !p.wait().map_err(|e| e.to_string())?.success() { return Err(format!("ffmpeg failed on {}", j.out.display())); }
    fs::rename(&tmp, &j.out).map_err(|e| e.to_string())
}

fn main() {
    let mut args: Vec<String> = std::env::args().skip(1).collect();
    let flag = |args: &mut Vec<String>, f: &str| args.iter().position(|a| a == f).map(|i| args.remove(i)).is_some();
    let (list, raw) = (flag(&mut args, "--list"), flag(&mut args, "--raw"));
    let Some(game) = args.first().map(PathBuf::from) else {
        eprintln!("usage: w4m-import [--list] [--raw] <W4M install dir> [out dir, default client/assets]");
        std::process::exit(2);
    };
    let out = PathBuf::from(args.get(1).map_or("client/assets", |s| s.as_str()));
    let pc = game.join("Data/Audio/PC");
    let (speech_idx, lip_idx) = (index(&game.join("Data/Audio/Speech")), index(&game.join("speech")));
    let mut banks: Vec<(String, PathBuf)> = index(&pc).into_iter()
        .filter_map(|(n, p)| Some((n.strip_suffix(".fsb")?.to_string(), p))).collect();
    banks.sort();
    if banks.is_empty() { eprintln!("no .fsb under {}", pc.display()); std::process::exit(1); }

    let mut jobs = Vec::new();
    let (mut raw_files, mut raw_bytes, mut skipped) = (0, 0u64, 0);
    let mut queue = |subs: &[Sample], i: usize, out: PathBuf, jobs: &mut Vec<Job>| {
        if out.exists() { skipped += 1; return; }
        match extract(&subs[i]) {
            Some((input, fmt)) => jobs.push(Job { input, fmt, out, secs: subs[i].secs(), rate: subs[i].freq.min(44100) }),
            None => eprintln!("skip {}: codec {}", subs[i].name, codec(subs[i].mode)),
        }
    };
    for (bank, path) in &banks {
        let bytes = fs::read(path).expect("read bank");
        let subs = match parse_fsb4(&bytes) {
            Ok(s) => s,
            Err(e) => { eprintln!("{bank}: {e}"); continue; }
        };
        if list {
            println!("{bank} ({} subsounds)", subs.len());
            for s in &subs {
                println!("  {:<30} {:>5} {}ch {}Hz {:.2}s {}B", s.name, codec(s.mode), s.channels, s.freq, s.secs(), s.data.len());
            }
            continue;
        }
        if raw {
            let dir = out.join("raw").join(bank);
            fs::create_dir_all(&dir).unwrap();
            let mut seen = HashMap::new();
            for s in &subs {
                let Some((data, ext)) = extract(s) else { continue };
                let n = seen.entry(safe(&s.name)).and_modify(|n| *n += 1).or_insert(1);
                let p = dir.join(if *n == 1 { format!("{}.{ext}", safe(&s.name)) } else { format!("{}_{n}.{ext}", safe(&s.name)) });
                if !p.exists() { fs::write(&p, &data).unwrap(); }
                raw_files += 1;
                raw_bytes += data.len() as u64;
            }
        }
        for (name, b, picks) in SFX.iter().filter(|(_, b, _)| b == bank) {
            let dir = out.join("sfx");
            fs::create_dir_all(&dir).unwrap();
            for (k, pick) in picks.iter().enumerate() {
                match subs.iter().position(|s| s.name == *pick) {
                    Some(i) => queue(&subs, i, variant(&dir, name, k), &mut jobs),
                    None => eprintln!("{b}: missing {pick}"),
                }
            }
        }
        if let Some((_, theme)) = MUSIC.iter().find(|(b, _)| b == bank) {
            fs::create_dir_all(out.join("music")).unwrap();
            // frontendmusic also holds a shorter "FrontendDay" loop; the longest track is the main one
            let i = (0..subs.len()).max_by_key(|&i| subs[i].samples).unwrap();
            queue(&subs, i, out.join("music").join(format!("{theme}.ogg")), &mut jobs);
        }
        if let (Some(lsd), Some(lip)) = (speech_idx.get(&format!("{bank}.lsd")), lip_idx.get(bank.as_str())) {
            let lsd = String::from_utf8_lossy(&fs::read(lsd).unwrap()).into_owned();
            let lip = String::from_utf8_lossy(&fs::read(lip.join("LIP.txt")).unwrap_or_default()).into_owned();
            let cats = speech_categories(&lsd, &lip, &subs);
            let dir = out.join("voices").join(bank.strip_prefix("vo").unwrap_or(bank));
            fs::create_dir_all(&dir).unwrap();
            for (name, cat) in VOICES {
                for (k, &i) in cats.get(*cat).into_iter().flatten().enumerate() {
                    queue(&subs, i, variant(&dir, name, k), &mut jobs);
                }
            }
        }
    }
    if list { return; }

    let next = AtomicUsize::new(0);
    let errors = Mutex::new(Vec::new());
    let threads = std::thread::available_parallelism().map_or(4, |n| n.get());
    std::thread::scope(|sc| for _ in 0..threads {
        sc.spawn(|| while let Some(j) = jobs.get(next.fetch_add(1, Ordering::Relaxed)) {
            if let Err(e) = to_ogg(j) { errors.lock().unwrap().push(e); }
        });
    });

    let mut per_dir: std::collections::BTreeMap<String, (usize, f32, f32)> = Default::default();
    for j in &jobs {
        let rel = j.out.strip_prefix(&out).unwrap().parent().unwrap().display().to_string();
        let e = per_dir.entry(rel).or_insert((0, f32::MAX, 0.0));
        *e = (e.0 + 1, e.1.min(j.secs), e.2.max(j.secs));
    }
    for (dir, (n, lo, hi)) in &per_dir { println!("{dir:<24} {n:>4} files  {lo:.2}-{hi:.2}s"); }
    println!("converted {} files, {} already present", jobs.len(), skipped);
    if raw { println!("raw: {raw_files} subsounds ({} MB) under {}", raw_bytes >> 20, out.join("raw").display()); }
    let errors = errors.into_inner().unwrap();
    for e in &errors { eprintln!("{e}"); }
    if !errors.is_empty() { std::process::exit(1); }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mpeg_padding_is_stripped() {
        // MPEG1 L2 160 kbps 44.1 kHz: 522-byte frames, padded to 524 like FSB "MPEG_PADDED4"
        let mut frame = vec![0u8; 522];
        frame[..4].copy_from_slice(&[0xFF, 0xFD, 0x90, 0x04]);
        let padded = [frame.clone(), vec![0, 0], frame.clone(), vec![0, 0]].concat();
        assert_eq!(mpeg_frames(&padded), [frame.clone(), frame].concat());
    }

    #[test]
    fn speech_lookup_handles_truncated_names() {
        let s = |name: &str| Sample { name: name.into(), samples: 1, mode: 0, freq: 1, channels: 1, data: &[] };
        let subs = [s("cheer 1"), s("a gift from home bah just mor")];
        let lsd = "<name>Speech/voalien/Cheer</name>\r\n1\r\n;\r\n<name>Speech/voalien/Taunt</name>\r\n2\r\n3\r\n;";
        let lip = "#1 cheer 1.txt\n0,A,<none>\n#2 a gift from home bah just more junk.txt\n#3 a gift from home bah just more junk 2.txt";
        let c = speech_categories(lsd, lip, &subs);
        assert_eq!(c["Cheer"], [0]);
        assert_eq!(c["Taunt"], [1]);
    }
}
