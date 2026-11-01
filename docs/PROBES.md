# Probes

## WP0: `-fwasm-exceptions` x `-sJSPI` (2026-11-01) — PASS

Source: `probes/jspi_eh/` (`./build.sh`; optional `EXTRA_FLAGS=-sWASM_LEGACY_EXCEPTIONS=0`).
Toolchain: emcc 6.0.13, node 24.15.0 `--experimental-wasm-jspi`.

What it checks:
1. `EM_ASYNC_JS` import awaiting a JS Promise (setTimeout) suspends the wasm stack 9 frames deep.
2. A `std::runtime_error` thrown after resume unwinds through the previously suspended frames and is caught in `main` / in the frame function; RAII destructors run (counted).
3. JS-driven loop `while (await M._probe_frame())` with `probe_frame` in `-sJSPI_EXPORTS` (same pattern as `colobot_frame`): 3 frames, each suspends + throws + catches.

Results (legacy EH default and exnref `-sWASM_LEGACY_EXCEPTIONS=0`):
```
main: caught=1 plain=10 dtors=8   RESULT_MAIN PASS
frames=3 ok=3 dtors=35            RESULT_FRAMES PASS
```

Not yet covered (review §5 item 2): emdawnwebgpu triangle + `wgpuInstanceWaitAny` with
`TimedWaitAny` and a browser (Chrome) run. Owner: WP4 (first task), WP12 for Playwright.
