// parser.wasm と signer.wasm だけで署名まで通す。ホストが C でやっていることを JS でなぞる例。
// 出てくる署名は実機のものとビット単位で一致する（署名は決定論的なので）。
//   node tools/e2e_wasm.mjs
import { readFileSync } from "fs";
const load = p => new WebAssembly.Instance(new WebAssembly.Module(readFileSync(p)), {}).exports;
const P = load("build/parser.wasm"), S = load("build/signer.wasm");
const MN = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";

// 1. parser.wasm で PSBT を plan_t にする
const psbt = readFileSync("components/parser/build/vectors/own_mixed_nwu.psbt");
new Uint8Array(P.memory.buffer).set(psbt, P.parser_input());
const rc = P.parser_parse(psbt.length, 0x73c5da0a);
if (rc) throw new Error("parse rc=" + rc);

// 2. plan_t をそのまま signer.wasm へ渡す
if (!S.signer_init(0)) throw new Error("init");
const enc = new TextEncoder().encode(MN);
new Uint8Array(S.memory.buffer).set(enc, S.signer_in());
if (!S.signer_seed_from_mnemonic(enc.length, 0)) throw new Error("seed");
console.log("fingerprint", S.signer_fingerprint().toString(16).padStart(8, "0"));

const PLAN = 5016;
new Uint8Array(S.memory.buffer, S.signer_plan(), PLAN)
  .set(new Uint8Array(P.memory.buffer, P.parser_plan(), PLAN));

// 3. non_witness_utxo も移す（parser が入力バッファ内の位置を教えてくれる）
const nIn = new Uint8Array(S.memory.buffer)[S.signer_plan() + 16];
let used = 0;
for (let i = 0; i < nIn; i++) {
  const len = P.parser_prevtx_len(i);
  if (!len) { S.signer_set_prevtx(i, 0, 0); continue; }
  const off = P.parser_input() + P.parser_prevtx_off(i);
  new Uint8Array(S.memory.buffer, S.signer_prevtx() + used, len)
    .set(new Uint8Array(P.memory.buffer, off, len));
  S.signer_set_prevtx(i, used, len);
  used += len;
}

// 4. 検証 → 表示 → 署名
const rv = S.signer_review();
if (rv) throw new Error("review rc=" + rv);
const dv = S.signer_display();
if (dv) throw new Error("display rc=" + dv);
const mem = new DataView(S.memory.buffer), u8 = new Uint8Array(S.memory.buffer);
const d = S.signer_display_out();
const fee = mem.getBigUint64(d, true), spend = mem.getBigUint64(d + 8, true), nOut = u8[d + 16];
const btc = v => (Number(v) / 1e8).toFixed(8);
console.log(`fee ${btc(fee)}  spend ${btc(spend)}  outputs ${nOut}`);
for (let i = 0; i < nOut; i++) {
  const o = d + 24 + i * 184;
  const amt = mem.getBigUint64(o, true), owner = u8[o + 8];
  let t = ""; for (let k = o + 10; u8[k]; k++) t += String.fromCharCode(u8[k]);
  console.log(`  out ${i} ${btc(amt)} ${["外部", "お釣り", "自分"][owner]} ${t}`);
}
const n = S.signer_sign();
console.log("署名", n, "本");
for (let i = 0; i < n; i++) {
  const s = S.signer_sigs() + i * 108, len = u8[s + 34];
  console.log(`  in${u8[s]} ${len} byte ${[...u8.slice(s + 35, s + 35 + len)].map(b => b.toString(16).padStart(2, "0")).join("")}`);
}
