// SPDX-License-Identifier: AGPL-3.0-or-later
// Runs site/starboard.wasm (the browser build) over every example x side x
// profile and prints the same lines as ports/wasm/native_check.c, so CI can
// diff browser vs native C byte for byte.
//   node tools/check_wasm.js site/starboard.wasm examples/cdm/*.cdm
"use strict";
const fs = require("fs");
const stub = () => 0;
const inst = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(process.argv[2])),
  { wasi_snapshot_preview1: { fd_close: stub, fd_seek: stub, fd_write: stub } });
const e = inst.exports;
const mem = () => new Uint8Array(e.memory.buffer);
const dec = new TextDecoder();
for (const file of process.argv.slice(3)) {
  const t = fs.readFileSync(file);
  // Object identities, as native_check.c takes them from the parsed CDM.
  const ids = [...t.toString("latin1").matchAll(/^\s*INTERNATIONAL_DESIGNATOR\s*=\s*(\S+)/gm)]
    .map(m => m[1].toUpperCase().slice(0, 12));
  for (let side = 0; side < 2; side++) {
    for (let p = 0; p < 6; p++) {
      mem().set(t, e.sbw_input());
      mem().set(new TextEncoder().encode(ids[side] + "\0"), e.sbw_me_buf());
      const n = e.sbw_decide(t.length, p, 0n);
      console.log(dec.decode(mem().slice(e.sbw_output(), e.sbw_output() + n)));
    }
  }
}
console.log("selftest " + e.sbw_selftest());
