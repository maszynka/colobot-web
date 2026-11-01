#!/usr/bin/env bash
# Assemble dist/ for Workers static assets. Usage: scripts/assemble-dist.sh [build-dir] [data-dir]
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD="${1:-build-web}"; DATA="${2:-}"
rm -rf dist && mkdir -p dist
cp "$BUILD/src/colobot.js" "$BUILD/src/colobot.wasm" dist/
cp web/index.html web/loader.js web/sw.js web/_headers dist/
if [ -n "$DATA" ]; then mkdir -p dist/data && cp -R "$DATA"/. dist/data/; fi
echo "dist ready:"; ls -la dist
