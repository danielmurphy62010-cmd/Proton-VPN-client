//! Configurable priority system. Pure logic, unit-testable with plain rustc.
//!
//! Two strategies:
//! * `Weighted`: every enabled criterion is min-max normalised across the candidates to 0..1
//!   (1 = best according to `prefer`) and the weighted average decides the order.
//! * `Lexicographic`: criteria are compared strictly in list order; the next one only breaks ties.
//!
//! A profile with no latency samples yet is neutral (0.5) for the latency criterion under
//! `Weighted` and sorts last under `Lexicographic`, so unmeasured profiles are neither promoted
//! nor starved; the background prober fills in their data.

use crate::stats::ProfileStats;
use std::cmp::Ordering;

#[cfg(feature = "serde")]
use serde::{Deserialize, Serialize};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
#[cfg_attr(feature = "serde", serde(rename_all = "lowercase"))]
pub enum Metric {
    Latency,
    Successes,
    Failures,
    Connections,
    Ratio,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
#[cfg_attr(feature = "serde", serde(rename_all = "lowercase"))]
pub enum Prefer {
    Lower,
    Higher,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
#[cfg_attr(feature = "serde", serde(rename_all = "lowercase"))]
pub enum Strategy {
    Weighted,
    Lexicographic,
}

#[derive(Clone, Debug, PartialEq)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
pub struct Criterion {
    pub metric: Metric,
    pub prefer: Prefer,
    pub weight: f64,
    pub enabled: bool,
}

#[derive(Clone, Debug, PartialEq)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
#[cfg_attr(feature = "serde", serde(default))]
pub struct Priority {
    pub strategy: Strategy,
    pub criteria: Vec<Criterion>,
}

impl Default for Priority {
    fn default() -> Self {
        Priority {
            strategy: Strategy::Weighted,
            criteria: vec![
                Criterion { metric: Metric::Latency, prefer: Prefer::Lower, weight: 5.0, enabled: true },
                Criterion { metric: Metric::Ratio, prefer: Prefer::Higher, weight: 3.0, enabled: true },
                Criterion { metric: Metric::Failures, prefer: Prefer::Lower, weight: 2.0, enabled: true },
                Criterion { metric: Metric::Successes, prefer: Prefer::Higher, weight: 1.0, enabled: true },
                Criterion { metric: Metric::Connections, prefer: Prefer::Higher, weight: 0.0, enabled: false },
            ],
        }
    }
}

fn raw(metric: Metric, s: &ProfileStats) -> Option<f64> {
    match metric {
        Metric::Latency => s.has_latency().then_some(s.avg_ping_ms),
        Metric::Successes => Some(s.successes as f64),
        Metric::Failures => Some(s.failures as f64),
        Metric::Connections => Some(s.connections as f64),
        Metric::Ratio => Some(s.ratio()),
    }
}

/// Returns the keys ordered best first. Ties fall back to the key itself so output is deterministic.
pub fn rank<K: Clone + Ord>(p: &Priority, items: &[(K, ProfileStats)]) -> Vec<K> {
    let active: Vec<&Criterion> = p.criteria.iter().filter(|c| c.enabled).collect();
    let mut idx: Vec<usize> = (0..items.len()).collect();

    match p.strategy {
        Strategy::Weighted => {
            let scores: Vec<f64> = items.iter().map(|_| 0.0).collect();
            let mut scores = scores;
            let total_weight: f64 = active.iter().map(|c| c.weight.max(0.0)).sum();
            if total_weight > 0.0 {
                for c in &active {
                    let w = c.weight.max(0.0);
                    if w == 0.0 {
                        continue;
                    }
                    let vals: Vec<Option<f64>> = items.iter().map(|(_, s)| raw(c.metric, s)).collect();
                    let known: Vec<f64> = vals.iter().flatten().copied().collect();
                    let (lo, hi) = known
                        .iter()
                        .fold((f64::INFINITY, f64::NEG_INFINITY), |(lo, hi), v| (lo.min(*v), hi.max(*v)));
                    for (i, v) in vals.iter().enumerate() {
                        let norm = match v {
                            None => 0.5,
                            Some(_) if hi - lo < 1e-12 => 0.5,
                            Some(x) => {
                                let t = (x - lo) / (hi - lo);
                                if c.prefer == Prefer::Higher { t } else { 1.0 - t }
                            }
                        };
                        scores[i] += w * norm;
                    }
                }
                for s in scores.iter_mut() {
                    *s /= total_weight;
                }
            }
            idx.sort_by(|&a, &b| {
                scores[b]
                    .partial_cmp(&scores[a])
                    .unwrap_or(Ordering::Equal)
                    .then_with(|| items[a].0.cmp(&items[b].0))
            });
        }
        Strategy::Lexicographic => {
            idx.sort_by(|&a, &b| {
                for c in &active {
                    let (va, vb) = (raw(c.metric, &items[a].1), raw(c.metric, &items[b].1));
                    let ord = match (va, vb) {
                        (None, None) => Ordering::Equal,
                        (None, Some(_)) => Ordering::Greater, // unknown sorts last
                        (Some(_), None) => Ordering::Less,
                        (Some(x), Some(y)) => {
                            let o = x.partial_cmp(&y).unwrap_or(Ordering::Equal);
                            if c.prefer == Prefer::Higher { o.reverse() } else { o }
                        }
                    };
                    if ord != Ordering::Equal {
                        return ord;
                    }
                }
                items[a].0.cmp(&items[b].0)
            });
        }
    }
    idx.into_iter().map(|i| items[i].0.clone()).collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn st(avg: f64, pings: u32, ok: u32, bad: u32, conns: u32) -> ProfileStats {
        ProfileStats { avg_ping_ms: avg, ping_count: pings, successes: ok, failures: bad, connections: conns }
    }

    fn only(metric: Metric, prefer: Prefer, strategy: Strategy) -> Priority {
        Priority { strategy, criteria: vec![Criterion { metric, prefer, weight: 1.0, enabled: true }] }
    }

    #[test]
    fn lowest_latency_wins() {
        let items = vec![("a".to_string(), st(80.0, 10, 0, 0, 0)), ("b".to_string(), st(20.0, 10, 0, 0, 0)), ("c".to_string(), st(50.0, 10, 0, 0, 0))];
        for strat in [Strategy::Weighted, Strategy::Lexicographic] {
            let order = rank(&only(Metric::Latency, Prefer::Lower, strat), &items);
            assert_eq!(order, vec!["b", "c", "a"]);
        }
    }

    #[test]
    fn prefer_higher_flips_the_order() {
        let items = vec![("a".to_string(), st(0.0, 0, 5, 0, 0)), ("b".to_string(), st(0.0, 0, 9, 0, 0))];
        let order = rank(&only(Metric::Successes, Prefer::Higher, Strategy::Weighted), &items);
        assert_eq!(order, vec!["b", "a"]);
        let order = rank(&only(Metric::Successes, Prefer::Lower, Strategy::Weighted), &items);
        assert_eq!(order, vec!["a", "b"]);
    }

    #[test]
    fn unmeasured_latency_is_neutral_when_weighted_and_last_when_lexicographic() {
        let items = vec![
            ("fast".to_string(), st(10.0, 5, 0, 0, 0)),
            ("unknown".to_string(), st(0.0, 0, 0, 0, 0)),
            ("slow".to_string(), st(300.0, 5, 0, 0, 0)),
        ];
        let w = rank(&only(Metric::Latency, Prefer::Lower, Strategy::Weighted), &items);
        assert_eq!(w, vec!["fast", "unknown", "slow"]);
        let l = rank(&only(Metric::Latency, Prefer::Lower, Strategy::Lexicographic), &items);
        assert_eq!(l, vec!["fast", "slow", "unknown"]);
    }

    #[test]
    fn lexicographic_uses_later_criteria_only_to_break_ties() {
        let p = Priority {
            strategy: Strategy::Lexicographic,
            criteria: vec![
                Criterion { metric: Metric::Latency, prefer: Prefer::Lower, weight: 1.0, enabled: true },
                Criterion { metric: Metric::Failures, prefer: Prefer::Lower, weight: 1.0, enabled: true },
            ],
        };
        let items = vec![
            ("a".to_string(), st(30.0, 5, 0, 9, 0)),
            ("b".to_string(), st(30.0, 5, 0, 1, 0)),
            ("c".to_string(), st(10.0, 5, 0, 99, 0)),
        ];
        assert_eq!(rank(&p, &items), vec!["c", "b", "a"]);
    }

    #[test]
    fn weights_trade_criteria_off() {
        // "quick" is fastest but flaky; "steady" is slower but reliable.
        let items = vec![
            ("quick".to_string(), st(10.0, 5, 1, 50, 0)),
            ("steady".to_string(), st(40.0, 5, 50, 1, 0)),
        ];
        let latency_heavy = Priority {
            strategy: Strategy::Weighted,
            criteria: vec![
                Criterion { metric: Metric::Latency, prefer: Prefer::Lower, weight: 10.0, enabled: true },
                Criterion { metric: Metric::Ratio, prefer: Prefer::Higher, weight: 1.0, enabled: true },
            ],
        };
        assert_eq!(rank(&latency_heavy, &items)[0], "quick");
        let reliability_heavy = Priority {
            strategy: Strategy::Weighted,
            criteria: vec![
                Criterion { metric: Metric::Latency, prefer: Prefer::Lower, weight: 1.0, enabled: true },
                Criterion { metric: Metric::Ratio, prefer: Prefer::Higher, weight: 10.0, enabled: true },
            ],
        };
        assert_eq!(rank(&reliability_heavy, &items)[0], "steady");
    }

    #[test]
    fn disabled_criteria_and_empty_lists_fall_back_to_key_order() {
        let items = vec![("b".to_string(), st(5.0, 1, 0, 0, 0)), ("a".to_string(), st(500.0, 1, 0, 0, 0))];
        let mut p = only(Metric::Latency, Prefer::Lower, Strategy::Weighted);
        p.criteria[0].enabled = false;
        assert_eq!(rank(&p, &items), vec!["a", "b"]);
        p.criteria.clear();
        assert_eq!(rank(&p, &items), vec!["a", "b"]);
    }
}
