mod config;
mod metrics;
mod supervisor;

use anyhow::Result;
use std::sync::{Arc,Mutex};
use tracing_subscriber::EnvFilter;

fn main()->Result<()> {
 tracing_subscriber::fmt().with_env_filter(EnvFilter::from_default_env()).init();
 let cfg=Arc::new(Mutex::new(config::Config::load()?));
 let rt=tokio::runtime::Builder::new_multi_thread().enable_all().build()?;
 rt.block_on(supervisor::run(cfg))
}
