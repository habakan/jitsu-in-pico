# An Android app that signs with the same modules the device runs

This is a sample, not a wallet. Its purpose is to show that `parser.wasm` and `signer.wasm` can be
driven from an ordinary Android app, and to find out where the ABI is awkward when someone actually
tries. It signs a built-in PSBT with BIP39's published all-zero test seed and shows the signatures.

**Verified on an emulator (API 36, 2026-10-04): the signatures it produces are byte-identical to the
ones the native implementation and the JavaScript host produce for the same PSBT.** Same two modules,
same bytes, three places.

```
input 0  304402206c382f89023969e3844ff83718c42e0744bac9ee815381ed36729e3a1cf98433…
input 1  7d45967478d2613679a34700d86c0d89e533a2fde0497c41333d58a45f2364407…
```

## What it needs

**No native build.** [Chicory](https://github.com/dylibso/chicory) is a WebAssembly runtime written
in pure Java, so there is no JNI, no NDK, and no `.so` per ABI — the whole thing is a JAR dependency
and two `.wasm` files in `assets/`.

**No permissions.** A signer has no reason to reach the network, and the manifest asking for nothing
states that more plainly than any README could.

## Building

```sh
make apk        # with the SDK's own tools
make install    # and push it to whatever adb is talking to
```

No Gradle, and so no `gradle-wrapper.jar` to commit. The same reasoning that keeps it out of the
Kotlin host keeps it out of here: a binary blob is not something this repository should ask you to
trust. `aapt2`, `d8`, `zipalign` and `apksigner` come from the SDK you already have.

## What building this found

Writing a real app, rather than only a library, surfaced things a host library's own tests do not.

**The Kotlin standard library has to be dexed explicitly.** `kotlinc` compiles against it but does
not put it in the output, and `d8` only dexes what it is given, so the app died on its first stdlib
call with `NoClassDefFoundError: kotlin.collections.CollectionsKt`. Nothing about the module or the
host library was wrong; the packaging was. Gradle hides this, which is exactly why doing it by hand
is informative.

**`minSdkVersion` has to be passed to `aapt2 link`.** Without it the manifest claims SDK 0 and the
install is refused outright (`INSTALL_FAILED_DEPRECATED_SDK_VERSION`).

**The ABI itself needed nothing.** `Signer.kt` was written for the JVM host tests and worked here
unchanged — no Android-specific shims, no threading concerns, no per-ABI anything. The one place the
ABI shows through is `seedFromMnemonic` taking a `CharArray`, which is deliberate: a `String` cannot
be cleared, and Android hands you an `Editable` from `EditText` that you should copy out of and clear
rather than call `.toString()` on.

## What it does not do

No camera, so no reading an animated QR; the PSBT is a build-time asset. No UR output. It is single
signature P2WPKH and P2TR only, like everything else here, and it holds a publicly known test seed on
purpose. The limitations of the signing itself are in
[../../docs/limitations.md](../../docs/limitations.md).
