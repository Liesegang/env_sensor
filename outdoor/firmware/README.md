# Outdoor 基板のファームウェア

`outdoor/hardware/env_sensor_3.fsch` / `.fbrd`用のnRF Connect SDK / Zephyrアプリ。
ディレクトリは`outdoor/firmware`。回路・受信アプリ・プロトコルも`outdoor/`配下に置く。
起動時にブザーを停止し、I2Cを1回スキャンした後、各測定値を1秒ごとに`printk()`する。
ブザー試験は無効。BLEは同じ測定値を1秒ごとにNotifyする。

## 実装型番と接続

回路の`parts/part`の`PARTS`属性とユーザーの指定を優先する。
流用されたパッド、記号、ライブラリの名前から実装型番を決めない。

| 部品 | 型番の根拠 | センサー | 接続 |
| --- | --- | --- | --- |
| IC3 | ユーザーが実装型番を確認 | SFA40 | MCU側I2C `0x5d`、実機検証済み |
| IC4 | 実機device ID=`0x0121` | OPT4001 | MCU側I2C `0x45` |
| IC5 | `PARTS=bme690` | BME690 | MCU側I2C `0x76` |
| IC6 | `PARTS=spg41`（回路の表記） | SGP41 | MCU側I2C `0x59` |
| IC7 | `PARTS=SHT4x-Axx`、ユーザー指定はSHT45 | SHT45 | STCC4の補助I2C |
| IC8 | partsの`value=STCC4-D-R3`、product IDを取得済み | STCC4 | MCU側I2C `0x64` |
| IC9 | partsの`value=BMV080`、ユーザーが型番を確認 | BMV080 | MCU側I2C `0x54`、未接続・配線修正が必要 |
| IC10 | partsの`deviceset=AS3935`を暫定採用、`PARTS`未指定 | AS3935 | MCU側I2C `0x03`は仮設定、未接続 |

TWIM30、SCL=P0.03、SDA=P0.04、100kHzを使う。
R5/R6は5.1kΩの外部プルアップ。PCA9515のENは3.3Vにつながり、
下流のSCL1/SDA1へ接続される。
SHT45はSTCC4の専用SCL_C/SDA_Cにつながるため、STCC4が取得した温湿度を利用する。

スキャンは起動後1秒待ち、7bitアドレス`0x01..0x03`と`0x08..0x77`を
データ長0のWriteとSTOPで調べる。スキャンでは測定コマンドを送らない。
NACKは未検出、タイムアウト等は異常として表示し中断する。
起動時のバス復旧に失敗した場合はSCL/SDAの状態を表示する。

## ドライバーと取得値

すべてのモジュールを`driver/`へ置く。各`include/`が公開API。
通常のAPIは成功時0、失敗時に負のerrnoを返す。初期化失敗はセンサーごとに表示し、
ほかのセンサーの取得を続ける。読み取り失敗時には古い値を表示しない。

| モジュール | 取得・表示する値 |
| --- | --- |
| `opt4001` | 照度lux、mantissa、exponent、counter、device ID |
| `bme690` | 温度°C、湿度%RH、気圧Pa、ガス抵抗Ω、gas valid、heater stable、status、測定index、gas index、heater設定、chip ID、variant ID |
| `sgp41` | VOC Index、NOx Indexと各有効フラグ、raw VOC・NOx、NOx raw有効フラグ、conditioning、補償入力と出所、self-test結果、serial |
| `stcc4` | CO₂ ppm、status、product ID、serial、SHT45のraw温湿度 |
| `sht45` | STCC4経由のraw値を温度°C・湿度%RHへ変換 |
| `bmv080` | PM1.0・PM2.5・PM10の質量濃度と粒子数濃度、遮蔽・範囲外フラグ、経過時間。SDK v11.2.0でビルド確認済み、通常構成は無効 |
| `as3935` | 雷・過大ノイズ・妨害信号のイベント、エネルギー、推定距離・頭上・範囲外、検出設定、RC校正状態。現在は無効 |
| `sfa40` | HCHO ppb、温度°C、湿度%RH、raw値、暖機状態、product ID、serial。通常構成で有効 |
| `buzzer` | `buzzer_init()`、`buzzer_start()`、`buzzer_stop()`によるPWM制御 |
| `common` | SensirionのCRC8・I2Cコマンド、SGP41補償値、OPT4001のデコード |

各周期は`SAMPLE <連番> at <経過ms> ms`から始まる。
変換待ち時間を含めて開始時刻を1秒間隔に保ち、周期を超えた場合は
期限を過ぎた分をスキップして`SAMPLE OVERRUN`を表示する。
温度・湿度は内部ではm°C・0.001%RHとして保持し、整数で小数表示する。

SGP41は最初の10秒間をconditioningとし、その間は`raw_nox=0 nox_valid=0`。
その後VOCとNOxの両方を取得する。補償には同じ周期のSHT45温湿度を優先し、
取得失敗時はBME690、両方失敗時は25°C・50%RHを使う。
Sensirion公式Gas Index Algorithm v3.2.0でVOC Index・NOx Index（1〜500）へ変換する。
最初の約45秒はアルゴリズムの暖機中として有効フラグをfalseにし、Webでは`—`を表示する。
NOxはconditioning後から暖機を始める。欠測時は履歴をリセットする。
raw値は詳細表示・CSV用に保持する。[SGP41の変換と検証](driver/sgp41/README.md)を参照。

STCC4の測定周期は内部クロックの許容差により1000ms ±150ms。
データ未準備のNACK時はコマンドを再送せず、150ms後に1回だけ再読する。
再読でも未準備なら`-EAGAIN`として欠測を通知し、以前の値は再利用しない。

BME690はBosch公式SensorAPI v1.1.0を`driver/bme690/vendor/`に同梱し、
校正データから温湿度・気圧・ガス抵抗を計算する。forced mode、heater 300°C / 100ms。
BSECによるIAQ等は未実装。OPT4001はDTS品の係数437.5µlux/countで変換する。

## 後で接続するセンサー

SFA40は接続を確認し初期設定で有効。BMV080・AS3935は初期設定で無効。無効なモジュールの初期化・読み取りは呼ばない。
接続後、`prj.conf`で必要なものだけ有効にし、ビルド・書き込みする。

```ini
CONFIG_APP_AS3935=y
CONFIG_APP_SFA40=y
```

AS3935のアドレスは`0x03`を仮設定している。実際のモジュールのA0/A1に合わせ、
Devicetreeの`reg`を`0x01`・`0x02`・`0x03`のいずれかへ変更する。
現在の回路にはIRQ接続がないため1秒ごとにポーリングし、周期内の複数イベントは
個別に取得できない。新しい雷かどうかは`lightning`で判定する。
[AS3935の設定とAPI](driver/as3935/README.md)を参照。

SFA40は電源投入後の最初の1分に`ready=0`、最初の10分に`within_spec=0`を表示する。
HCHO濃度と合わせて暖機状態を確認する。[SFA40の設定とAPI](driver/sfa40/README.md)を参照。

ユーザー提供のBMV080 SDK v11.2.0を`.local/`へ導入し、
Cortex-M33のsoft-floatライブラリでARMビルドとリンクを確認した。
実機のPM測定は未検証。基板IC9のpin/pad対応は反転しているため、
[配線の照合結果](../hardware/BMV080-pin-review.md)を確認し、接続を修正してから使用する。
コードだけを検証するコマンド（書き込みはしない）：

```sh
./scripts/fw test-bmv080 ~/Downloads/bmv080-sdk-v11-2-0.zip
```
[BMV080の導入手順](driver/bmv080/README.md)を参照。

## 環境と検証

macOSのnRF Connect SDK **v3.4.1**と対応ツールチェーンを使用する。
SDKの標準パスは`/opt/nordic/ncs/v3.4.1`。別の場所では`NCS_DIR`で指定する。
`nrfutil`の`sdk-manager` / `device`とSEGGER J-Linkが必要。

以下は`outdoor/firmware`で実行する。

```sh
nrfutil install sdk-manager
nrfutil install device
./scripts/fw setup
./scripts/fw doctor
./scripts/fw build
./scripts/fw test
```

`test`はビルド後に回路図・PCB・Devicetreeの接続、PARTS指定、PWM極性、
I2Cポート、クロック、DC/DC、RTT、ARM成果物、BLE設定と追加モジュールの有効・無効を11件で検証する。
さらに実際のCコードをホストでコンパイルし、CRC正常値・全1bit破損、
照度・温湿度の単位と境界、SGP41補償を9件で確認する。
追加したAS3935・SFA40の実際のCソースを模擬I2Cで実行し、初期化・CRC・状態・
単位変換・距離コード・IRQ解除・校正・未接続時のエラーを11件で確認する。
STCC4の正常読出し・NACK再読・再読上限・コマンド失敗・CRC破損・タイムアウトを6件で確認する。合計37件。
さらにBLEのCエンコーダーの容量境界・分割を検証し、Webの互換テスト用データを生成する。
通常構成のBMV080は無効側だけがビルド対象。
Gas Indexのconditioning・暖機・基準値・信号変化・欠測・無効入力を実際の公式Cコードで追加検証する。
BMV080のSDK有効側は`test-bmv080`で別途ビルド・11件の構成検証を行う。
成果物は`build/zephyr/zephyr.{elf,hex,bin}`。
センサーAPIには変換待ち時間があるため、割り込みハンドラーから呼ばない。

## 書き込みとリアルタイムログ

CON1は1=nRESET、2=3.3V/VTref、3=SWDIO、4=GND、5=SWCLK。
基板を給電してJ-Linkを接続する。確認したシリアル番号は`69730357`。

```sh
./scripts/fw flash 69730357
```

テスト成功後、既存アプリ領域を`.local/before_flash_*.hex`へ保存し、書き込み・検証・リセットする。
バックアップ失敗時は書き込みを中止する。recoverや全消去は自動実行しない。

デバイスの`printk()` / `printf()`はSWD経由のSEGGER RTT channel 0へ出力する。
ターミナル1で受信を開始する：

```sh
cd /Users/yuki/src/github.com/Liesegang/env_sensor/outdoor/firmware
./scripts/fw rtt 69730357
```

ターミナル2でリアルタイムに表示する：

```sh
tail -F /Users/yuki/src/github.com/Liesegang/env_sensor/outdoor/firmware/.local/rtt.log
```

`rtt`はELFからRTTのアドレスを取得する。既存の`rtt.log`は
`rtt_previous_<日時>_<pid>.log`に退避する。終了は任意のキーまたはCtrl+C。
電源を入れ直した場合はLoggerも起動し直す。RTTは8KiBのノンブロッキング設定で、
受信していない間はログが欠落することがある。

## BLEとWeb受信

`CONFIG_APP_BLE=y`で`Outdoor Sensor`として接続可能な広告を出す。
Service UUIDは`722acd80-e77f-468d-a916-67e599e7cffd`。
Read / Notifyの測定characteristicは`722acd81-e77f-468d-a916-67e599e7cffd`の1つ。
1秒の取得が完了するたびに、全ドライバーの値・状態・errnoを1パケットにまとめて通知する。
MTUが小さい場合は同じcharacteristicで分割する。
送信は専用スレッドで実行し、I2C取得周期を維持する。切断後は広告を再開する。

BLEのため内部RCの校正とPSA乱数源を有効にする。LFXOは無効、ブザーは停止したまま。
HFXOの内部負荷容量15pFはNordic DKの設定を参照した実機動作確認済みの設定であり、
KAGA FEIのモジュール仕様から得た推奨値ではない。
受信画面は[outdoor/software](../software/README.md)、
バイナリの定義は[protocol/outdoor-v1.json](../protocol/outdoor-v1.json)と
[プロトコル仕様](../protocol/README.md)を参照。

## ブザーとMCU

MCUはKAGA FEI ES4L15BA1 / nRF54L15、対象は`env_sensor_outdoor/nrf54l15/cpuapp`。
PWM20 channel 0 → P1.01 → R1 180Ω → TR1 2SC3265 → BUZZ1 UGCT7525。
Highで駆動し、停止時はLow。通常は初期化と停止だけを行う。
P1.01と兼用のLFXOを無効にし、内部RCとGRTCのsystem LFCLK（校正する内部RC）を使う。
モジュール仕様に合わせてL1を使うDC/DCモードを指定する。UARTコンソールは無効。

設定は`prj.conf`。`CONFIG_APP_BUZZER_TEST=n`で無音を維持する。
テストを有効にすると2秒待ち、2kHz・duty 50%で1秒間隔に3回、各50ms鳴らす。
`CONFIG_BUZZER_DUTY_PERMILLE`は500が50%、100が10%、10が1%。

## 実機の状況（2026-10-04）

- SDK v3.4.1 / Zephyr v4.4.2 / GCC 14.3.0で、移動後の通常構成のARMビルド・37件のテスト、Gas IndexとCエンコーダーの検証に成功。
- BMV080 SDK・AS3935・SFA40を同時に有効にした全7センサー構成もARMビルド・11件の構成検証に成功。RAMは162432 bytes。追加ドライバーは実機未検証。
- nRF54L15 REV2の書き込み・検証・リセットと、ブザー停止を確認。
- I2C `0x45`・`0x59`・`0x64`・`0x76`を検出済み。
- OPT4001の照度、STCC4のCO₂・SHT45温湿度と識別情報を取得済み。
- BMV080接続時は`I2C bus recovery: -140; SCL=1 SDA=0`。電源の入れ直しでも解消せず、BMV080を外すと復旧した。BMV080側の接続は確認が必要。
- BMV080を外した状態で、OPT4001・BME690・STCC4/SHT45・SGP41の38周期すべてを1000ms間隔で取得し、エラー・周期超過なしを確認。
- SGP41は最初の10周期がconditioning、以降はVOC・NOx両方が有効。BME690はgas valid / heater stableを確認。
- BMV080 SDK v11.2.0有効側のARMビルド・リンクと11件の構成検証に成功。PM値は実機未取得。
- `Outdoor Sensor`へスマホのBLEデバッグアプリから接続できたことをユーザーが確認。
- SFA40追加前の通常構成をJ-Link `69730357`へバックアップ後に書き込み・検証・リセット済み。BMV080・AS3935・SFA40は無効、ブザーは停止。
- STCC4の150ms再読処理追加後、126周期（sample 22〜147）を1000ms間隔で記録。周期超過なし。VOCは48回目、NOxは57回目からIndexが有効になった。
- sample 28でOPT4001のチェックサムエラー`-77`が1回あり、不正値を破棄して次周期で復帰。sample 113でSTCC4が再読後も未準備の`-11`を返し、その周期はBME690の温湿度でSGP41を補償。次周期でSTCC4/SHT45へ復帰した。
- MacのCoreBluetoothでService・Read/Notify characteristicを確認。実際のReadと連続15回のNotifyをWebの`src/protocol.ts`でデコードし、連番・1000ms周期・有効なVOC/NOx Index・SHT45補償入力の一致を検証。切断後の広告再開も確認。
- ブラウザーから実機へのWeb Bluetooth接続・画面描画は未検証。追加3センサーとPM値は未検証。

検証ログは`.local/sensor_verified.log`。例：照度65.944lux、BME690温度23.120°C、
湿度54.880%RH、気圧101803Pa、ガス抵抗8100Ω、CO₂ 391ppm、
SHT45温度23.274°C・湿度54.908%RH、SGP41 raw VOC=29429・raw NOx=17492。

今回の実機ログは`.local/sensor_verified_20261004_connected.log`、
検証結果は`.local/connected_sensor_validation.json`と`.local/ble_verified_after_retry_validation.json`。
BLE sample 101ではSHT45温度23.456°C・湿度58.845%RH、CO₂ 413ppm、
照度40.340lux、VOC Index 89・NOx Index 1を受信した。

バックアップ、実機ログ、SDKはGit対象外の`.local/`へ保存する。
データシートは共通の[docs/datasheets](../../docs/datasheets/README.md)へ集約する。

## 参照

- [TI OPT4001](https://www.ti.com/lit/ds/symlink/opt4001.pdf)
- [Sensirion SGP41](https://github.com/Sensirion/embedded-i2c-sgp41)
- [Sensirion STCC4](https://github.com/Sensirion/embedded-i2c-stcc4)
- [Sensirion SHT4x](https://sensirion.com/resource/datasheet/sht4x)
- [Bosch BME690 SensorAPI](https://github.com/boschsensortec/BME690_SensorAPI/tree/8a4d9229aaa26c960ca8f7717cb93087f067ecbe)
- [Bosch BMV080 SDK](https://www.bosch-sensortec.com/en/software-tools/double-opt-in-forms/sdk-v11-2.html)
- [AS3935データシート](https://www.sciosense.com/wp-content/uploads/2024/01/AS3935-Datasheet.pdf)
- [Sensirion SFA40公式ドライバー](https://github.com/Sensirion/embedded-i2c-sfa4x/tree/1d7da43d9bd88ad5d9636d18807f52dbb6b86b4a)
- [SEGGER RTT Logger](https://kb.segger.com/J-Link_RTT_Logger)
- [MCU仕様](../../docs/datasheets/ES4L15BA1_DataSheet_V1_0_20260624E.pdf)

## SFA40追加後の実機確認（2026-10-04）

`CONFIG_APP_SFA40=y`で通常構成をビルド・全チェック後、J-Link `69730357`へ
既存アプリをバックアップして書き込み・検証・リセットした。ブザーは停止。
I2Cは`0x45 / 0x59 / 0x5d / 0x64 / 0x76`の5デバイスを検出。
SFA40のproduct ID `0x09020283`、serial `4f926e9f20dc`を確認し、
暖機の最初の1分後に`ready=1`となりHCHO 2.5ppbを取得した。
10分の安定化完了は今回の確認範囲外。
MacのCoreBluetoothでReadと15回のNotify（sequence 49〜63、1秒間隔）を受信し、
Webアプリの`src/protocol.ts`でデコードを検証した。有効マスクは`0x4f`。
検証ログは`.local/sfa40_flash.log`、`.local/sfa40_verified_rtt.log`、
`.local/ble_sfa40.jsonl`、`.local/ble_sfa40_validation.json`。

## SD CSVとブラウザー時刻同期

通常構成で`CONFIG_APP_SD_LOG=y`。毎秒の取得結果をRAMへ保存し、別スレッドが
約1分ごとにSDカードの`OUTDOOR/00000001.CSV`等へまとめて書き込み・同期する。
既存のFATカードを使用し、自動フォーマットは無効。全センサーの値と状態を残す。
時刻は同じBLE characteristicへのWriteでブラウザーから受け取り、
system LFCLK（校正する内部RC）を使うGRTCの稼働時間で維持する。
電源断・リセット後は再同期する。未同期の行は日時を空欄にして稼働時間を残す。
[形式・設定・エラー時の動作](logging/README.md)を参照。

2026-10-04に基板へFAT32カードを挿し、60件単位の保存成功を確認した。
PCに挿し替えた実カードのCSV（240行・95列）を読み取り、日時・状態・単位を検証済み。
14サンプル・1148フィールドを実BLEデータと照合し、すべて一致した。
SD未挿入中のバッファ超過による173サンプルの欠測も検出した。
詳細と検証コマンドは[SD記録の実機確認](logging/README.md)を参照。
