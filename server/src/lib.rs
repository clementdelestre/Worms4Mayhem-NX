//! Lobby + input relay server for Worms4NX. See PROTOCOL.md. It never simulates the game.
use std::collections::hash_map::RandomState;
use std::collections::{BTreeMap, HashMap};
use std::hash::{BuildHasher, Hasher};
use std::sync::{Arc, Mutex};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream};
use tokio::sync::mpsc::{unbounded_channel, UnboundedSender};

pub const VERSION: u16 = 1;
pub const HELLO: u8 = 0x01;
pub const WELCOME: u8 = 0x02;
pub const ERROR: u8 = 0x03;
pub const LIST_ROOMS: u8 = 0x10;
pub const ROOM_LIST: u8 = 0x11;
pub const CREATE_ROOM: u8 = 0x12;
pub const JOIN_ROOM: u8 = 0x13;
pub const ROOM_STATE: u8 = 0x14;
pub const LEAVE: u8 = 0x15;
pub const START: u8 = 0x20;
pub const INPUTS: u8 = 0x21;
pub const TURN_END: u8 = 0x22;
pub const DESYNC: u8 = 0x23;
pub const CHAT: u8 = 0x30;
pub const PING: u8 = 0x31;
pub const PONG: u8 = 0x32;

/// Frame builder: `u16 len | type | payload`, little-endian.
pub struct W(pub Vec<u8>);
impl W {
    pub fn new(ty: u8) -> W { W(vec![0, 0, ty]) }
    pub fn u8(mut self, v: u8) -> W { self.0.push(v); self }
    pub fn u16(mut self, v: u16) -> W { self.0.extend(v.to_le_bytes()); self }
    pub fn u32(mut self, v: u32) -> W { self.0.extend(v.to_le_bytes()); self }
    pub fn u64(mut self, v: u64) -> W { self.0.extend(v.to_le_bytes()); self }
    pub fn bytes(mut self, v: &[u8]) -> W { self.0.extend(v); self }
    pub fn str(self, s: &str) -> W {
        let b = &s.as_bytes()[..s.len().min(255)];
        self.u8(b.len() as u8).bytes(b)
    }
    pub fn done(mut self) -> Vec<u8> {
        let n = (self.0.len() - 2) as u16;
        self.0[..2].copy_from_slice(&n.to_le_bytes());
        self.0
    }
}

/// Payload reader; every getter returns None on truncation.
pub struct R<'a>(pub &'a [u8]);
impl<'a> R<'a> {
    pub fn take(&mut self, n: usize) -> Option<&'a [u8]> {
        if self.0.len() < n { return None; }
        let (a, b) = self.0.split_at(n);
        self.0 = b;
        Some(a)
    }
    pub fn u8(&mut self) -> Option<u8> { Some(self.take(1)?[0]) }
    pub fn u16(&mut self) -> Option<u16> { Some(u16::from_le_bytes(self.take(2)?.try_into().ok()?)) }
    pub fn u32(&mut self) -> Option<u32> { Some(u32::from_le_bytes(self.take(4)?.try_into().ok()?)) }
    pub fn u64(&mut self) -> Option<u64> { Some(u64::from_le_bytes(self.take(8)?.try_into().ok()?)) }
    pub fn str(&mut self) -> Option<String> {
        let n = self.u8()? as usize;
        Some(String::from_utf8_lossy(self.take(n)?).into_owned())
    }
}

type Tx = UnboundedSender<Vec<u8>>;

struct Player {
    name: String,
    token: u64,
    conn: u64,
    tx: Option<Tx>,
    room: Option<u32>,
}

struct Room {
    name: String,
    max: u8,
    host: u32,
    players: Vec<u32>,
    start: Option<Vec<u8>>, // Start frame as broadcast, replayed on reconnect
    log: Vec<u8>,           // 4 bytes per tick
    sums: HashMap<u32, u32>,
}

#[derive(Default)]
struct State {
    players: HashMap<u32, Player>,
    rooms: BTreeMap<u32, Room>,
    next: u32,
}

fn error(msg: &str) -> Vec<u8> { W::new(ERROR).str(msg).done() }

impl State {
    fn send(&self, id: u32, f: &[u8]) {
        if let Some(tx) = self.players.get(&id).and_then(|p| p.tx.as_ref()) {
            let _ = tx.send(f.to_vec());
        }
    }

    fn broadcast(&self, room: u32, f: &[u8], except: Option<u32>) {
        for &p in &self.rooms[&room].players {
            if Some(p) != except { self.send(p, f); }
        }
    }

    fn room_state(&self, rid: u32) {
        let r = &self.rooms[&rid];
        let mut w = W::new(ROOM_STATE).u32(rid).u32(r.host).u8(r.players.len() as u8);
        for p in &r.players {
            let pl = &self.players[p];
            w = w.u32(*p).str(&pl.name).u8(pl.tx.is_some() as u8);
        }
        self.broadcast(rid, &w.done(), None);
    }

    fn hello(&mut self, buf: &[u8], conn: u64, tx: &Tx) -> Option<u32> {
        let mut r = R(buf);
        let (ty, ver, name, token) = (r.u8()?, r.u16()?, r.str()?, r.u64()?);
        if ty != HELLO || ver != VERSION {
            let _ = tx.send(error("expected Hello with matching version"));
            return None;
        }
        let known = (token != 0).then(|| self.players.iter().find(|(_, p)| p.token == token).map(|(&id, _)| id)).flatten();
        let id = known.unwrap_or_else(|| {
            self.next += 1;
            let mut h = RandomState::new().build_hasher();
            h.write_u32(self.next);
            // ponytail: SipHash with random keys, not a CSPRNG; use getrandom if tokens must resist guessing
            self.players.insert(self.next, Player { name, token: h.finish() | 1, conn, tx: None, room: None });
            self.next
        });
        let p = self.players.get_mut(&id).unwrap();
        p.conn = conn;
        p.tx = Some(tx.clone());
        let (token, room) = (p.token, p.room);
        self.send(id, &W::new(WELCOME).u32(id).u64(token).done());
        if let Some(rid) = room {
            self.room_state(rid);
            let r = &self.rooms[&rid];
            if let Some(start) = &r.start {
                self.send(id, start);
                for (i, chunk) in r.log.chunks(255 * 4).enumerate() {
                    let f = W::new(INPUTS).u32(i as u32 * 255).u8((chunk.len() / 4) as u8).bytes(chunk).done();
                    self.send(id, &f);
                }
            }
        }
        Some(id)
    }

    fn handle(&mut self, id: u32, buf: &[u8]) {
        if self.handle_msg(id, buf).is_none() { self.send(id, &error("bad message")); }
    }

    fn handle_msg(&mut self, id: u32, buf: &[u8]) -> Option<()> {
        let mut r = R(buf);
        let room = self.players[&id].room;
        match r.u8()? {
            LIST_ROOMS => {
                let mut w = W::new(ROOM_LIST).u8(self.rooms.len().min(255) as u8);
                for (rid, rm) in self.rooms.iter().take(255) {
                    w = w.u32(*rid).str(&rm.name).u8(rm.players.len() as u8).u8(rm.max).u8(rm.start.is_some() as u8);
                }
                self.send(id, &w.done());
            }
            CREATE_ROOM => {
                let (name, max) = (r.str()?, r.u8()?);
                if !(2..=8).contains(&max) { return None; }
                self.leave(id);
                self.next += 1;
                let rid = self.next;
                self.rooms.insert(rid, Room { name, max, host: id, players: vec![], start: None, log: vec![], sums: HashMap::new() });
                self.join(id, rid);
            }
            JOIN_ROOM => {
                let rid = r.u32()?;
                let err = match self.rooms.get(&rid) {
                    None => Some("no such room"),
                    Some(rm) if rm.start.is_some() => Some("match already started"),
                    Some(rm) if rm.players.len() >= rm.max as usize => Some("room full"),
                    _ => None,
                };
                match err {
                    Some(e) => self.send(id, &error(e)),
                    None if room == Some(rid) => {}
                    None => { self.leave(id); self.join(id, rid); }
                }
            }
            LEAVE => self.leave(id),
            START => {
                let rid = room?;
                let (_seed, teams, _per) = (r.u32()?, r.u8()?, r.u8()?);
                let owners: Vec<u32> = (0..teams).map(|_| r.u32()).collect::<Option<_>>()?;
                let rm = self.rooms.get_mut(&rid)?;
                if rm.host != id { self.send(id, &error("only the host can start")); return Some(()); }
                if !(2..=8).contains(&teams) || owners.iter().any(|o| !rm.players.contains(o)) { return None; }
                let f = W::new(START).bytes(&buf[1..]).done();
                rm.start = Some(f.clone());
                rm.log.clear();
                rm.sums.clear();
                self.broadcast(rid, &f, None);
            }
            INPUTS => {
                let rid = room?;
                let (first, n) = (r.u32()?, r.u8()? as usize);
                let inputs = r.take(n * 4)?;
                let rm = self.rooms.get_mut(&rid)?;
                rm.start.as_ref()?;
                if first as usize != rm.log.len() / 4 {
                    let e = format!("Inputs tick {first} != expected {}", rm.log.len() / 4);
                    self.send(id, &error(&e));
                    return Some(());
                }
                rm.log.extend(inputs);
                self.broadcast(rid, &W::new(INPUTS).bytes(&buf[1..]).done(), Some(id));
            }
            TURN_END => {
                let rid = room?;
                let (tick, sum) = (r.u32()?, r.u32()?);
                let rm = self.rooms.get_mut(&rid)?;
                if *rm.sums.entry(tick).or_insert(sum) != sum {
                    self.broadcast(rid, &W::new(DESYNC).u32(tick).done(), None);
                }
            }
            CHAT => {
                let text = r.str()?;
                self.broadcast(room?, &W::new(CHAT).u32(id).str(&text).done(), None);
            }
            PING => self.send(id, &W::new(PONG).u32(r.u32()?).done()),
            _ => return None,
        }
        Some(())
    }

    fn join(&mut self, id: u32, rid: u32) {
        self.rooms.get_mut(&rid).unwrap().players.push(id);
        self.players.get_mut(&id).unwrap().room = Some(rid);
        self.room_state(rid);
    }

    fn leave(&mut self, id: u32) {
        let Some(rid) = self.players.get_mut(&id).and_then(|p| p.room.take()) else { return };
        let rm = self.rooms.get_mut(&rid).unwrap();
        rm.players.retain(|&p| p != id);
        if rm.host == id && !rm.players.is_empty() { rm.host = rm.players[0]; }
        self.after_change(rid);
    }

    /// Deletes the room once nobody online is left in it (offline players go with it).
    fn after_change(&mut self, rid: u32) {
        let rm = &self.rooms[&rid];
        if rm.players.iter().any(|p| self.players[p].tx.is_some()) {
            return self.room_state(rid);
        }
        for p in self.rooms.remove(&rid).unwrap().players {
            self.players.remove(&p);
        }
    }

    fn disconnect(&mut self, id: u32, conn: u64) {
        let Some(p) = self.players.get_mut(&id) else { return };
        if p.conn != conn { return; } // a newer connection took this player over
        p.tx = None;
        match p.room {
            Some(rid) if self.rooms[&rid].start.is_some() => self.after_change(rid),
            _ => { self.leave(id); self.players.remove(&id); }
        }
    }
}

pub async fn serve(listener: TcpListener) {
    let st = Arc::new(Mutex::new(State::default()));
    for conn in 1.. {
        if let Ok((sock, _)) = listener.accept().await {
            tokio::spawn(handle(sock, st.clone(), conn));
        }
    }
}

async fn handle(sock: TcpStream, st: Arc<Mutex<State>>, conn: u64) {
    let _ = sock.set_nodelay(true);
    let (mut rd, mut wr) = sock.into_split();
    let (tx, mut rx) = unbounded_channel::<Vec<u8>>();
    tokio::spawn(async move {
        while let Some(f) = rx.recv().await {
            if wr.write_all(&f).await.is_err() { break; }
        }
    });
    let mut me = None;
    while let Ok(len) = rd.read_u16_le().await {
        let mut buf = vec![0; len as usize];
        if len == 0 || rd.read_exact(&mut buf).await.is_err() { break; }
        let mut s = st.lock().unwrap();
        match me {
            Some(id) => s.handle(id, &buf),
            None => match s.hello(&buf, conn, &tx) {
                Some(id) => me = Some(id),
                None => break,
            },
        }
    }
    if let Some(id) = me { st.lock().unwrap().disconnect(id, conn); }
}
