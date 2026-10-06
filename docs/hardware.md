# Current hardware and wiring

## Parts for the current build

| Part | Model / type | Qty. | Notes |
|---|---|---:|---|
| Microcontroller board | Raspberry Pi Pico 2 H (RP2350) | 1 | Main board |
| Breadboard | EIC-102J | 1 | Pico 2 H, display, switches, and wiring |
| Display | M154-240240-RGB (ST7789V, 1.54 inch) | 1 | Header fitted; no soldering needed |
| Camera | OV7675 board, Arducam B0070 | 1 | 3.3V supply; connected with male-to-female jumper wires |
| Tactile switches | TVDP01-G73BB | 2 | Next and approve; a third switch is spare |
| Debug probe | Raspberry Pi Debug Probe | 1 | SWD flashing and UART logs; includes the SH-SH SWD cable |
| USB cables | Data cables for the Pico and Debug Probe | 2 | One for each board |
| Jumper wires | Male-to-male and male-to-female | As needed | For signal and power connections |

The pin assignment must match `src/board_pins.h`. Always compare the silkscreen on the actual part
against its pin numbers before wiring anything. The OV7670 and OV2640 boards and the joystick below
are alternatives or earlier experiments; they are not part of this build.

## GPIO assignment in the current build

| GPIO | pin | what | goes to |
|---|---|---|---|
| GP0 | 1 | UART0 TX | the Debug Probe's UART "RX" |
| GP1 | 2 | unused | spare |
| GP2-GP9 | 4-7, 9-12 | camera D0-D7 | consecutive, because PIO reads all eight as a run |
| GP10 | 14 | camera PCLK | |
| GP11 | 15 | camera HREF | |
| GP12 | 16 | camera VSYNC | |
| GP13 | 17 | unused | spare |
| GP14 | 19 | camera SIO-D (I2C1 SDA) | |
| GP15 | 20 | camera SIO-C (I2C1 SCL) | |
| GP16 | 21 | panel DC | |
| GP17 | 22 | next button | tactile switch to GND |
| GP18 | 24 | panel SCL (SPI0 SCK) | |
| GP19 | 25 | panel SDA (SPI0 TX) | |
| GP20 | 26 | unused | spare |
| GP21 | 27 | camera XCLK (CLOCK GPOUT0) | OV7670 / OV7675 only; spare on an OV2640 board with a crystal |
| GP22 | 29 | unused | spare |
| GP26 | 31 | approve button | tactile switch to GND |
| GP27 | 32 | unused | spare |
| GP28 | 34 | unused | spare |

Things to watch when building this on a breadboard:

- **The Pico's board covers columns D to G**, so in rows 1 to 20 only columns A and B (left of C) and
  I and J (right of H) are usable. To feed 3V3 and GND to several things, run one lead from the Pico
  and daisy-chain them along the rows on the peripheral side
- The M154-240240-RGB panel **arrived with its header already fitted**, so no soldering was needed.
  Its board overhangs towards the panel, so putting the pin row in an outer column (J, say) and
  letting the panel hang off the edge of the board leaves the next column free to wire
- On the TVDP01-G73BB tactile switches, the two pins that straddle the channel turned out to be the
  connected pair: pressing joins row n to row n+2. Take GND from row n and the signal from row n+2.
  If one does not respond, `button_test.uf2` shows which GPIO actually goes low
- The current build uses two buttons to step through the review screens: "next" (GP17) and "approve" (GP26)

- GP23 (power control), GP24 (VBUS sense), GP25 (LED) and GP29 (VSYS monitor) are used by the board
  itself and not brought out
- That the peripherals in use (UART0, I2C1, SPI0, CLOCK GPOUT0) can reach the pins above was checked
  against the FUNCSEL tables in pico-sdk's `io_bank0.h`
- Signals that are tied off: panel CS to GND, panel RES to 3V3, panel BLK to 3V3, camera RESET to the
  camera's I/O supply, camera PWDN to GND
- 3V3(OUT) (pin 36) feeds the panel and the camera. GND is on pins 3, 8, 13, 18, 23, 28,
  33 and 38

The wiring of the breadboard as actually built is in [docs/breadboard.md](breadboard.md).

## Wiring, part by part

### The M154-240240-RGB panel (ST7789V, 8 pins)

| panel | goes to |
|---|---|
| GND | GND |
| VCC | 3V3 (2.4 to 3.3V; not 5V) |
| SCL | GP18 |
| SDA | GP19 |
| RES | 3V3 (reset at startup is done with the SWRESET command) |
| DC | GP16 |
| CS | GND |
| BLK | 3V3 (on a GPIO this could be dimmed with PWM; for now the QR's white level stands in) |

Because CS is tied off, SPI runs in mode 3 (CPOL=1, CPHA=1) — see `src/drivers/st7789.c`.
If the image is mirrored or offset, the things to fix are `MADCTL` (0x36) and the window setup.

### Optional: the AE-SKRHAAE010-BO joystick (8 pins, needs soldering)

Checked against Akizuki's schematic (`AE-SKRHAAE010-BO.pdf`). Every direction and the press are
already pulled up to +V through 10k and go to GND when pressed.

| kit | goes to |
|---|---|
| +V | 3V3 |
| GND (two of them) | GND |
| SW | GP26 |
| A (UP) | GP13 |
| B (RIGHT) | GP22 |
| C (LEFT) | GP20 |
| D (DOWN) | GP17 |

### Tactile switches (two used)

The current build uses two TVDP01-G73BB switches: next on GP17 and approve on GP26. One side goes to
the GPIO and the other to GND; the RP2350's internal pull-ups are enabled.

### The camera

#### Alternative: the OV7670 board, ST-HL-08-V1 (24-pin DIP)

Pin assignment per Akizuki's reference document: 1 AVDD, 2 AGND, 3 DOGND, 4 DVDD, 5 DOVDD, 6 PWDN,
7 RESET, 8 STROBE, 9 VSYNC, 10 PCLK, 11 SIO-C, 12 SIO-D, 13 XCLK, 14 HREF, 15 VREF1, 16 VREF2,
17-24 D0-D7.

**This board has no power supply on it.** The sensor wants AVDD 2.45 to 3.0V, DVDD 1.62 to 1.98V and
DOVDD 1.7 to 3.0V, so the Pico's 3V3 straight in exceeds AVDD's 3.0V maximum. Building it the way
Akizuki's document says needs extra parts:

| for | part (Akizuki) | price |
|---|---|---|
| DVDD 1.8V | UT7500L-18-T92-B (TO-92; its pins are 1=VOUT / 2=GND / 3=VIN, the reverse of the 78L series) https://akizukidenshi.com/catalog/g/g110491/ | ¥30 |
| its input and output capacitors | 10µF ceramic, two https://akizukidenshi.com/catalog/g/g108155/ | ¥120 |
| AVDD / DOVDD 3.0V | NJM2884U1-03 (SOT-89-5, surface mount) https://akizukidenshi.com/catalog/g/g110896/ | ¥40 |
| a carrier for it | AE-SOT89 (ten) https://akizukidenshi.com/catalog/g/g110835/ | ¥70 |
| its output and input capacitors | 2.2µF https://akizukidenshi.com/catalog/g/g108152/ and 1µF https://akizukidenshi.com/catalog/g/g131472/ | ¥60 |
| VREF1 / VREF2 | 0.1µF (ten) https://akizukidenshi.com/catalog/g/g113582/ | ¥100 |
| SIO-C / SIO-D pull-ups | 4.7k (a hundred) https://akizukidenshi.com/catalog/g/g116472/ | ¥100 |

Akizuki has no TO-92 part for the 3.0V LDO, so that one means soldering a surface-mount package. The
lens is fixed focus, and the datasheet's depth of field is about 20cm.

#### Current camera: the OV7675 board, Arducam B0070 (single 3.3V supply)

https://akizukidenshi.com/catalog/g/g113201/. The signals are the same 8-bit parallel set as the
OV7670 (VSYNC / HREF / PCLK / XCLK / SCL / SDA) and work with the GPIO assignment above. This is the
camera used in the current build; it needs no added power supply.

#### Alternative: an OV2640 board (Nissho Technology, 18 pins, 3.3V)

https://www.csun.co.jp/SHOP/2022031501.html (¥1,045, low stock). It carries a 12MHz crystal, so XCLK
(GP21) is not needed. The M12 mount lens should focus by turning, though the page does not say so
outright. Pins: 1 VCC (3.3V), 2 GND, 3 VS, 4 SCL, 5 HS, 6 SDA, 7 RESET, 8-15 D0-D7, 16 PCLK, 18 PWDN.

### The Debug Probe

- The "D" port (SWD) to the Pico 2 H's debug connector (JST SH, 3 pins). The supplied SH-SH cable fits
  as is
- The "U" port (UART): RX to GP0, GND to GND. TX is unused
- The Pico 2 H itself is powered from the PC over a separate USB cable

## Flashing and the UART

Writing over the Debug Probe's SWD is the quick way. No BOOTSEL, no unplugging USB, and it can reset
the board afterwards.

```
make deps-openocd                            # once (builds Raspberry Pi's fork)
make flash-swd ELF=build/rp2350/app.elf      # flash
make run ELF=build/rp2350/app.elf SECONDS=90 # flash, start listening, reset
make monitor SECONDS=60                      # listen only
```

Upstream OpenOCD (Homebrew's 0.12.0, and HEAD) cannot reach Hazard3 through the DAP and dies on
`target/rp2350.cfg`'s `target create ... riscv -dap`. Raspberry Pi's fork has `target/rp2350-riscv.cfg`.

Flashing a `.uf2` from BOOTSEL (`make flash`) is still there, but macOS's removable-volume permissions
sometimes refuse the `cp`, in which case drag it in Finder.

## Bring-up, in order, as parts arrived

1. With nothing but the Pico 2 H and the Debug Probe, flash `build/rp2350/signer.uf2` and confirm the
   measured signing times appear on the UART at 115200. `psbt_bench.uf2` times a full PSBT round
   (done; `docs/architecture-b.md` §11)
2. **Before soldering anything**: put three tactile switches in the breadboard (GP17 next, GP26
   approve, GP27 reject, each to GND) and flash `build/rp2350/app_nolcd.uf2`. The review screens' text
   comes out on the UART instead of the panel, so the buttons, the screen flow, approving and signing,
   and the UR output can all be checked with nothing else wired
3. **Done (2026-10-01)**: wired the panel and two tactile switches and flashed `build/rp2350/app.elf`.
   Five review screens appeared; stepping through all of them and approving produced a signature
   (129ms for two inputs) and the signed PSBT came back as an animated QR. The joystick was not
   needed — next (GP17) and approve (GP26) were enough. Reading it with a wallet was still unverified
4. **Done (2026-10-02)**: wired the camera (OV7675 / Arducam B0070). Read PID 0x76 and VER 0x73, and
   captured QVGA successfully: 191ms per frame, about 5.2fps, with quirc decoding in 61ms.
   `camera_test.uf2` shows the captured QVGA scaled down on the LCD, reads QRs with our quirc fork, and
   puts the capture and decode times and whatever was read on the UART. It also prints the PID read
   over SCCB (0x76 for an OV7670), which makes it useful for checking the wiring

## Camera capture (`src/drivers/camera.*`)

- PIO (`camera.pio`) picks out only the Y of YUV422 (Y U Y V) and DMA writes QVGA greyscale (76.8KB)
  straight into the buffer. It captures directly into quirc's image buffer, so there is no second
  framebuffer
- Every capture restarts from waiting on VSYNC, so starting partway through a frame still gets the next
  one from its first row. The end of a row is found by waiting for HREF to fall
- **This can be verified on the hardware with no camera attached**
  (`make run ELF=build/rp2350/pio_loopback_test.elf`): another state machine in the same PIO generates
  the DVP waveform (D0-D7, PCLK, HREF, VSYNC) and the capture side reads those same pins, which needs
  no wiring because PIO inputs read the pads. On 2026-09-25 this ran on the hardware at PCLK 1.5, 6.25
  and 25MHz, including starting mid-frame, and every captured pixel matched the Y that was generated.
  25MHz is the same rate as the XCLK given to an OV7670; a 64x8 frame's theoretical 61µs measured 65µs
- XCLK is the clock output on GP21 (150MHz / 6 = 25MHz). SCCB is I2C1 at 100kHz
- `camera_ov7670.c` provides the current QVGA YUV configuration, which has been verified with the
  OV7675 board. The OV2640 needs a separate configuration and has not been tested
- Verification without hardware: `make check-camera-sim` runs pioasm's output through a minimal PIO
  simulator. Against a synthesised DVP waveform (VSYNC, HREF, blanking, HREF arriving late, starting
  mid-frame) the captured pixels match the Y. A version with the end-of-row HREF wait removed fails on
  the waveform where HREF lags PCLK. Electrical timing (data setup and hold) can only be checked on the
  hardware

`app.uf2` signs with the seed from BIP39's test vector (`abandon ... about`). It must never hold funds.
