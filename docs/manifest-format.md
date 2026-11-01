# Asset manifest format (contract WP5 writer <-> WP11 reader)

`manifest.json` served next to `colobot.wasm`:
```json
{
  "version": 1,
  "packs": { "boot": { "url": "boot.pak", "size": 123, "sha256": "..." } },
  "files": {
    "textures/interface/button1.png": { "pack": "boot", "offset": 0, "size": 4567, "sha256": "..." },
    "music/music010.ogg": { "pack": null, "url": "music/music010.ogg", "size": 999, "sha256": "..." }
  }
}
```
- Paths are PhysFS paths relative to the data root, `/` separators, case as in colobot-data.
- `pack` = name in `packs` (zip, store-only, so `offset` points at raw file bytes) or `null` for loose files (`url` required).
- Directories are implied by file paths (WP11 synthesizes `enumerate`/`stat`).
- `sha256` is hex; used for SW cache keys. Packs <= 25 MiB (Cloudflare limit), `boot` <= 15 MB.
