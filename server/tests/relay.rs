use std::time::Duration;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream};
use worms4nx_server::*;

struct C(TcpStream);
impl C {
    async fn new(addr: &str, name: &str, token: u64) -> (C, u32, u64) {
        let mut c = C(TcpStream::connect(addr).await.unwrap());
        c.send(W::new(HELLO).u16(VERSION).str(name).u64(token)).await;
        let f = c.expect(WELCOME).await;
        let mut r = R(&f);
        let (id, token) = (r.u32().unwrap(), r.u64().unwrap());
        (c, id, token)
    }
    async fn send(&mut self, w: W) { self.0.write_all(&w.done()).await.unwrap(); }
    async fn recv(&mut self) -> (u8, Vec<u8>) {
        let read = async {
            let n = self.0.read_u16_le().await.unwrap() as usize;
            let mut b = vec![0; n];
            self.0.read_exact(&mut b).await.unwrap();
            (b[0], b[1..].to_vec())
        };
        tokio::time::timeout(Duration::from_secs(2), read).await.expect("timeout")
    }
    /// Skips RoomState/Chat/Teams/Lobby noise until a frame of type `ty`.
    async fn expect(&mut self, ty: u8) -> Vec<u8> {
        loop {
            let (t, b) = self.recv().await;
            if t == ty { return b; }
            assert!(t == ROOM_STATE || t == CHAT || t == TEAMS || t == LOBBY, "got 0x{t:02x} while waiting 0x{ty:02x}");
        }
    }
}

async fn server() -> String {
    let l = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let addr = l.local_addr().unwrap().to_string();
    tokio::spawn(serve(l));
    addr
}

fn inputs(first: u32, n: u8) -> W {
    let mut w = W::new(INPUTS).u32(first).u8(n);
    for i in 0..n { w = w.bytes(&[i, 1, 2, 3, 0, 255]); }
    w
}

#[tokio::test]
async fn full_match() {
    let addr = server().await;
    let (mut a, ida, _) = C::new(&addr, "alice", 0).await;
    let (mut b, idb, tokb) = C::new(&addr, "bob", 0).await;

    a.send(W::new(CREATE_ROOM).str("r1").u8(2)).await;
    let st = a.expect(ROOM_STATE).await;
    let rid = R(&st).u32().unwrap();
    b.send(W::new(LIST_ROOMS)).await;
    let list = b.expect(ROOM_LIST).await;
    assert_eq!(list[0], 1);
    b.send(W::new(JOIN_ROOM).u32(rid)).await;
    let st = b.expect(ROOM_STATE).await;
    assert_eq!(st[8], 2, "two players in room");

    b.send(W::new(START).u32(42).u8(2).u8(1).u32(ida).u32(idb)).await;
    assert_eq!(b.expect(ERROR).await.len() > 1, true, "non-host cannot start");
    a.send(W::new(START).u32(42).u8(2).u8(1).u32(ida).u32(idb)).await;
    let sa = a.expect(START).await;
    assert_eq!(sa, b.expect(START).await);

    a.send(inputs(0, 3)).await;
    a.send(inputs(3, 3)).await;
    let f = b.expect(INPUTS).await;
    assert_eq!(&f[..5], &[0, 0, 0, 0, 3]);
    assert_eq!(R(&b.expect(INPUTS).await).u32(), Some(3));
    a.send(W::new(PANEL).u32(6).u8(1).u8(4)).await;
    assert_eq!(b.expect(PANEL).await, [6, 0, 0, 0, 1, 4], "panel relayed, not logged");
    a.send(inputs(9, 1)).await; // gap
    a.expect(ERROR).await;
    assert_eq!(a.expect(START).await, sa, "rejected sender is resynced");
    assert_eq!(R(&a.expect(REPLAY).await).u32(), Some(6));
    assert_eq!(a.expect(INPUTS).await.len(), 5 + 6 * INPUT_BYTES);

    a.send(W::new(TURN_END).u32(6).u32(0xabc)).await;
    b.send(W::new(TURN_END).u32(6).u32(0xabc)).await;
    a.send(W::new(PING).u32(7)).await;
    assert_eq!(R(&a.expect(PONG).await).u32(), Some(7), "no desync on equal sums");
    b.send(W::new(TURN_END).u32(6).u32(0xdef)).await;
    assert_eq!(R(&a.expect(DESYNC).await).u32(), Some(6));
    assert_eq!(R(&b.expect(DESYNC).await).u32(), Some(6));

    // bob drops and reconnects with his token: same id, Start, then the log from tick 0
    drop(b);
    let st = a.expect(ROOM_STATE).await;
    assert_eq!(*st.last().unwrap(), 0, "bob shown offline");
    let (mut b, id2, _) = C::new(&addr, "bob", tokb).await;
    assert_eq!(id2, idb);
    assert_eq!(b.expect(START).await, sa);
    assert_eq!(R(&b.expect(REPLAY).await).u32(), Some(6));
    let f = b.expect(INPUTS).await;
    assert_eq!(&f[..5], &[0, 0, 0, 0, 6]);
    assert_eq!(f.len(), 5 + 6 * INPUT_BYTES);

    b.send(W::new(CHAT).str("hi")).await;
    let c = a.expect(CHAT).await;
    assert_eq!(R(&c).u32(), Some(idb));

    // the host drops mid-match: bob takes over (CPU and proxied teams are played by the host)
    drop(a);
    let st = b.expect(ROOM_STATE).await;
    assert_eq!(R(&st[4..]).u32(), Some(idb));
}

// a team with `weapon` carries a Weapon Factory weapon (PROTOCOL.md Weapon)
fn teams(list: &[(&str, u8, bool)]) -> W {
    let mut w = W::new(TEAMS).u8(list.len() as u8);
    for (n, cpu, weapon) in list {
        w = (0..6).fold(w.str(n).u8(*cpu), |w, _| w.str("")).u8(0).u8(1).str("Worm").u8(1).u8(0xff).u8(0).u8(*weapon as u8);
        if *weapon { w = (0..13 + 4).fold(w.str("Lobber").u8(0), |w, _| w.u32(0)).u8(1).str("holy").str(""); }
    }
    w
}

fn lobby(order: &[(u32, u8)], map: &str, ready: &[u32]) -> W {
    let mut w = W::new(LOBBY).u8(order.len() as u8);
    for (c, i) in order { w = w.u32(*c).u8(*i); }
    w = w.str(map).u32(5).u8(2).u8(7).u8(8).u32(0).u8(3).u8(ready.len() as u8);
    for c in ready { w = w.u32(*c); }
    w
}

#[tokio::test]
async fn lobby_flow() {
    let addr = server().await;
    let (mut a, ida, _) = C::new(&addr, "alice", 0).await;
    let (mut b, idb, _) = C::new(&addr, "bob", 0).await;
    a.send(W::new(CREATE_ROOM).str("r").u8(4)).await;
    let rid = R(&a.expect(ROOM_STATE).await).u32().unwrap();
    b.send(W::new(JOIN_ROOM).u32(rid)).await;
    a.expect(ROOM_STATE).await;

    // TEAMS reach the other console only, with the sender's id; CPU teams are host-only
    b.send(teams(&[("Bob1", 0, true), ("Bob2", 0, false)])).await;
    let t = a.expect(TEAMS).await;
    assert_eq!(R(&t).u32(), Some(idb));
    assert_eq!(t[4], 2);
    b.send(teams(&[("Bob3", 0, true), ("Cpu", 2, false)])).await;
    b.expect(ERROR).await;
    a.send(teams(&[("Al", 0, false), ("Cpu", 2, false)])).await;
    assert_eq!(R(&b.expect(TEAMS).await).u32(), Some(ida));

    // LOBBY: host only, relayed to the others, kept for late joiners
    b.send(lobby(&[], "x", &[])).await;
    b.expect(ERROR).await;
    let l = lobby(&[(ida, 0), (idb, 0), (idb, 1)], "Island", &[ida]);
    let frame = l.0.clone();
    a.send(l).await;
    let got = b.expect(LOBBY).await;
    assert_eq!(got, frame[3..]);

    // READY goes to the host with the console id
    b.send(W::new(READY).u8(1)).await;
    let rd = a.expect(READY).await;
    assert_eq!((R(&rd).u32(), rd[4]), (Some(idb), 1));

    // late joiner gets every console's teams then the lobby
    let (mut c, idc, _) = C::new(&addr, "carol", 0).await;
    c.send(W::new(JOIN_ROOM).u32(rid)).await;
    let mut seen = vec![];
    loop {
        let (ty, body) = c.recv().await;
        if ty == TEAMS { seen.push(R(&body).u32().unwrap()); }
        if ty == LOBBY { assert_eq!(body, frame[3..]); break; }
    }
    assert_eq!(seen, vec![ida, idb]);

    // host leaves: bob (next in slot order) is host, the last LOBBY is kept, alice's teams are gone
    a.send(W::new(LEAVE)).await;
    loop {
        let st = b.expect(ROOM_STATE).await;
        if R(&st[4..]).u32() == Some(idb) { break; }
    }
    let (mut d, _, _) = C::new(&addr, "dave", 0).await;
    d.send(W::new(JOIN_ROOM).u32(rid)).await;
    let mut seen = vec![];
    loop {
        let (ty, body) = d.recv().await;
        if ty == TEAMS { seen.push(R(&body).u32().unwrap()); }
        if ty == LOBBY { assert_eq!(body, frame[3..]); break; }
    }
    assert_eq!(seen, vec![idb]);
    let _ = idc;
}
