"""camera.pio を実機なしで確かめる最小の PIO シミュレータ。

pioasm が生成したヘッダ（build/rp2350/camera.pio.h）の命令語を 1 サイクル 1 命令で実行し、合成した DVP 波形
（VSYNC パルス、HREF の間に Y U Y V を流す、行間のブランキング）から取り込んだ画素が Y と一致するかを見る。
入力ピンは RP2350 の同期化段と同じく 2 サイクル遅れて見える。電気的なタイミングは実機でしか確かめられない。
使う命令（JMP / WAIT pin / IN pins / MOV / PULL と autopush）だけを実装し、それ以外は例外にする。"""
import random
import re
import sys

hdr = open(sys.argv[1]).read()
prog = [int(x, 16) for x in re.findall(r"^\s+0x([0-9a-f]{4}),", hdr, re.M)]
wrap_target = int(re.search(r"#define dvp_y_wrap_target (\d+)", hdr).group(1))
wrap = int(re.search(r"#define dvp_y_wrap (\d+)", hdr).group(1))
PCLK, HREF, VSYNC = 8, 9, 10


def waveform(width, height, pclk_period, hblank, vsync_lines, seed, href_lag=0):
    """PIO クロックごとの入力ピン（IN ベースからの 11 bit）と、2 フレーム分の Y、1 フレーム目の VSYNC の長さを返す。
    データは PCLK の立ち下がりで変わり、立ち上がりで確定する。HREF は PCLK の縁から href_lag サイクル遅れて変わる"""
    rng = random.Random(seed)
    frames = [[rng.randrange(256) for _ in range(width * height)] for _ in range(2)]
    samples = []

    def clock(n_pclk, data_fn, href, vsync):
        for k in range(n_pclk):
            d = data_fn(k)
            for c in range(pclk_period):
                hi = c >= pclk_period // 2
                samples.append(d | hi << PCLK | href << HREF | vsync << VSYNC)

    vsync_len = vsync_lines * (2 * width + hblank) * pclk_period
    for ys in frames:
        clock(vsync_lines * (2 * width + hblank), lambda k: 0, 0, 1)  # VSYNC パルス
        clock(3 * hblank, lambda k: 0, 0, 0)
        for row in range(height):
            line = ys[row * width:(row + 1) * width]
            uv = [rng.randrange(256) for _ in range(width)]
            clock(2 * width, lambda k, line=line, uv=uv: line[k // 2] if k % 2 == 0 else uv[k // 2], 1, 0)
            clock(hblank, lambda k: rng.randrange(256), 0, 0)  # ブランキング中のデータは無意味
    if href_lag:
        href = [(v >> HREF) & 1 for v in samples]
        samples = [(v & ~(1 << HREF)) | (href[max(t - href_lag, 0)] << HREF) for t, v in enumerate(samples)]
    return samples, frames, vsync_len


def run(samples, width, max_words):
    pc, x, osr, isr, isr_count = 0, 0, width - 1, 0, 0  # OSR は CPU が TX FIFO に入れた 1 行の画素数 - 1
    tx = [width - 1]
    rx = []
    for t in range(2, len(samples)):
        pins = samples[t - 2]  # 同期化段の 2 サイクル遅れ
        ins = prog[pc]
        op, arg = ins >> 13, ins & 0xFF
        if ins & 0x1F00:
            raise ValueError("delay / side-set is not simulated")
        nxt = pc + 1
        if op == 0b000:  # JMP
            cond, addr = arg >> 5, arg & 31
            if cond == 0b000:
                nxt = addr
            elif cond == 0b010:  # x--
                if x != 0:
                    nxt = addr
                x = (x - 1) & 0xFFFFFFFF
            else:
                raise ValueError(f"jmp cond {cond}")
        elif op == 0b001:  # WAIT
            pol, src, idx = arg >> 7, (arg >> 5) & 3, arg & 31
            if src != 0b01:
                raise ValueError("only wait pin")
            if (pins >> idx & 1) != pol:
                continue  # 条件が満たされるまで同じ命令に留まる
        elif op == 0b010:  # IN
            src, bits = arg >> 5, arg & 31
            if src != 0 or bits != 8:
                raise ValueError("only in pins, 8")
            isr = (isr >> 8) | ((pins & 0xFF) << 24)  # 右シフト
            isr_count += 8
            if isr_count >= 32:  # autopush
                rx.append(isr)
                isr, isr_count = 0, 0
                if len(rx) == max_words:
                    return rx
        elif op == 0b100 and arg >> 7:  # PULL
            if not tx:
                continue
            osr = tx.pop(0)
        elif op == 0b101:  # MOV
            dst, src = arg >> 5, arg & 7
            if (arg >> 3) & 3 or dst != 0b001 or src != 0b111:
                raise ValueError("only mov x, osr")
            x = osr
        else:
            raise ValueError(f"opcode {op:03b}")
        pc = wrap_target if pc == wrap else nxt
    return rx


def capture(w, h, period, hblank, vs, start, href_lag=0):
    """start が "vsync" なら 1 フレーム目の VSYNC の最中から、"mid" なら 1 フレーム目の途中から起動する。
    前者は 1 フレーム目、後者は次の VSYNC を待って 2 フレーム目を取り込むのが正しい"""
    samples, frames, vsync_len = waveform(w, h, period, hblank, vs, seed=w * 31 + h + period, href_lag=href_lag)
    offset = vsync_len // 2 if start == "vsync" else vsync_len + len(samples) // 4
    want = frames[0] if start == "vsync" else frames[1]
    got = [(wd >> (8 * i)) & 0xFF for wd in run(samples[offset:], w, w * h // 4) for i in range(4)]
    return got == want


failures = 0
cases = [
    # width, height, PCLK 周期（PIO クロック数）, 行間の PCLK 数, VSYNC の行数, 起動する位置, HREF の遅れ
    (320, 3, 12, 144, 3, "vsync", 0),
    (320, 3, 24, 144, 3, "mid", 0),
    (320, 3, 12, 144, 3, "mid", 3),
    (16, 8, 8, 20, 2, "mid", 0),
    (16, 8, 12, 5, 1, "vsync", 2),
    (16, 8, 12, 5, 1, "mid", 5),
]
for w, h, period, hblank, vs, start, lag in cases:
    ok = capture(w, h, period, hblank, vs, start, lag)
    failures += not ok
    print(f"{w}x{h} PCLK={period} cycles, hblank={hblank}, start in {start}, HREF lag {lag}: {'ok' if ok else 'NG'}")

# PCLK をどこまで速くできるかの目安（同期化段 2 サイクル + 命令の間隔で決まる）
for period in (10, 8, 6, 4):
    ok = capture(16, 4, period, 10, 1, "mid")
    print(f"PCLK period {period} cycles ({150 / period:.1f} MHz at 150 MHz): {'ok' if ok else 'loses data'}")
sys.exit(1 if failures else 0)
