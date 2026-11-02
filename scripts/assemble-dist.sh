#!/usr/bin/env bash
# Assemble dist/ for Workers static assets. Usage: scripts/assemble-dist.sh [build-dir] [data-dir]
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD="${1:-build-web}"; DATA="${2:-}"
rm -rf dist && mkdir -p dist
cp "$BUILD/src/colobot.js" "$BUILD/src/colobot.wasm" dist/
cp web/index.html web/privacy.html web/loader.js web/account.js web/zip.js web/sw.js web/_headers dist/
if [ -n "$DATA" ]; then mkdir -p dist/data && cp -R "$DATA"/. dist/data/; fi
# Data URLs are content-addressed: point the boot preload in index.html at the current boot pack.
if [ -f dist/data/manifest.json ]; then
  node -e '
    const fs = require("fs"), m = JSON.parse(fs.readFileSync("dist/data/manifest.json"));
    const b = m.packs.boot, url = "data/" + (b.gz ? b.gz.url : b.url);
    const html = fs.readFileSync("dist/index.html", "utf8");
    const out = html.replace(/href="[^"]*"([^>]*data-boot)/, `href="${url}"$1`);
    if (out === html && !html.includes(url)) throw new Error("boot preload link not found");
    fs.writeFileSync("dist/index.html", out);
    console.log("boot preload ->", url);'
fi
echo "dist ready:"; ls -la dist
