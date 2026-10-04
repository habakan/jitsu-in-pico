# An APK built with the SDK's own tools: aapt2, d8, apksigner. No Gradle, and so no
# gradle-wrapper.jar to commit — a binary blob is not something this repository should ask you to
# trust, and the same reasoning that keeps it out of the Kotlin host keeps it out of here.
#
#   make apk       build it
#   make install   build it and push it to whatever adb is talking to
ANDROID_HOME ?= $(HOME)/Library/Android/sdk
API          ?= 36
MIN_API      ?= 26
BUILD_TOOLS  ?= 36.1.0
BT      := $(ANDROID_HOME)/build-tools/$(BUILD_TOOLS)
PLATFORM := $(ANDROID_HOME)/platforms/android-$(API)/android.jar
PKG     := dev.habakan.wasmsigner.sample

ROOT    := ../..
CHICORY := 1.4.0
CP      := $(ROOT)/components/signer/hosts/kotlin/lib/runtime-$(CHICORY).jar:$(ROOT)/components/signer/hosts/kotlin/lib/wasm-$(CHICORY).jar
SIGNER_KT := $(ROOT)/components/signer/hosts/kotlin/Signer.kt
# The Kotlin standard library has to be in the dex too, or the app dies on its first stdlib call with
# NoClassDefFoundError. Taken from wherever kotlinc lives, so the version always matches the compiler
# that built the code. Homebrew puts it under libexec; a plain install does not
KOTLIN_HOME ?= $(shell dirname $$(dirname $$(readlink -f $$(command -v kotlinc))))
KOTLIN_STDLIB ?= $(firstword $(wildcard $(KOTLIN_HOME)/libexec/lib/kotlin-stdlib.jar \
                                        $(KOTLIN_HOME)/lib/kotlin-stdlib.jar))

OUT := build
SRC := src/main

apk: $(OUT)/app.apk
.PHONY: apk

# The modules ship as assets, with their hashes beside them. The app refuses an asset whose hash is
# not the one it expects, which is the whole reason for copying the hash in too
$(OUT)/assets/signer.wasm: $(ROOT)/build/signer.wasm
	@mkdir -p $(OUT)/assets
	cp $< $@
	shasum -a 256 $< | cut -d' ' -f1 > $(OUT)/assets/signer.wasm.sha256

$(OUT)/assets/parser.wasm: $(ROOT)/build/parser.wasm
	@mkdir -p $(OUT)/assets
	cp $< $@

$(OUT)/assets/sample.psbt: $(ROOT)/build/psbt/own_mixed_nwu.psbt
	@mkdir -p $(OUT)/assets
	cp $< $@

ASSETS := $(OUT)/assets/signer.wasm $(OUT)/assets/parser.wasm $(OUT)/assets/sample.psbt

$(OUT)/res.apk: $(SRC)/AndroidManifest.xml $(wildcard $(SRC)/res/*/*) $(ASSETS)
	@mkdir -p $(OUT)/flat
	$(BT)/aapt2 compile -o $(OUT)/flat --dir $(SRC)/res
	$(BT)/aapt2 link -o $@ -I $(PLATFORM) \
	  --min-sdk-version $(MIN_API) --target-sdk-version $(API) \
	  --manifest $(SRC)/AndroidManifest.xml \
	  -A $(OUT)/assets \
	  --java $(OUT)/gen \
	  $(OUT)/flat/*.flat

$(OUT)/classes.jar: $(SRC)/kotlin/MainActivity.kt $(SIGNER_KT) $(OUT)/res.apk
	kotlinc $(SRC)/kotlin/MainActivity.kt $(SIGNER_KT) \
	  -cp "$(PLATFORM):$(CP)" -d $@ -nowarn

# d8 wants every class the app references, Chicory included, as a single dex
$(OUT)/classes.dex: $(OUT)/classes.jar
	@test -n "$(KOTLIN_STDLIB)" || { echo "kotlin-stdlib.jar not found under $(KOTLIN_HOME)"; exit 1; }
	$(BT)/d8 --lib $(PLATFORM) --min-api $(MIN_API) --output $(OUT) \
	  $(OUT)/classes.jar $(subst :, ,$(CP)) $(KOTLIN_STDLIB)

$(OUT)/unsigned.apk: $(OUT)/res.apk $(OUT)/classes.dex
	cp $(OUT)/res.apk $@
	cd $(OUT) && zip -q ../$@ classes.dex 2>/dev/null || (cd $(OUT) && jar uf ../$@ classes.dex)

# A debug key, generated here rather than committed. It signs nothing but this sample
$(OUT)/debug.keystore:
	@mkdir -p $(OUT)
	keytool -genkeypair -keystore $@ -alias sample -storepass android -keypass android \
	  -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=wasm signer sample"

$(OUT)/app.apk: $(OUT)/unsigned.apk $(OUT)/debug.keystore
	$(BT)/zipalign -f -p 4 $(OUT)/unsigned.apk $(OUT)/aligned.apk
	$(BT)/apksigner sign --ks $(OUT)/debug.keystore --ks-pass pass:android \
	  --key-pass pass:android --out $@ $(OUT)/aligned.apk
	$(BT)/apksigner verify --print-certs $@ | head -2
	@ls -l $@ | awk '{print "apk: "$$5" bytes"}'

install: $(OUT)/app.apk
	adb install -r $(OUT)/app.apk
	adb shell am start -n $(PKG)/.MainActivity
.PHONY: install

clean:
	rm -rf $(OUT)
.PHONY: clean
