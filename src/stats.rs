//! Per-profile statistics. Pure logic, no I/O, so it can be unit-tested with plain rustc.
//!
//! Meaning of the counters:
//! * `avg_ping_ms` / `ping_count`: latency to the ping target measured through the profile.
//!   `ping_count` stops growing at the cap (default 200). Once capped, every new sample is blended
//!   in with weight 1/cap, so the average becomes an exponential moving average that favours
//!   recent samples instead of freezing.
//! * `connections`: how many times the profile's tunnel has been brought up.
//! * `successes` / `failures`: outcomes of proxied TCP connections routed through the profile.

#[cfg(feature = "serde")]
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

#[derive(Clone, Debug, Default, PartialEq)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
#[cfg_attr(feature = "serde", serde(default))]
pub struct ProfileStats {
    pub avg_ping_ms: f64,
    pub ping_count: u32,
    pub successes: u32,
    pub failures: u32,
    pub connections: u32,
}

pub const DEFAULT_PING_CAP: u32 = 200;

impl ProfileStats {
    /// Record one successful latency sample.
    pub fn record_ping(&mut self, ms: f64, cap: u32) {
        let cap = cap.max(1);
        // If the cap was lowered below the stored count, clamp so the weighting follows the new cap.
        self.ping_count = self.ping_count.min(cap);
        if self.ping_count < cap {
            self.ping_count += 1;
        }
        let n = self.ping_count as f64;
        // First sample: avg is 0 and n is 1, so this yields exactly `ms`.
        self.avg_ping_ms += (ms - self.avg_ping_ms) / n;
    }

    pub fn record_success(&mut self) {
        self.successes = self.successes.saturating_add(1);
    }

    pub fn record_failure(&mut self) {
        self.failures = self.failures.saturating_add(1);
    }

    pub fn record_connection(&mut self) {
        self.connections = self.connections.saturating_add(1);
    }

    pub fn has_latency(&self) -> bool {
        self.ping_count > 0
    }

    /// success : failure ratio expressed as successes / failures, with failures floored at 1 so a
    /// profile with no failures gets a finite, comparable value.
    pub fn ratio(&self) -> f64 {
        self.successes as f64 / (self.failures.max(1) as f64)
    }
}

/// Map of profile id -> stats. A BTreeMap keeps the JSON file stable and diff-friendly.
#[derive(Clone, Debug, Default)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
#[cfg_attr(feature = "serde", serde(transparent))]
pub struct StatsStore {
    pub profiles: BTreeMap<String, ProfileStats>,
}

impl StatsStore {
    pub fn entry(&mut self, id: &str) -> &mut ProfileStats {
        self.profiles.entry(id.to_string()).or_default()
    }

    pub fn get(&self, id: &str) -> Option<&ProfileStats> {
        self.profiles.get(id)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn first_sample_sets_average() {
        let mut s = ProfileStats::default();
        s.record_ping(40.0, 200);
        assert_eq!(s.ping_count, 1);
        assert!((s.avg_ping_ms - 40.0).abs() < 1e-9);
    }

    #[test]
    fn below_cap_is_a_true_mean() {
        let mut s = ProfileStats::default();
        for v in [10.0, 20.0, 30.0, 40.0] {
            s.record_ping(v, 200);
        }
        assert_eq!(s.ping_count, 4);
        assert!((s.avg_ping_ms - 25.0).abs() < 1e-9);
    }

    #[test]
    fn count_stops_at_cap() {
        let mut s = ProfileStats::default();
        for _ in 0..1000 {
            s.record_ping(50.0, 200);
        }
        assert_eq!(s.ping_count, 200);
        assert!((s.avg_ping_ms - 50.0).abs() < 1e-6);
    }

    #[test]
    fn newer_samples_dominate_after_cap() {
        let mut s = ProfileStats::default();
        for _ in 0..200 {
            s.record_ping(100.0, 200);
        }
        // 400 new samples at 20ms. A frozen mean would stay at 100 and a true mean over all 600
        // samples would be 46.7. With weight 1/200 per sample the capped average decays toward 20
        // as 20 + 80 * (199/200)^400, which is about 30.77.
        for _ in 0..400 {
            s.record_ping(20.0, 200);
        }
        assert_eq!(s.ping_count, 200);
        let expected = 20.0 + 80.0 * (199.0f64 / 200.0).powi(400);
        assert!((s.avg_ping_ms - expected).abs() < 1e-6, "avg was {}, expected {}", s.avg_ping_ms, expected);
        assert!(s.avg_ping_ms < 46.0, "must beat a true mean");
    }

    #[test]
    fn lowering_the_cap_clamps_existing_count() {
        let mut s = ProfileStats::default();
        for _ in 0..200 {
            s.record_ping(10.0, 200);
        }
        s.record_ping(10.0, 50);
        assert_eq!(s.ping_count, 50);
    }

    #[test]
    fn ratio_floors_failures_at_one() {
        let mut s = ProfileStats::default();
        s.successes = 9;
        assert!((s.ratio() - 9.0).abs() < 1e-9);
        s.failures = 3;
        assert!((s.ratio() - 3.0).abs() < 1e-9);
    }
}
