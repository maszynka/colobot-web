# Upstream

`src/` is a vendored copy of https://github.com/colobot/colobot

- Commit: `5c0fb16bb8caeba0e6bc27c700bc9119e1bcd03f` (2026-08-12, "Merge pull request #1893 from immibis/fix-1891")
- Copied with `.git`, `data/` (submodule), `lib/googletest`, `lib/json` (empty submodules) excluded.
- Local changes are marked `colobot-web` / `__EMSCRIPTEN__` in the source. Do not re-sync blindly.

Third-party code (`third_party/`):
- `physfs/` — PhysicsFS release-3.2.0 (github.com/icculus/physfs tag tarball)
- `nlohmann/json.hpp` — nlohmann/json v3.11.3 single header
- `glm/` — GLM 1.0.1 (light release, headers only)
- `stubs/` — colobot-web temporary stubs (libintl identity, libsndfile)
