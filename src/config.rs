use anyhow::{Context,Result};
use directories::ProjectDirs;
use serde::{Deserialize,Serialize};
use std::{fs,path::PathBuf};

#[derive(Clone,Debug,Serialize,Deserialize,PartialEq)]
#[serde(rename_all="snake_case")]
pub enum Mode { Smart,OpenVpn,WireGuard,Stealth,Tor,Lan }
#[derive(Clone,Debug,Serialize,Deserialize)]
pub struct Profile { pub id:String,pub name:String,pub country:String,pub protocol:Mode,pub host:String,pub port:u16,pub enabled:bool,pub wireguard_config:Option<PathBuf> }
#[derive(Clone,Debug,Serialize,Deserialize)]
pub struct Weights { pub latency:f64,pub successes:f64,pub failures:f64,pub connections:f64,pub ratio:f64 }
impl Default for Weights { fn default()->Self { Self{latency:1.0,successes:0.2,failures:1.0,connections:0.1,ratio:0.5} } }
#[derive(Clone,Debug,Serialize,Deserialize)]
pub struct Config {
 pub mode:Mode,pub countries:Vec<String>,pub profiles:Vec<Profile>,pub max_fallback:u8,pub final_fallback:String,
 pub ping_seconds:u64,pub priority:Weights,pub benchmark_enabled:bool,pub benchmark_ping_seconds:u64,pub benchmark_rotate_seconds:u64,pub benchmark_priority:Weights,
 pub tor_exe:PathBuf,pub openvpn_exe:PathBuf,pub wireguard_dir:PathBuf,pub proxy_bind:String,pub proton_api:String
}
impl Default for Config { fn default()->Self { Self{mode:Mode::Smart,countries:vec![],profiles:vec![],max_fallback:5,final_fallback:"tor".into(),ping_seconds:10,priority:Weights::default(),benchmark_enabled:false,benchmark_ping_seconds:1,benchmark_rotate_seconds:30,benchmark_priority:Weights::default(),tor_exe:"./Tor/tor/tor.exe".into(),openvpn_exe:"openvpn.exe".into(),wireguard_dir:"./wireguard".into(),proxy_bind:"127.0.0.1:8080".into(),proton_api:"https://api.protonvpn.ch".into()} } }
impl Config {
 fn path()->PathBuf { ProjectDirs::from("com","Community","ProtonVpnClient").map(|p|p.config_dir().join("config.toml")).unwrap_or("config.toml".into()) }
 pub fn load()->Result<Self> { let p=Self::path(); if !p.exists(){let c=Self::default();c.save()?;return Ok(c)} let s=fs::read_to_string(p)?;let c:Self=toml::from_str(&s).context("invalid config")?;anyhow::ensure!((1..=20).contains(&c.max_fallback));Ok(c) }
 pub fn save(&self)->Result<()> { let p=Self::path();if let Some(d)=p.parent(){fs::create_dir_all(d)?}fs::write(p,toml::to_string_pretty(self)?)?;Ok(()) }
}
