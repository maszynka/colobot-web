import create from './probe.mjs';
const M = await create();
// main() is a JSPI export; callMain does not await, so await via ready + small delay
await new Promise(r => setTimeout(r, 200));
let more = 1;
while (more) more = await M._probe_frame();   // JS-driven loop, one activation at a time
const ok = M._probe_summary();
console.log('RESULT_FRAMES', ok ? 'PASS' : 'FAIL');
