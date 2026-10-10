# Pico 2 H carrier (KiCad draft)

現行の jitsu-in-pico 配線を、Pico 2 H・LCD・OV7675 のソケット接続へ移すための KiCad 下書きです。

## ファイル

- `pico2h_carrier.kicad_sch`: コネクタ単位の回路図。ネットラベルは既存の実配線に合わせています。
- `pico2h_carrier.kicad_pcb`: 92 x 82 mm の仮外形と部品の機械領域。回路図のフットプリントは未配置です。
- `PicoCarrier.kicad_sym`: この設計用のコネクタシンボル。
- `sym-lib-table`: 上記シンボルライブラリのプロジェクト登録。

## コネクタ

| Ref | 対象 | フットプリント |
|---|---|---|
| J1 | Pico 2 H 左列、物理ピン1–20 | 1x20 2.54 mm female socket |
| J2 | Pico 2 H 右列、USB側から物理ピン40–21 | 1x20 2.54 mm female socket |
| J3 | M154-240240-RGB | 1x8 2.54 mm female socket |
| J4 | OV7675 / Arducam B0070 | 2x10 2.54 mm female socket |
| SW1, SW2 | 進む、承認 | 汎用6 mm THTタクトスイッチ |
| J5 | Debug Probe UART RX/GND | 1x2 2.54 mm female socket |

基板上の電気接続は回路図のネットラベルを参照してください。LCDのJ3 pin 1はGND、カメラJ4は奇数ピンが左列です。Pico右列J2 pin 1はUSB側のVBUS位置です。

## 仮レイアウト

前面上部にLCDとカメラ、下に操作ボタンを並べ、Pico 2 Hを裏面下部に置く構成です。USB端子は左側へ向ける想定です。Picoのソケット穴と周辺モジュールのコネクタ穴が重ならないよう、Pico用の領域を分けています。

## 機械設計の前提

- Pico 2 H: 51 x 21 mm、ヘッダ間隔17.78 mm、2.54 mmピッチ。[Raspberry Piデータシート](https://datasheets.raspberrypi.com/pico/pico-2-datasheet.pdf)
- M154-240240-RGB: 公称基板外形43.72 x 32 mm。[秋月電子の製品ページ](https://akizukidenshi.com/catalog/g/g131019/)
- Arducam B0070: 公称基板外形30.5 x 30.5 mm、20ピン。[秋月電子の製品ページ](https://akizukidenshi.com/catalog/g/g113201/)
- TVDP01-G73BB: 6 x 6 mm、端子間ピッチ4.5 mm。[秋月電子の製品ページ](https://akizukidenshi.com/catalog/g/g109825)

PCB上の部品位置、LCDとカメラのコネクタ基準点、固定穴、Picoの裏面クリアランスは未確定です。公称外形を図面に置いただけなので、実物のヘッダ位置・穴位置・レンズ突出量を測ってから配線とケース寸法を確定してください。基板外形も初期検討用です。

## 次の作業

1. LCDとカメラの実物でヘッダ位置・固定穴・レンズ突出量を測り、基板上の配置を確定する。
2. A4に基板外形を1:1印刷し、部品とUSBケーブルの干渉を確認する。
3. KiCadでコネクタ位置と固定穴を配置し、ネットを同期して配線する。
4. 基板を仮組みしてから、LCD窓・レンズ穴・左側USB開口を持つケースを設計する。
5. ERC/DRCとケース寸法を確認してから製造データを出力する。

## 確認

この作業環境には KiCad がないため、KiCad上での読込確認、ERC、DRC、Gerber出力は未実施です。製造データとして発注する前にKiCadで開き、フットプリントを配置して確認してください。
