.DEFAULT_GOAL := build/signer.wasm

include mk/config.mk
include mk/deps.mk
include mk/wasm.mk
include mk/checks-host.mk
include mk/checks-qemu.mk
include mk/firmware.mk
include mk/device.mk
include mk/docs.mk
