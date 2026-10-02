# 試作ハードウェアと配線

2026-09-22 時点。秋月で購入した部品（Pico 2 H、Debug Probe、1.54 インチ ST7789 液晶、OV7670 基板、
5 方向スイッチキット、タクトスイッチ 3 個）で組む前提。ピン定義は `platform/rp2350/board_pins.h` と一致させる。
実物のシルク印刷とピン番号は、配線前に必ず見比べる。

## GPIO 割り当て（Pico 2、使える 26 本を全部使う）

| GPIO | 物理ピン | 用途 | 接続先 |
|---|---|---|---|
| GP0 | 1 | UART0 TX | Debug Probe の UART「RX」 |
| GP1 | 2 | ボタン C | タクトスイッチ → GND（UART RX は使わない） |
| GP2〜GP9 | 4〜7, 9〜12 | カメラ D0〜D7 | PIO で 8 本まとめて読むので連番にする |
| GP10 | 14 | カメラ PCLK | |
| GP11 | 15 | カメラ HREF | |
| GP12 | 16 | カメラ VSYNC | |
| GP13 | 17 | ジョイスティック UP（A） | |
| GP14 | 19 | カメラ SIO-D（I2C1 SDA） | |
| GP15 | 20 | カメラ SIO-C（I2C1 SCL） | |
| GP16 | 21 | 液晶 DC | |
| GP17 | 22 | ジョイスティック DOWN（D） | |
| GP18 | 24 | 液晶 SCL（SPI0 SCK） | |
| GP19 | 25 | 液晶 SDA（SPI0 TX） | |
| GP20 | 26 | ジョイスティック LEFT（C） | |
| GP21 | 27 | カメラ XCLK（CLOCK GPOUT0） | OV7670 / OV7675 のみ。水晶付きの OV2640 基板では空き |
| GP22 | 29 | ジョイスティック RIGHT（B） | |
| GP26 | 31 | ジョイスティック SW（押し込み） | |
| GP27 | 32 | ボタン A（キャンセル） | タクトスイッチ → GND |
| GP28 | 34 | ボタン B | タクトスイッチ → GND |

ブレッドボードで組むときの注意:

- **Pico の基板が D〜G 列を覆う**ので、行 1〜20 で使えるのは A・B 列（C 列の左）と I・J 列（H 列の右）だけ。
  3V3 と GND を複数へ配る場合は、Pico から 1 本だけ引いて周辺機器側の行で数珠つなぎにする
- 液晶 M154-240240-RGB は**ピンヘッダが実装済みで届いた**（はんだ付けは不要だった）。基板が本体側へ張り出すので、
  ピン列を外周の列（J 列など）に挿して本体をボードの外へはみ出させると、隣の列が空いて配線できる
- タクトスイッチ TVDP01-G73BB は、溝をまたぐ 2 本が内部でつながっている向きだった（押すと行 n と行 n+2 がつながる）。
  GND を行 n、信号を行 n+2 に取る。反応しないときは `button_test.uf2` でどの GPIO が落ちるかを見る
- 確認画面を送るだけなら「進む（GP17）」「承認（GP26）」の 2 個で足りる

- GP23（電源制御）、GP24（VBUS 検出）、GP25（LED）、GP29（VSYS 監視）はボード内部で使われていて引き出されていない
- 使う周辺機能（UART0、I2C1、SPI0、CLOCK GPOUT0）が上のピンに出せることは、pico-sdk の `io_bank0.h` の FUNCSEL 定義で確認した
- 固定する信号: 液晶 CS → GND、液晶 RES → 3V3、液晶 BLK → 3V3、カメラ RESET → カメラの I/O 電源、カメラ PWDN → GND
- 3V3(OUT)（物理ピン 36）から液晶・ジョイスティック・カメラに給電する。GND は物理ピン 3, 8, 13, 18, 23, 28, 33, 38

ピンが足りなくなったら、まず液晶 CS を GP1 に移してボタン C を外す（CS 固定で SPI の同期が崩れる場合の逃げ道）。

実際に組んでいるブレッドボードの配線は [docs/breadboard.md](breadboard.md)。

## 部品ごとの配線

### 液晶 M154-240240-RGB（ST7789V、8 ピン）

| 液晶 | 接続先 |
|---|---|
| GND | GND |
| VCC | 3V3（2.4〜3.3V。5V 不可） |
| SCL | GP18 |
| SDA | GP19 |
| RES | 3V3（起動時は SWRESET コマンドでリセットする） |
| DC | GP16 |
| CS | GND |
| BLK | 3V3 |

CS を固定しているので、SPI は mode 3（CPOL=1, CPHA=1）で送る（`platform/rp2350/st7789.c`）。
表示の上下左右やオフセットがずれていたら、`MADCTL`（0x36）と窓の設定を直す。

### ジョイスティック AE-SKRHAAE010-BO（8 ピン、要はんだ付け）

秋月の回路図（`AE-SKRHAAE010-BO.pdf`）で確認した。各方向と押し込みは 10kΩ で +V にプルアップ済みで、押すと GND に落ちる。

| キット | 接続先 |
|---|---|
| +V | 3V3 |
| GND（2 本） | GND |
| SW | GP26 |
| A（UP） | GP13 |
| B（RIGHT） | GP22 |
| C（LEFT） | GP20 |
| D（DOWN） | GP17 |

### タクトスイッチ ×3

片側を GPIO（GP27 / GP28 / GP1）、反対側を GND。プルアップは RP2350 の内蔵を使う。

### カメラ

#### OV7670 基板 ST-HL-08-V1（購入済み、24 ピン DIP）

秋月の技術参考資料で確認したピン配置: 1 AVDD、2 AGND、3 DOGND、4 DVDD、5 DOVDD、6 PWDN、7 RESET、8 STROBE、
9 VSYNC、10 PCLK、11 SIO-C、12 SIO-D、13 XCLK、14 HREF、15 VREF1、16 VREF2、17〜24 D0〜D7。

**この基板には電源回路が無い。** センサーの定格は AVDD 2.45〜3.0V、DVDD 1.62〜1.98V、DOVDD 1.7〜3.0V で、
Pico の 3V3 を直接つなぐと AVDD の最大 3.0V を超える。秋月の資料どおりに組むなら追加部品が要る:

| 用途 | 部品（秋月） | 価格 |
|---|---|---|
| DVDD 1.8V | UT7500L-18-T92-B（TO-92、ピンは 1=VOUT / 2=GND / 3=VIN で 78L 系と逆）https://akizukidenshi.com/catalog/g/g110491/ | ¥30 |
| 上の入出力コンデンサ | 10µF 積層セラミック ×2 https://akizukidenshi.com/catalog/g/g108155/ | ¥120 |
| AVDD / DOVDD 3.0V | NJM2884U1-03（SOT-89-5、表面実装）https://akizukidenshi.com/catalog/g/g110896/ | ¥40 |
| 上の変換基板 | AE-SOT89（10 枚）https://akizukidenshi.com/catalog/g/g110835/ | ¥70 |
| 上の出力 / 入力コンデンサ | 2.2µF https://akizukidenshi.com/catalog/g/g108152/ 、1µF https://akizukidenshi.com/catalog/g/g131472/ | ¥60 |
| VREF1 / VREF2 | 0.1µF（10 個）https://akizukidenshi.com/catalog/g/g113582/ | ¥100 |
| SIO-C / SIO-D プルアップ | 4.7kΩ（100 本）https://akizukidenshi.com/catalog/g/g116472/ | ¥100 |

3.0V の LDO は秋月に TO-92 品が無く、表面実装品のはんだ付けが要る。レンズは固定焦点で、仕様書の被写界深度は約 20cm。

#### 代替: OV7675 基板 Arducam B0070（秋月、3.3V 単一電源）

https://akizukidenshi.com/catalog/g/g113201/ （¥1,080）。信号は OV7670 と同じ 8bit パラレル（VSYNC / HREF / PCLK / XCLK / SCL / SDA）で、
上の GPIO 割り当てのまま使える。電源回路の追加が要らないので、OV7670 より先に試すならこちら。

#### 本命: OV2640 基板（日昇テクノロジー、18 ピン、3.3V）

https://www.csun.co.jp/SHOP/2022031501.html （¥1,045、在庫わずか）。12MHz 水晶を載せているので XCLK（GP21）は不要。
M12 マウントのレンズで、回してピントを合わせられる見込み（ページに明記は無い）。
ピン: 1 VCC(3.3V)、2 GND、3 VS、4 SCL、5 HS、6 SDA、7 RESET、8〜15 D0〜D7、16 PCLK、18 PWDN。

### Debug Probe

- 「D」ポート（SWD）→ Pico 2 H のデバッグ端子（JST SH 3 ピン）。付属の SH-SH ケーブルでそのまま挿さる
- 「U」ポート（UART）の RX → GP0、GND → GND。TX は使わない
- Pico 2 H 本体は、別の USB ケーブルで PC から給電する

## 書き込みと UART

Debug Probe の SWD で書くのが速い。BOOTSEL も USB の抜き差しも要らず、書き込み後にリセットまでできる。

```
make deps-openocd                            # 一度だけ（Raspberry Pi のフォークをビルド）
make flash-swd ELF=build/rp2350/app.elf      # 書き込み
make run ELF=build/rp2350/app.elf SECONDS=90 # 書き込み → 受信開始 → リセット
make monitor SECONDS=60                      # 受信だけ
```

上流の OpenOCD（Homebrew の 0.12.0、HEAD とも）は Hazard3 を DAP 経由で扱えず、`target/rp2350.cfg` の
`target create ... riscv -dap` で落ちる。Raspberry Pi のフォークには `target/rp2350-riscv.cfg` がある。

BOOTSEL からの `.uf2` 書き込み（`make flash`）も残してあるが、macOS のリムーバブルボリューム権限で
`cp` が弾かれることがあり、その場合は Finder でドラッグする。

## 初期確認の手順（部品が届いたら）

1. Pico 2 H と Debug Probe だけで `build/rp2350/signer.uf2` を書き、UART（115200bps）に署名の実測時間が出ることを確かめる。`psbt_bench.uf2` で PSBT 一巡の時間も測れる（済、`docs/architecture-b.md` 11 節）
2. **はんだ付け前**: タクトスイッチ 3 個をブレッドボードに挿し（GP17 = 進む、GP26 = 承認、GP27 = 却下、それぞれ GND へ）、`build/rp2350/app_nolcd.uf2` を書く。液晶の代わりに確認画面の文字が UART に出るので、ボタン操作・画面遷移・承認して署名・UR の出力までを配線なしで確かめられる
3. **済（2026-10-01）**: 液晶とタクトスイッチ 2 個を配線して `build/rp2350/app.elf` を書いた。確認画面 5 枚が表示され、
   全画面を送ってから承認すると署名し（2 入力 129ms）、署名済み PSBT がアニメーション QR で出ることを確認した。
   ジョイスティックは使わず、進む（GP17）と承認（GP26）の 2 個で足りた。ウォレットでの読み取りは未確認
4. カメラを配線して `build/rp2350/camera_test.uf2` を書く。取り込んだ QVGA を LCD に縮小表示し、自前フォークの quirc で QR を読んで、取り込みとデコードの時間、読めた文字列を UART に出す。SCCB で読んだ PID（OV7670 なら 0x76）も出るので、配線の確認に使える

## カメラの取り込み（`platform/rp2350/camera.*`）

- PIO（`camera.pio`）が YUV422（Y U Y V）の Y だけを拾い、DMA が QVGA のグレースケール（76.8KB）を直接バッファに書く。quirc の画像バッファへそのまま取り込むので、フレームバッファを二重に持たない
- 毎回 VSYNC 待ちからやり直すので、フレームの途中から始めても次のフレームの先頭から取れる。行の終わりは HREF が下がるのを待つ
- **カメラ無しで実機検証できる**（`make run ELF=build/rp2350/pio_loopback_test.elf`）。同じ PIO の別ステートマシンに
  DVP の波形（D0〜D7・PCLK・HREF・VSYNC）を出させ、取り込み側に同じピンを読ませる。PIO の入力はパッドを見るので配線は要らない。
  2026-09-25 に実機で PCLK 1.5 / 6.25 / 25MHz、フレーム途中からの起動を確認し、取り込んだ画素が生成した Y と全て一致した
  （25MHz は OV7670 に与える XCLK と同じ速さ。1 フレーム 64x8 の理論値 61µs に対し実測 65µs）
- XCLK は GP21 のクロック出力（150MHz / 6 = 25MHz）。SCCB は I2C1 の 100kHz
- センサーの設定は `camera_ov7670.c`（QVGA YUV の定番設定）だけ。OV7675 / OV2640 の設定は部品が決まってから足す。どちらも実機で要調整
- 実機なしの検証: `make check-camera-sim` が、pioasm の出力を最小の PIO シミュレータで実行する。合成した DVP 波形（VSYNC、HREF、ブランキング、HREF の遅れ、途中からの起動）で、取り込んだ画素が Y と一致する。行末の HREF 待ちを消した版は、HREF が PCLK より遅れる波形で失敗する。電気的なタイミング（データのセットアップ / ホールド）は実機でしか確かめられない

`app.uf2` は BIP39 テストベクタの seed（`abandon ... about`）で署名する。資金を扱ってはならない。
