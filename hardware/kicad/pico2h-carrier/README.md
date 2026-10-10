# Pico 2 H carrier (KiCad draft)

現行の jitsu-in-pico 配線を、Pico 2 H・LCD・OV7675 のソケット接続へ移すための KiCad 下書きです。

## ファイル

- `pico2h_carrier.kicad_sch`: コネクタ単位の回路図。ネットラベルは既存の実配線に合わせています。
- `pico2h_carrier.kicad_pcb`: 58 x 38 mm・四隅1 mm面取りの外形、4つの固定穴、部品の機械領域。電気フットプリントは未配置です。
- `PicoCarrier.kicad_sym`: この設計用のコネクタシンボル。
- `sym-lib-table`: 上記シンボルライブラリのプロジェクト登録。

## コネクタ

| Ref | 対象 | フットプリント |
|---|---|---|
| J1 | Pico 2 H 左列、物理ピン1–20 | 1x20 2.54 mm female socket（B面） |
| J2 | Pico 2 H 右列、USB側から物理ピン40–21 | 1x20 2.54 mm female socket（B面） |
| J3 | M154-240240-RGB | 1x8 2.54 mm female socket |
| J4 | OV7675 / Arducam B0070 | 2x10 2.54 mm female socket（B面） |
| SW1, SW2 | 進む、承認 | 汎用6 mm THTタクトスイッチ |
| J5 | Debug Probe UART RX/GND | 1x2 2.54 mm female socket |

基板上の電気接続は回路図のネットラベルを参照してください。LCDのJ3 pin 1はGND、カメラJ4は奇数ピンが左列です。Pico右列J2 pin 1はUSB側のVBUS位置です。

## 仮レイアウト

前面（F面側）にLCDと右端の操作ボタン2個、キャリア基板の裏面（B面）にPico 2 H、背面ケースにカメラを置く積層構成です。基板の横幅はPicoの51 mm、縦幅はLCDの32 mmとカメラの30.5 mmに合わせて58 x 38 mmにしています。USB端子は左側へ向きます。LCDは前カバー、カメラは背面ケースに固定し、短いハーネスでJ3（F面）・J4（B面）へ接続します。

Rev Cの[完成・内部図](3dprint/assembly_preview.png)、[回転できる3Dビュー](3dprint/assembly.html)、[Bambu Studio用3MF・組立手順](3dprint/README.md)を用意しました。ケースは63.4 × 43.4 × 42.3 mmです。

PCB座標はケースXY座標から各2.7 mmを引いた値です。固定穴はPCB上で(3, 3)、(55, 3)、(3, 35)、(55, 35)、直径2.7 mmです。ケースを閉じるねじで、前カバー・キャリア・背面ケースを共締めします。寸法はケースと同じ寸法ファイルを使います。

`MECH_CASE`と`MECH_COMPONENTS`にそれぞれケース・電子部品の簡略VRMLを登録しています。KiCadの3Dビューで配置を参照し、内部を見たい場合は`MECH_CASE`のモデル表示を無効にしてください。VRMLの原点はPCB左上・表面です。電子部品のモデルは電気フットプリントと独立しているため、部品配置を変更した場合は参照モデルも更新します。

## 機械設計の前提

- Pico 2 H: 51 x 21 mm、ヘッダ間隔17.78 mm、2.54 mmピッチ。[Raspberry Piデータシート](https://datasheets.raspberrypi.com/pico/pico-2-datasheet.pdf)
- M154-240240-RGB: 公称基板外形43.72 x 32 mm。[秋月電子の製品ページ](https://akizukidenshi.com/catalog/g/g131019/)
- Arducam B0070: 公称基板外形30.5 x 30.5 mm、20ピン。[秋月電子の製品ページ](https://akizukidenshi.com/catalog/g/g113201/)
- TVDP01-G73BB: 6 x 6 mm、端子間ピッチ4.5 mm。[秋月電子の製品ページ](https://akizukidenshi.com/catalog/g/g109825)

公称外形と仮の実装高さに基づき、取付柱・押し子・USB開口を設けた調整用試作です。未計測のレンズ高さ、コネクタの向きと奥行き、ソケット高さは[寸法ファイル](3dprint/case_dimensions.json)に仮定を記録しています。実測後に確定してください。

## 次の作業

1. LCDとカメラの実物でヘッダ位置・固定穴・レンズ突出量を測り、基板上の配置を確定する。
2. A4に基板外形を1:1印刷し、部品とUSBケーブルの干渉を確認する。
3. KiCadでコネクタ位置と固定穴を配置し、ネットを同期して配線する。J1/J2/J4はB面配置でピン配列が反転する。
4. [3Dプリント用ケース試作](3dprint/README.md)を実物に合わせて調整し、基板を仮組みする。
5. ERC/DRCとケース寸法を確認してから製造データを出力する。

## 確認

この作業環境には KiCad がないため、KiCad上での読込確認、ERC、DRC、Gerber出力は未実施です。製造データとして発注する前にKiCadで開き、フットプリントを配置して確認してください。
