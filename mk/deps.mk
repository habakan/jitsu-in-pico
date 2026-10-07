RISCV_TC_REL := https://github.com/raspberrypi/pico-sdk-tools/releases/download/v2.3.1-0
ifeq ($(shell uname -s),Darwin)
RISCV_TC_PKG := riscv-toolchain-16-mac.zip
RISCV_TC_SUM := 186538414857e31012aaa41f5f7ecfdee5bbd42fe3f4ec047232ac06d35a24c1
else
RISCV_TC_PKG := riscv-toolchain-16-x86_64-lin.tar.gz
RISCV_TC_SUM := fc36f1b37f99de54a358115443638023de34e15fe199e718e50cca4d212e7e9f
endif

# Pin every dependency to a commit, especially code that handles keys; never track the tip of a branch.
# Review the diff before updating a pin.
SECP_REV   := 46db787112beabdb5e17e0dc35680716f1057e7b
WAMR_REV   := b70d708d46be750bfcf008218b42c7b98c49368a
PICO_REV   := 079c6f39023649b154152db30f1d781e884879bc
QUIRC_REV  := 927d680904dc95fdff4cd9d022eb374b438ff8f2
QRGEN_REV  := 3c6d0b3cefb4e049dc337e82237c9644399716a8
SPLEEN_REV := 57f9219328c9f5873085320fe8bc8f7dd34b8791

# $(1) destination, $(2) URL, $(3) commit
define clone_at
	git clone --filter=blob:none $(2) third_party/$(1)
	cd third_party/$(1) && git checkout --detach $(3)
endef

deps:
	mkdir -p third_party
	$(call clone_at,secp256k1,https://github.com/bitcoin-core/secp256k1.git,$(SECP_REV))
	$(call clone_at,wasm-micro-runtime,https://github.com/bytecodealliance/wasm-micro-runtime.git,$(WAMR_REV))
	$(call clone_at,pico-sdk,https://github.com/raspberrypi/pico-sdk.git,$(PICO_REV))
	$(call clone_at,quirc,https://github.com/dlbeer/quirc.git,$(QUIRC_REV))
	$(call clone_at,QR-Code-generator,https://github.com/nayuki/QR-Code-generator.git,$(QRGEN_REV))
	$(call clone_at,spleen,https://github.com/fcambus/spleen.git,$(SPLEEN_REV))
	cd third_party/pico-sdk && git submodule update --init --depth 1 lib/tinyusb 2>/dev/null || true
	curl -sL -o third_party/$(RISCV_TC_PKG) $(RISCV_TC_REL)/$(RISCV_TC_PKG)
	cd third_party && echo "$(RISCV_TC_SUM)  $(RISCV_TC_PKG)" | (shasum -a 256 -c 2>/dev/null || sha256sum -c)
	mkdir -p third_party/riscv-toolchain && cd third_party && case $(RISCV_TC_PKG) in \
	  *.zip) unzip -q $(RISCV_TC_PKG) -d riscv-toolchain ;; *) tar xzf $(RISCV_TC_PKG) -C riscv-toolchain ;; esac
	rm third_party/$(RISCV_TC_PKG)
	$(MAKE) patch-deps

# Dependencies for CI host checks; excludes hardware and QEMU requirements
deps-host:
	mkdir -p third_party
	$(call clone_at,secp256k1,https://github.com/bitcoin-core/secp256k1.git,$(SECP_REV))
	$(call clone_at,wasm-micro-runtime,https://github.com/bytecodealliance/wasm-micro-runtime.git,$(WAMR_REV))
	$(call clone_at,QR-Code-generator,https://github.com/nayuki/QR-Code-generator.git,$(QRGEN_REV))
	$(call clone_at,spleen,https://github.com/fcambus/spleen.git,$(SPLEEN_REV))
	$(MAKE) patch-deps
.PHONY: deps-host

# Check that each available third_party dependency matches its pinned commit
check-deps:
	@for d in secp256k1:$(SECP_REV) wasm-micro-runtime:$(WAMR_REV) pico-sdk:$(PICO_REV) \
	          quirc:$(QUIRC_REV) QR-Code-generator:$(QRGEN_REV) spleen:$(SPLEEN_REV); do \
	  n=$${d%%:*}; want=$${d#*:}; \
	  if [ ! -d third_party/$$n ]; then echo "$$n: 未取得（飛ばす）"; continue; fi; \
	  got=$$(git -C third_party/$$n rev-parse HEAD 2>/dev/null); \
	  if [ "$$got" != "$$want" ]; then echo "$$n: $$got != $$want"; exit 1; fi; done
	@echo "取得済みの third_party はすべて固定した commit"
.PHONY: check-deps

# WAMR 2.4.3's classic interpreter assumes i64.store is 4-byte aligned; unaligned stores trap on Hazard3.
# Upstream fixed this in PR #5123 (merged 2026-09-30); keep the patch only while using 2.4.3.
patch-deps:
	@cd third_party/wasm-micro-runtime && p=$(CURDIR)/patches/wamr-classic-interp-unaligned-i64-store.patch; \
	  if git apply --reverse --check $$p 2>/dev/null; then echo "wamr: 既に修正済み（パッチ不要）"; \
	  else git apply $$p && echo "wamr: パッチ適用"; fi
.PHONY: deps patch-deps
