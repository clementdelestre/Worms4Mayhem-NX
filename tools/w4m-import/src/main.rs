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
    ("big_explosion", "weapons", &["ExplosionLarge1"]),
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
    ("tick_slow", "weapons", &["ClockSlow"]),
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
    ("bow_impact", "weapons", &["BowImpact"]),
    ("donkey_impact", "weapons", &["ConcreteDonkeyImpact", "ConcreteDonkeyImpact2", "ConcreteDonkeyImpact3"]),
    ("explosion_boxed", "weapons", &["ExplosionBoxed1"]),  // WXP_ExploArrow_RingDk
    ("fireworks", "global", &["Firework1", "Firework2", "Firework3"]),  // global/FireWorksExplosion
    ("buffalo", "weapons", &["BuffaloOfLies"]),  // weapons/BuffaloOfLies
    ("debris", "weapons", &["Debris1", "Debris2", "Debris3", "Debris4"]),  // weapons/Debris
    ("jetpack", "weapons", &["JetPack"]),  // weapons/JetPack
    ("jetpack_end", "weapons", &["JetPackEnd"]),
    ("fire_loop", "weapons", &["FireLoop"]),  // PARTTWK EmitterSoundFX of map emitters (FEV sound definitions)
    ("steam_loop", "weapons", &["SteamLoop"]),
    ("flies_loop", "weapons", &["FliesLoop"]),
    ("elec_arc", "weapons", &["ElecArc"]),
    ("electric_arcing", "weapons", &["ElectricArcing"]),
    ("storm_cloud", "weapons", &["ThunderClap_3", "ThunderClap_4", "ThunderClap_5", "ThunderClap_1", "ThunderClap_2"]),
    ("hose_into_water", "weapons", &["TapIntoWater"]),
    ("bat_impact", "weapons", &["BaseballBatImpact"]),  // WXP_AbdTelep_Central's EmitterSoundFX
    ("bubble_inflate", "weapons", &["BubbleMachinePlace"]),  // weapons/BubbleMachineInflate
    ("bubble_wobble", "weapons", &["BubbleMachineWobble"]),
    ("bubble_loop", "weapons", &["Bubble1", "Bubble2", "Bubble3", "Bubble4", "Bubble5", "Bubble6"]),  // weapons/BubbleMachineLoop
    ("flood", "weapons", &["RainLoopAmb"]),
    ("flood_rain", "weapons", &["RainLoop"]),  // weapons/FloodRainLoop: the looping layer (WXP_StormClouds' EmitterSoundFX)
    ("flood_thunder", "weapons", &["Thunder"]),  // its oneshot layer, 1.5 s trigger delay
    ("fatkins_bounce", "weapons", &["FatkinsBounce1", "FatkinsBounce2"]),  // WEAPTWK BounceSfx of kWeaponFatkins
    ("banana_bounce", "weapons", &["BananaBombImpact"]),  // kWeaponBananaBomb's
    ("mine_machine", "weapons", &["MineMachineOperate"]),  // MineFactoryLogicEntity's loop
    ("fe_scalehit", "global", &["In_Scalehitxy"]),
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
    ("fe_speech", "frontendsfx", &["In_Speech"]),
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

// category -> (subsound index, line hash), via <bank>.lsd (category -> line hashes) and LIP.txt (hash -> line name).
fn speech_categories(lsd: &str, lip: &str, subs: &[Sample]) -> HashMap<String, Vec<(usize, String)>> {
    let names: HashMap<&str, &str> = lip.lines().filter_map(|l| l.strip_prefix('#')?.split_once(' '))
        .map(|(h, n)| (h, n.trim().trim_end_matches(".txt"))).collect();
    let mut cats: HashMap<String, Vec<(usize, String)>> = HashMap::new();
    let mut cur = String::new();
    for l in lsd.lines().map(str::trim) {
        if let Some(n) = l.strip_prefix("<name>").and_then(|n| n.strip_suffix("</name>")) {
            cur = n.rsplit('/').next().unwrap().to_string();
        } else if let Some(line) = names.get(l) {
            // FSB names are cut to 29 bytes
            let short = &line.as_bytes()[..line.len().min(29)];
            if let Some(i) = subs.iter().position(|s| s.name.as_bytes().eq_ignore_ascii_case(short)) {
                let v = cats.entry(cur.clone()).or_default();
                if !v.iter().any(|x| x.0 == i) { v.push((i, l.to_string())); }
            }
        }
    }
    cats
}

// WormsX.fev (layout: tools/w4m-re/fev.py, docs/w4m/audio.md §12): what the importer needs of each event.
struct Inst { sd: usize, start: f32, looped: bool, vol: f32 }
struct Event { path: String, vol: f32, mode: u32, min: f32, max: f32, cat: String, params: Vec<(String, f32)>, insts: Vec<Inst>, envs: Vec<(u32, Vec<(f32, f32)>)> }
struct Wave { bank: String, index: usize }
struct SoundDef { play: u32, vol: f32, waves: Vec<Wave> }
struct Fev { events: Vec<Event>, sds: Vec<SoundDef>, cats: HashMap<String, f32> }

fn parse_fev(b: &[u8]) -> Fev {
    struct R<'a> { b: &'a [u8], p: usize }
    impl R<'_> {
        fn u(&mut self) -> u32 { let v = u32le(self.b, self.p); self.p += 4; v }
        fn f(&mut self) -> f32 { f32::from_bits(self.u()) }
        fn skip(&mut self, n: usize) { self.p += n; }
        fn s(&mut self) -> String {
            let n = self.u() as usize;
            let v = String::from_utf8_lossy(&self.b[self.p..self.p + n]).trim_end_matches('\0').to_string();
            self.p += n;
            v
        }
    }
    // category gains multiply down the tree; keyed by path below the root, like the events' category strings
    fn cat(r: &mut R, path: &str, gain: f32, out: &mut HashMap<String, f32>) {
        let name = r.s();
        let g = gain * r.f();
        r.skip(12);
        let full = if path.is_empty() { name } else { format!("{path}/{name}") };
        out.insert(full.split_once('/').map_or(String::new(), |(_, rest)| rest.to_string()), g);
        for _ in 0..r.u() { cat(r, &full, g, out); }
    }
    fn group(r: &mut R, path: &str, out: &mut Vec<Event>) {
        let full = format!("{path}/{}", r.s());
        for _ in 0..r.u() { r.s(); match r.u() { 2 => { r.s(); } _ => r.skip(4) } }
        let (ns, ne) = (r.u(), r.u());
        for _ in 0..ne {
            let kind = r.u();
            let name = r.s();
            r.skip(16);
            let h = r.p;
            let mut e = Event { path: format!("{full}/{name}").trim_start_matches("/Master/").to_string(), vol: f32::from_bits(u32le(r.b, h)),
                                mode: u32le(r.b, h + 0x1c), min: f32::from_bits(u32le(r.b, h + 0x20)), max: f32::from_bits(u32le(r.b, h + 0x24)),
                                cat: String::new(), params: vec![], insts: vec![], envs: vec![] };
            r.skip(0x84);
            // instance: u16 sounddef, f start, f length, u32 start mode, u32 loop mode (0 loop, 1 oneshot, 2 loop to end), ..., f volume at +38
            let inst = |r: &mut R, v: &mut Vec<Inst>| {
                v.push(Inst { sd: u16le(r.b, r.p) as usize, start: f32::from_bits(u32le(r.b, r.p + 2)), looped: u32le(r.b, r.p + 14) != 1,
                              vol: f32::from_bits(u32le(r.b, r.p + 38)) });
                r.skip(58);
            };
            if kind == 0x10 { r.u(); inst(r, &mut e.insts); } else {
                for _ in 0..r.u() {
                    let (ni, nenv) = (u16le(r.b, r.p + 6), u16le(r.b, r.p + 8));
                    r.skip(10);
                    for _ in 0..ni { inst(r, &mut e.insts); }
                    for _ in 0..nenv {
                        r.u(); r.s(); r.u();
                        let flags = r.u();
                        r.u();
                        let pts = (0..r.u()).map(|_| { let x = r.f(); let y = r.f(); r.u(); (x, y) }).collect();
                        r.skip(8);
                        e.envs.push((flags, pts));
                    }
                }
                for _ in 0..r.u() { let n = r.s(); let vel = r.f(); r.skip(20); let k = r.u() as usize; r.skip(4 * k); e.params.push((n, vel)); }
                r.u();
            }
            r.u();
            e.cat = r.s();
            out.push(e);
        }
        for _ in 0..ns { group(r, &full, out); }
    }
    let mut r = R { b, p: 16 };
    let n = r.u() as usize;
    r.skip(8 * n);
    r.s();
    for _ in 0..r.u() { r.skip(16); r.s(); }
    let mut cats = HashMap::new();
    cat(&mut r, "", 1.0, &mut cats);
    let mut events = Vec::new();
    for _ in 0..r.u() { group(&mut r, "", &mut events); }
    let props: Vec<(u32, f32)> = (0..r.u()).map(|_| { let p = (u32le(r.b, r.p), f32::from_bits(u32le(r.b, r.p + 16))); r.skip(70); p }).collect();
    let sds = (0..r.u()).map(|_| {
        r.s();
        let (play, vol) = props[r.u() as usize];
        let waves = (0..r.u()).map(|_| { r.skip(8); r.s(); let bank = r.s(); let index = r.u() as usize; r.u(); Wave { bank, index } }).collect();
        SoundDef { play, vol, waves }
    }).collect();
    Fev { events, sds, cats }
}

// Speech events whose parameter is not "MultiSelect" get no value from the exe (handle lookup 0x6f99a2) and stay at the minimum
// (fmod_event reset 0x10024640): they always play their first instance. Returns "<bank>/<Category>" -> that instance's wave index.
fn fev_fixed_lines(f: &Fev) -> HashMap<String, usize> {
    let mut out = HashMap::new();
    for e in &f.events {
        let Some(rest) = e.path.split_once("Speech/").map(|(_, r)| r.to_string()) else { continue };
        if e.insts.len() < 2 || e.params.iter().any(|p| p.0 == "MultiSelect") { continue; }
        let i = e.insts.iter().min_by(|a, b| a.start.total_cmp(&b.start)).unwrap();
        if let Some(w) = f.sds[i.sd].waves.first() { out.insert(rest, w.index); }
    }
    out
}

// W4M LIP.txt rows "frame,VISEME,<none>" under "#<hash> <line>.txt": per hash, (frame, viseme) with the exe's parse (0x605d40):
// the viseme's first letter A C E F L M O Q R -> 1..8, 0 (Rest); another letter keeps the previous row's
fn lip_rows(lip: &str) -> HashMap<String, Vec<(u16, u8)>> {
    let mut out: HashMap<String, Vec<(u16, u8)>> = HashMap::new();
    let (mut cur, mut v) = (String::new(), 0u8);
    for l in lip.lines().map(str::trim) {
        if let Some(h) = l.strip_prefix('#') {
            cur = h.split(' ').next().unwrap_or("").to_string();
            out.entry(cur.clone()).or_default();
        } else if let Some((fr, rest)) = l.split_once(',') {
            v = match rest.as_bytes().first() {
                Some(b'A') => 1, Some(b'C') => 2, Some(b'E') => 3, Some(b'F') => 4, Some(b'L') => 5, Some(b'M') => 6,
                Some(b'O') => 7, Some(b'Q') => 8, Some(b'R') => 0, _ => v,
            };
            if let (Ok(f), Some(rows)) = (fr.trim().parse::<u16>(), out.get_mut(&cur)) { rows.push((f, v)); }
        }
    }
    out
}

fn lip_line(name: &str, rows: &[(u16, u8)]) -> String {
    rows.iter().fold(name.to_string(), |s, (f, v)| format!("{s} {f}:{v}"))
}

// efmv/<group>/events.txt, one line per event: name, gain dB, loop, 3D linear min max (m, 0 0: 2D), sounddef play mode, wave count,
// then the volume envelope on its Time parameter if any: rate (share of the range per second) and x,y points;
// efmv/<group>/lip.txt: the event's line (its .lsd hash, the first: no EFMV event has a MultiSelect parameter) as LIP rows
fn efmv_index(f: &Fev, efmv: &[&Event], game: &Path, out: &Path) {
    let (lsds, lipdirs) = (index(&game.join("Data/Audio/EFMV")), index(&game.join("EFMV")));
    let mut groups: std::collections::BTreeMap<&str, (String, String)> = Default::default();
    for e in efmv {
        let (group, name) = e.path[5..].split_once('/').unwrap();
        let (i, sd) = (&e.insts[0], &f.sds[e.insts[0].sd]);
        let gain = e.vol * sd.vol * i.vol * f.cats.get(&e.cat).copied().unwrap_or(1.0);
        let (min, max) = if e.mode & 0x10 != 0 { (e.min / 20.0, e.max / 20.0) } else { (0.0, 0.0) };
        let mut l = format!("{} {:.2} {} {min} {max} {} {}", safe(name), if gain > 0.0 { 20.0 * gain.log10() } else { -99.0 }, i.looped as u8, sd.play, sd.waves.len());
        if let (Some((_, rate)), Some((_, pts))) = (e.params.iter().find(|p| p.0 == "Time"), e.envs.iter().find(|v| v.0 == 12)) {
            l += &pts.iter().fold(format!(" {rate}"), |s, (x, y)| format!("{s} {x},{y}"));
        }
        groups.entry(group).or_default().0 += &(l + "\n");
    }
    for (group, (events, lips)) in &mut groups {
        let lsd = lsds.get(&format!("{}.lsd", group.to_lowercase())).and_then(|p| fs::read(p).ok()).map(|b| String::from_utf8_lossy(&b).into_owned());
        let lip = lipdirs.get(&group.to_lowercase()).and_then(|d| fs::read(d.join("LIP.txt")).ok()).map(|b| String::from_utf8_lossy(&b).into_owned());
        if let (Some(lsd), Some(lip)) = (lsd, lip) {
            let rows = lip_rows(&lip);
            let mut cur = String::new();
            for l in lsd.lines().map(str::trim) {
                if let Some(n) = l.strip_prefix("<name>").and_then(|n| n.strip_suffix("</name>")) { cur = n.rsplit('/').next().unwrap().to_string(); }
                else if let Some(r) = rows.get(l).filter(|_| !cur.is_empty()) { *lips += &(lip_line(&safe(&cur), r) + "\n"); cur.clear(); }
            }
        }
        let dir = out.join("efmv").join(group);
        fs::create_dir_all(&dir).unwrap();
        fs::write(dir.join("events.txt"), &events).unwrap();
        fs::write(dir.join("lip.txt"), &lips).unwrap();
    }
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

    let fev = fs::read(pc.join("WormsX.fev")).map(|b| parse_fev(&b)).ok();
    let fixed = fev.as_ref().map(fev_fixed_lines).unwrap_or_default();
    // EFMV/<group>/<event> whose waves are all on disk (the Outtake* banks are not)
    let efmv: Vec<&Event> = fev.iter().flat_map(|f| &f.events).filter(|e| e.path.starts_with("EFMV/") && !e.insts.is_empty() &&
        fev.as_ref().unwrap().sds[e.insts[0].sd].waves.iter().all(|w| banks.iter().any(|(b, _)| b.eq_ignore_ascii_case(&w.bank)))).collect();
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
            let rows = lip_rows(&lip);
            let dir = out.join("voices").join(bank.strip_prefix("vo").unwrap_or(bank));
            fs::create_dir_all(&dir).unwrap();
            let mut lips = String::new();
            for (name, cat) in VOICES {
                let only = fixed.get(&format!("{bank}/{cat}"));
                let lines = cats.get(*cat).into_iter().flatten().filter(|x| only.is_none_or(|&w| w == x.0));
                for (k, (i, hash)) in lines.enumerate() {
                    queue(&subs, *i, variant(&dir, name, k), &mut jobs);
                    let stem = variant(&dir, name, k).file_stem().unwrap().to_string_lossy().into_owned();
                    if let Some(r) = rows.get(hash) { lips += &(lip_line(&stem, r) + "\n"); }
                }
            }
            fs::write(dir.join("lip.txt"), lips).unwrap();
        }
        for e in &efmv {
            let (group, name) = e.path[5..].split_once('/').unwrap();
            let dir = out.join("efmv").join(group);
            for (k, w) in fev.as_ref().unwrap().sds[e.insts[0].sd].waves.iter().enumerate() {
                if !w.bank.eq_ignore_ascii_case(bank) { continue; }
                if w.index >= subs.len() { eprintln!("{}: wave {} past {bank}", e.path, w.index); continue; }
                fs::create_dir_all(&dir).unwrap();
                queue(&subs, w.index, variant(&dir, &safe(name), k), &mut jobs);
            }
        }
    }
    if list { return; }
    if let Some(f) = &fev { efmv_index(f, &efmv, &game, &out); }

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
    fn fev_walk_finds_the_unparameterised_speech_events() {
        // needs the user's install; the walk must reach the 14 events named in docs/w4m/audio.md §12
        let Some(dir) = std::env::var_os("W4M_DIR") else { return };
        let b = fs::read(Path::new(&dir).join("Data/Audio/PC/WormsX.fev")).unwrap();
        let f = fev_fixed_lines(&parse_fev(&b));
        assert_eq!(f.len(), 14, "{f:?}");
        assert!(f.contains_key("vobuild/StartTurn") && f.contains_key("voklein/NoDamageA"));
    }

    #[test]
    fn speech_lookup_handles_truncated_names() {
        let s = |name: &str| Sample { name: name.into(), samples: 1, mode: 0, freq: 1, channels: 1, data: &[] };
        let subs = [s("cheer 1"), s("a gift from home bah just mor")];
        let lsd = "<name>Speech/voalien/Cheer</name>\r\n1\r\n;\r\n<name>Speech/voalien/Taunt</name>\r\n2\r\n3\r\n;";
        let lip = "#1 cheer 1.txt\n0,A,<none>\n#2 a gift from home bah just more junk.txt\n#3 a gift from home bah just more junk 2.txt";
        let c = speech_categories(lsd, lip, &subs);
        assert_eq!(c["Cheer"], [(0, "1".to_string())]);
        assert_eq!(c["Taunt"], [(1, "2".to_string())]);
    }

    #[test]
    fn lip_rows_follow_the_exe_parse() {
        let r = lip_rows("#7 a.txt\n0,CONS,<none>\n4,Rest,<none>\n\n6,X,<none>\n9,QUW,<none>\n#8 b.txt\n0,MBP,<none>");
        assert_eq!(r["7"], [(0, 2), (4, 0), (6, 0), (9, 8)]);
        assert_eq!(lip_line("b", &r["8"]), "b 0:6");
    }

    #[test]
    fn fixed_line_waves_are_bank_indices() {
        // needs the user's install: the FEV wave index of a fixed line is its subsound in the bank
        let Some(dir) = std::env::var_os("W4M_DIR") else { return };
        let pc = Path::new(&dir).join("Data/Audio/PC");
        let f = parse_fev(&fs::read(pc.join("WormsX.fev")).unwrap());
        let e = f.events.iter().find(|e| e.path == "Speech/vobuild/StartTurn").unwrap();
        let i = e.insts.iter().min_by(|a, b| a.start.total_cmp(&b.start)).unwrap();
        let bank = fs::read(index(&pc)["vobuild.fsb"].clone()).unwrap();
        let subs = parse_fsb4(&bank).unwrap();
        assert_eq!(fev_fixed_lines(&f)["vobuild/StartTurn"], f.sds[i.sd].waves[0].index);
        assert!(subs.len() > f.sds[i.sd].waves[0].index);
        assert_eq!(f.cats["Speech/EFMVDialogue"], 1.0);
    }
}
