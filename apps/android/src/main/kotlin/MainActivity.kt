// An Android app that signs a PSBT with the same two WASM modules the bare-metal device runs.
//
// The point of this sample is not that it is a wallet. It is that the modules in this repository can
// be driven from an ordinary Android app with no native build: Chicory is a plain JAR, so there is no
// JNI, no NDK, and no .so per ABI. The .wasm files ship as assets and their hashes are pinned, so an
// asset swapped for another is refused rather than run.
//
// It asks for no permissions. A signer has no reason to reach the network, and requesting nothing
// states that more clearly than any README.
package dev.habakan.wasmsigner.sample

import android.app.Activity
import android.os.Bundle
import android.widget.Button
import android.widget.TextView
import com.dylibso.chicory.runtime.Instance
import com.dylibso.chicory.wasm.Parser as WasmParser
import java.security.MessageDigest
import wasmsigner.Owner
import wasmsigner.Signer

private const val PLAN_SIZE = 5016

class MainActivity : Activity() {
    private lateinit var signer: Signer
    private lateinit var plan: ByteArray
    private var prevTxs: List<ByteArray?> = emptyList()

    private fun asset(name: String) = assets.open(name).use { it.readBytes() }
    private fun sha256(b: ByteArray) =
        MessageDigest.getInstance("SHA-256").digest(b).joinToString("") { "%02x".format(it) }

    /** The plan comes from parser.wasm, which has no keys. This app never looks inside the PSBT
     *  itself; everything it shows came out of a module. */
    private fun planFrom(psbt: ByteArray, fingerprint: Int): Pair<ByteArray, List<ByteArray?>> {
        val inst = Instance.builder(WasmParser.parse(asset("parser.wasm"))).build()
        val mem = inst.memory()
        fun call(n: String, vararg a: Long) = inst.export(n).apply(*a)?.firstOrNull()?.toInt() ?: 0
        mem.write(call("parser_input"), psbt)
        val rc = call("parser_parse", psbt.size.toLong(), fingerprint.toLong())
        require(rc == 0) { "the parser refused this PSBT: $rc" }
        val p = mem.readBytes(call("parser_plan"), PLAN_SIZE)
        val nIn = p[16].toInt() and 0xff
        val inputAt = call("parser_input")
        return p to (0 until nIn).map { i ->
            val len = call("parser_prevtx_len", i.toLong())
            if (len == 0) null
            else mem.readBytes(inputAt + call("parser_prevtx_off", i.toLong()), len)
        }
    }

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        setContentView(resources.getIdentifier("main", "layout", packageName))

        val modules = findViewById<TextView>(id("modules"))
        val load = findViewById<Button>(id("load"))
        val review = findViewById<Button>(id("review"))
        val out = findViewById<TextView>(id("out"))
        val sign = findViewById<Button>(id("sign"))
        val sigs = findViewById<TextView>(id("sigs"))

        val signerWasm = asset("signer.wasm")
        val parserWasm = asset("parser.wasm")
        // Shown so it can be compared by eye against what the device displays and what
        // `make check-repro` rebuilds. The same bytes in three places, or they are not the same bytes
        modules.text = "signer.wasm ${signerWasm.size} B\n  ${sha256(signerWasm)}\n" +
            "parser.wasm ${parserWasm.size} B\n  ${sha256(parserWasm)}"

        load.setOnClickListener {
            // The expected hash lives in the app, so a replaced asset is refused rather than run
            signer = Signer(signerWasm, sha256 = asset("signer.wasm.sha256").decodeToString().trim())
            signer.init(testnet = false)
            // CharArray, not String: a String cannot be cleared. A real app reads this from the
            // person, clears it, and never keeps it
            val mnemonic = ("abandon ".repeat(11) + "about").toCharArray()
            signer.seedFromMnemonic(mnemonic)
            mnemonic.fill('\u0000')
            out.text = "key loaded, fingerprint ${signer.fingerprint}\n" +
                "(BIP39's published all-zero test vector; never put funds through this)"
            review.isEnabled = true
            load.isEnabled = false
        }

        review.setOnClickListener {
            val (p, prev) = planFrom(asset("sample.psbt"), signer.fingerprint.toLong(16).toInt())
            plan = p
            prevTxs = prev
            signer.setPlan(plan).setPrevTxs(prevTxs)
            val r = signer.review()
            val d = signer.display()
            // Every string here was built inside the module from the plan's bytes. None of it is a
            // string the PSBT chose, which is why it is safe to put in front of a person
            out.text = buildString {
                append("fee ${d.fee} sats\nspending ${d.spend} sats\n\n")
                for (o in d.outputs) {
                    val tag = when (o.owner) {
                        Owner.EXTERNAL -> "to"
                        Owner.CHANGE -> "change"
                        Owner.SELF -> "to yourself"
                    }
                    append("$tag ${o.amount} sats\n  ${o.text}\n")
                }
                append("\nwill sign ${r.nSign} input(s)")
            }
            sign.isEnabled = true
            review.isEnabled = false
        }

        sign.setOnClickListener {
            // Only now, after someone has actually read what review() produced
            val result = signer.sign()
            sigs.text = result.joinToString("\n") { s ->
                "input ${s.input}\n  " + s.sig.joinToString("") { "%02x".format(it) }
            } + "\n\nThese are the same bytes the RP2350 produces for this PSBT, and the same bytes" +
                " Bitcoin Core produces, because signing here is deterministic."
            signer.unload()          // the key is gone; this is not deferred to onDestroy
            sign.isEnabled = false
        }
    }

    private fun id(name: String) = resources.getIdentifier(name, "id", packageName)

    override fun onDestroy() {
        super.onDestroy()
        if (::signer.isInitialized) signer.unload()
    }
}
