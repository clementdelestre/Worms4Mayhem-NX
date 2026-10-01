// Usage: worms4nx-server [bind]  (or W4NX_BIND env), default 0.0.0.0:7777
#[tokio::main]
async fn main() {
    let bind = std::env::args().nth(1).or_else(|| std::env::var("W4NX_BIND").ok()).unwrap_or("0.0.0.0:7777".into());
    let listener = tokio::net::TcpListener::bind(&bind).await.unwrap_or_else(|e| panic!("bind {bind}: {e}"));
    println!("worms4nx-server listening on {bind}");
    worms4nx_server::serve(listener).await;
}
