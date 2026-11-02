-- Opportunistic pruning (worker/index.js ratePrune) deletes by window_start.
-- Old per-email counters ('login-email:<email>') are replaced by per (IP, email) backoff keys.
CREATE INDEX IF NOT EXISTS rate_limits_start ON rate_limits(window_start);
DELETE FROM rate_limits WHERE key LIKE 'login-email:%';
