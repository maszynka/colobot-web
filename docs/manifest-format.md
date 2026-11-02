# Asset manifest format (contract WP5 writer <-> WP11 reader)

`manifest.json` served next to `colobot.wasm`:
```json
{
  "version": 1,
  "packs": { "boot": { "url": "boot.3f2a9c1e0b7d.pak", "size": 123, "sha256": "..." } },
  "files": {
    "textures/interface/button1.png": { "pack": "boot", "offset": 0, "size": 4567, "sha256": "..." },
    "music/music010.ogg": { "pack": null, "url": "music/music010.ab12cd34ef56.ogg", "size": 999, "sha256": "..." }
  }
}
```
- Paths are PhysFS paths relative to the data root, `/` separators, case as in colobot-data.
- `pack` = name in `packs` (zip, store-only, so `offset` points at raw file bytes) or `null` for loose files (`url` required). URLs are content-addressed (`name.<sha256[:12]>.ext`), so `/data/*` is served `immutable`; `assemble-dist.sh` writes the boot pack URL into the `<link data-boot>` preload.
- Directories are implied by file paths (WP11 synthesizes `enumerate`/`stat`).
- `sha256` is hex; used for SW cache keys. Packs <= 25 MiB (Cloudflare limit), `boot` <= 15 MB.
