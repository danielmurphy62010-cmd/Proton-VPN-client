use chrono::{DateTime,Utc};
use serde::{Deserialize,Serialize};
use std::{collections::VecDeque,fs,path::Path,time::Duration};
#[derive(Clone,Serialize,Deserialize)]
pub struct Point { pub at:DateTime<Utc>,pub latency_ms:Option<f64> }
#[derive(Default,Serialize,Deserialize)]
pub struct Metrics { pub average_ping_ms:Option<f64>,pub ping_count:u64,pub successful_connections:u64,pub failed_connections:u64,pub connection_count:u64,pub points:VecDeque<Point> }
impl Metrics {
 pub fn ping(&mut self,d:Option<Duration>){let ms=d.map(|x|x.as_secs_f64()*1000.0);self.points.push_back(Point{at:Utc::now(),latency_ms:ms});while self.points.len()>200{self.points.pop_front();}self.ping_count=self.ping_count.saturating_add(1).min(200);let v:Vec<f64>=self.points.iter().filter_map(|p|p.latency_ms).collect();self.average_ping_ms=if v.is_empty(){None}else{Some(v.iter().sum::<f64>()/v.len() as f64)}}
 pub fn save(&self,p:&Path)->anyhow::Result<()>{if let Some(d)=p.parent(){fs::create_dir_all(d)?}fs::write(p,serde_json::to_vec_pretty(self)?)?;Ok(())}
}
