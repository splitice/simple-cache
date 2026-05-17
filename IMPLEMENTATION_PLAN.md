# Cache Thrash Improvements (Plan)

This repo currently does a full index flush (and a blockfile snapshot) from the write path via `db_lru_gc()`. Under high churn this can devolve into near-continuous flush attempts. The changes implemented in this PR address flush storms and a potential infinite loop in LRU cleanup. The remaining items below are larger design changes.

## 3. Move Expiration Work Off the Write Path

Problem:
`db_lru_gc()` calls `db_expire_cursor()` which can scan thousands of entries per invocation. When `db_lru_gc()` is triggered from inserts, this couples cache maintenance cost directly to write throughput and amplifies latency under load.

Direction:
1. Split `db_lru_gc()` into:
   - a cheap write-path step: eviction only when over limit
   - a periodic maintenance step: expiration cursor work
2. Trigger maintenance periodically (timer-driven or a dedicated background loop) rather than per-insert.

## 4. Add Hysteresis to Eviction Thresholds

Problem:
When `db.db_size_bytes` hovers around `settings.max_size`, the system can oscillate between inserting and evicting, repeatedly triggering maintenance and flushes.

Direction:
Introduce two thresholds:
1. High watermark: start eviction when `db_size_bytes > max_size`.
2. Low watermark: evict down to `max_size * (1 - clear_pct)` (or another tuned value).

Optional enhancement:
Do not start a new eviction pass until the high watermark is exceeded again. This reduces "thrash" oscillations and improves steady-state throughput.

