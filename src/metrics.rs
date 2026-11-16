use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use std::{collections::VecDeque, fs, path::Path, time::Duration};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PingPoint {
    pub at: DateTime<Utc>,
    pub latency_ms: Option<f64>,
    pub probe: String,
}
#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct ProfileMetrics {
    pub average_ping_ms: Option<f64>,
    pub ping_count: u64,
    pub successful_connections: u64,
    pub failed_connections: u64,
    pub connection_count: u64,
    pub samples: VecDeque<PingPoint>,
}
impl ProfileMetrics {
    pub fn record_probe(&mut self, duration: Option<Duration>, probe: &str) {
        let latency_ms = duration.map(|d| d.as_secs_f64() * 1000.0);
        self.samples.push_back(PingPoint { at: Utc::now(), latency_ms, probe: probe.into() });
        while self.samples.len() > 200 { self.samples.pop_front(); }
        self.ping_count = self.ping_count.saturating_add(1).min(200);
        let values: Vec<f64> = self.samples.iter().filter_map(|x| x.latency_ms).collect();
        self.average_ping_ms = if values.is_empty() { None }
            else { Some(values.iter().sum::<f64>() / values.len() as f64) };
    }

    pub fn record_connection(&mut self, success: bool) {
        self.connection_count = self.connection_count.saturating_add(1);
        if success { self.successful_connections = self.successful_connections.saturating_add(1); }
        else { self.failed_connections = self.failed_connections.saturating_add(1); }
    }

    pub fn save(&self, path: &Path) -> anyhow::Result<()> {
        if let Some(parent) = path.parent() { fs::create_dir_all(parent)?; }
        let tmp = path.with_extension("json.tmp");
        fs::write(&tmp, serde_json::to_vec_pretty(self)?)?;
        if path.exists() { fs::remove_file(path)?; }
        fs::rename(tmp, path)?;
        Ok(())
    }

    pub fn priority_score(&self, weights: &crate::config::PriorityWeights) -> f64 {
        let latency_component = self.average_ping_ms.map(|ms| 1000.0 / (ms.max(0.1))).unwrap_or(0.0);
        let ratio = self.successful_connections as f64 / (self.failed_connections as f64 + 1.0);
        weights.latency * latency_component
            + weights.successful_connections * self.successful_connections as f64
            - weights.failed_connections * self.failed_connections as f64
            - weights.connection_count * self.connection_count as f64
            + weights.success_failure_ratio * ratio
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rolling_window_and_counter_cap_are_both_enforced() {
        let mut metrics = ProfileMetrics::default();
        for i in 0..205 {
            metrics.record_probe(Some(Duration::from_millis(i)), "test");
        }
        assert_eq!(metrics.ping_count, 200);
        assert_eq!(metrics.samples.len(), 200);
        let first = metrics.samples.front().unwrap().latency_ms.unwrap();
        let last = metrics.samples.back().unwrap().latency_ms.unwrap();
        assert_eq!(first, 5.0);
        assert_eq!(last, 204.0);
        assert_eq!(metrics.average_ping_ms, Some((5.0 + 204.0) / 2.0));
    }

    #[test]
    fn failed_probes_are_retained_but_excluded_from_average() {
        let mut metrics = ProfileMetrics::default();
        metrics.record_probe(Some(Duration::from_millis(25)), "test");
        metrics.record_probe(None, "test");
        assert_eq!(metrics.samples.len(), 2);
        assert_eq!(metrics.ping_count, 2);
        assert_eq!(metrics.average_ping_ms, Some(25.0));
    }
}
