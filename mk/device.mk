# To start the device, hold BOOTSEL while connecting USB, then copy the UF2 to the RP2350 drive.
# The drive may be named RP2350 or NO NAME; identify it by its 134MB FAT16 volume.
UF2 ?= build/rp2350/app.uf2
SECONDS ?= 60
BOOT_VOL = $$(diskutil list | awk '/Windows_FAT_16/ && /134.2 MB/ {print $$NF}' | head -1 | \
  xargs -I{} sh -c 'diskutil info {} | sed -n "s/.*Mount Point: *//p"')
flash: $(UF2)
	@vol="$(BOOT_VOL)"; test -n "$$vol" \
	  || (echo "Boot drive not found. Hold BOOTSEL while connecting USB."; false)
	@vol="$(BOOT_VOL)"; cp $(UF2) "$$vol/" 2>/dev/null \
	  && echo "Copied $(UF2) to $$vol; the drive will disconnect and reboot." \
	  || (echo "Could not write to $$vol. In macOS Privacy & Security > Files and Folders,"; \
	      echo "allow Terminal to access removable volumes, or drag $(UF2) in Finder."; false)

# Read the Debug Probe UART at 115200bps; set a duration with SECONDS=10.
monitor:
	mkdir -p build && uv run -q tools/device/monitor.py $(SECONDS)

# Flash over the Debug Probe's SWD; this does not require BOOTSEL or reconnecting USB.
# Use Raspberry Pi's OpenOCD fork because upstream lacks a RISC-V DAP driver for Hazard3 (make deps-openocd builds it).
ELF ?= $(UF2:.uf2=.elf)
OPENOCD_DIR ?= $(HOME)/work/oss/openocd-rpi
OPENOCD = $(OPENOCD_DIR)/src/openocd -s $(OPENOCD_DIR)/tcl -f interface/cmsis-dap.cfg \
  -c "adapter speed 5000" -f target/rp2350-riscv.cfg
# Upstream OpenOCD cannot create the RISC-V target over DAP; rp2350.cfg's -dap option fails.
deps-openocd:
	mkdir -p $(dir $(OPENOCD_DIR))
	git clone --depth 1 https://github.com/raspberrypi/openocd.git $(OPENOCD_DIR)
	cd $(OPENOCD_DIR) && git submodule update --init --depth 1 jimtcl src/jtag/drivers/libjaylink \
	  && ./bootstrap && ./configure --enable-cmsis-dap --enable-internal-jimtcl --disable-werror && $(MAKE) -j8
.PHONY: deps-openocd

flash-swd: $(ELF)
	$(OPENOCD) -c "program $(ELF) verify reset exit"

# Flash, start receiving, and reset; capture output from the beginning.
run: $(ELF)
	@mkdir -p build
	$(OPENOCD) -c "program $(ELF) verify exit" 2>&1 | tail -3
	@uv run -q tools/device/monitor.py $(SECONDS) & \
	  sleep 2; $(OPENOCD) -c "init; reset run; exit" >/dev/null 2>&1; wait
.PHONY: flash flash-swd monitor run
