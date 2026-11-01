#!/bin/sh
# Build + run the WP0 JSPI x wasm-EH probe (node >= 24 or Chrome 137+).
set -e
cd "$(dirname "$0")"
FLAGS="-O3 -fwasm-exceptions ${EXTRA_FLAGS}"
em++ $FLAGS -c probe.cpp -o probe.o
em++ $FLAGS probe.o -o probe.mjs -sJSPI \
  "-sJSPI_EXPORTS=['main','probe_frame']" \
  "-sEXPORTED_FUNCTIONS=['_main','_probe_frame','_probe_summary']" \
  -sMODULARIZE -sEXPORT_ES6 -sENVIRONMENT=node
node --experimental-wasm-jspi run.mjs
