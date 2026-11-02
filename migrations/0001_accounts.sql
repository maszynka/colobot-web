-- Accounts + cloud saves (docs/ACCOUNTS.md). Times are unix seconds.
CREATE TABLE users (
  id TEXT PRIMARY KEY,
  email TEXT UNIQUE COLLATE NOCASE,          -- email+password accounts only (NULL for Google-only)
  password_hash TEXT,                         -- base64 PBKDF2-SHA256
  password_salt TEXT,                         -- base64, 16 random bytes per user
  password_iter INTEGER,
  created_at INTEGER NOT NULL
);

CREATE TABLE sessions (
  token_hash TEXT PRIMARY KEY,                -- hex SHA-256 of the cookie token
  user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  created_at INTEGER NOT NULL,
  expires_at INTEGER NOT NULL
);
CREATE INDEX sessions_user ON sessions(user_id);

CREATE TABLE oauth_identities (
  provider TEXT NOT NULL,                     -- 'google'
  subject TEXT NOT NULL,                      -- Google 'sub'
  user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  email TEXT,
  created_at INTEGER NOT NULL,
  PRIMARY KEY (provider, subject)
);
CREATE INDEX oauth_user ON oauth_identities(user_id);

CREATE TABLE saves (
  user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  version INTEGER NOT NULL,                   -- 1, 2, 3 ... per user
  r2_key TEXT NOT NULL,
  size INTEGER NOT NULL,
  hash TEXT,                                  -- client content hash of /save (not of the zip bytes)
  device TEXT,
  created_at INTEGER NOT NULL,
  PRIMARY KEY (user_id, version)
);

CREATE TABLE rate_limits (
  key TEXT PRIMARY KEY,                       -- 'login-ip:1.2.3.4', 'login-email:a@b', 'register-ip:...'
  count INTEGER NOT NULL,
  window_start INTEGER NOT NULL
);
