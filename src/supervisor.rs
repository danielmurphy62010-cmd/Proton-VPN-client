use crate::{config::{Config,Mode},metrics::Metrics};
use anyhow::Result;
use std::{collections::HashMap,path::PathBuf,sync::{Arc,Mutex},time::Duration};
use tokio::{process::{Child,Command},net::TcpListener};

pub async fn run(shared:Arc<Mutex<Config>>)->Result<()> {
 let mut tor:Option<Child>=None;let mut stats:HashMap<String,Metrics>=HashMap::new();
 let cfg=shared.lock().unwrap().clone();
 if cfg.tor_exe.exists(){tor=Command::new(&cfg.tor_exe).args(["--SocksPort","127.0.0.1:9050"]).kill_on_drop(true).spawn().ok();}
 let bind=cfg.proxy_bind.clone();
 tokio::spawn(async move {if let Err(e)=proxy(bind).await{tracing::error!(%e,"proxy stopped")}});
 loop {
  let c=shared.lock().unwrap().clone();
  if tor.as_mut().map(|p|p.try_wait().ok().flatten().is_some()).unwrap_or(true)&&c.tor_exe.exists(){tor=Command::new(&c.tor_exe).args(["--SocksPort","127.0.0.1:9050"]).kill_on_drop(true).spawn().ok();}
  for p in c.profiles.iter().filter(|p|p.enabled){let m=stats.entry(p.id.clone()).or_default();let path=PathBuf::from("data").join(format!("{}.json",p.id));let _=m.save(&path);}
  let seconds=if c.benchmark_enabled{c.benchmark_ping_seconds}else{c.ping_seconds};
  tokio::time::sleep(Duration::from_secs(seconds.max(1))).await;
  let _=c.mode==Mode::Lan;
 }
}
async fn proxy(bind:String)->Result<()> {
 let l=TcpListener::bind(bind).await?;
 loop {let (mut client,_)=l.accept().await?;tokio::spawn(async move {
  use tokio::io::{AsyncReadExt,AsyncWriteExt};let mut b=[0u8;8192];let n=match client.read(&mut b).await{Ok(n)=>n,Err(_)=>return};if n==0{return}
  let req=String::from_utf8_lossy(&b[..n]);let Some(line)=req.lines().next() else{return};
  let Some(dest)=line.strip_prefix("CONNECT ").and_then(|s|s.split_whitespace().next()) else{let _=client.write_all(b"HTTP/1.1 501 Not Implemented\r\n\r\n").await;return};
  let mut upstream=match tokio::net::TcpStream::connect(dest).await{Ok(s)=>s,Err(_)=>{let _=client.write_all(b"HTTP/1.1 502 Bad Gateway\r\n\r\n").await;return}};
  let _=client.write_all(b"HTTP/1.1 200 Connection Established\r\n\r\n").await;let _=tokio::io::copy_bidirectional(&mut client,&mut upstream).await;
 });}
}
