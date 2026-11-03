# card_env_sensor

## v2（現行）

`v2/card_env_sensor_1.fsch` / `.fbrd` の部品と配線。実装型番はシンボル名より
部品の`PARTS`属性とユーザーの確認を優先する。

| 部品 | シンボル / `PARTS`の実装指定 | I²Cアドレス | 備考 |
| --- | --- | ---: | --- |
| IC2 | OPT4001DTSR | `0x45` | 照度。ADDRは`+V`接続 |
| IC11 | BME688 / **BME690** | `0x76` | 温湿度・気圧・ガス抵抗 |
| IC6 | SGP40-D-R4 / **SPG41**（SGP41の表記違い） | `0x59` | VOC・NOx |
| IC12 | STCC4-D-R3 | `0x64` | CO₂ |
| U1 | nPM1300-QEAA-R | `0x6B` | 電源管理 |
| IC1 | SFA40 | `0x5D` | 回路にはあるが今回の実機には未実装 |
| IC7 | SHT40-BD1B-R2 / **SHT4x-Axx**（実装SHT45） | 補助バス`0x44` | STCC4専用のSCL_C/SDA_Cへ接続 |

MCU IC5はES4L15BA1 / nRF54L15。メインI²CはP0.03（SCL）とP0.04（SDA）。
P1.00の`ENABLE`をLowにするとFET3がオンになり、センサーとI²Cプルアップの電源`+V`が入る。
P1.01の`BUZZER`はTR1経由でブザーを駆動する。P1.00/P1.01を使用するためLFXOは使えない。
IC2の電源デカップリングC36は0.1 µF、SCL/SDAのプルアップR12/R13は各5.1 kΩ。

SHT45はMCUのメインバスから直接読まない。STCC4の自己診断で補助バス上の
接続を確認し、STCC4の測定値に含まれる温湿度を使う。

## v1（旧KiCad設計）

### I²C アドレス

アドレスは R/W ビットを含まない 7-bit 表記である。

回路図のアドレス選択端子の接続を反映した通信先は次のとおり。

| リファレンス | デバイス | I²C アドレス | アドレスの決定条件 | 資料箇所 |
| --- | --- | ---: | --- | ---: |
| IC3 | [BME690](../../docs/datasheets/BME690_datasheet.pdf) | `0x76` | `SDO = GND`。`SDO = VDDIO` では `0x77` | p. 46 |
| IC4 | [SGP41-D-R4](../../docs/datasheets/Sensirion_Gas_Sensors_Datasheet_SGP41.pdf) | `0x59` | 固定 | p. 12 |
| IC5 | [STCC4-D-R3](../../docs/datasheets/CD_DS_STCC4_D1.pdf) | `0x64` | `ADDR = GND`。`ADDR = VDD` では `0x65` | p. 5 |
| U1 | [SHT45](../../docs/datasheets/HT_DS_Datasheet_SHT4x_5.pdf) | `0x44` | 固定。STCC4 の SHT4x 接続用バスに接続 | p. 1 |
| IC6 | [SFA40](../../docs/datasheets/Sensirion_Datasheet_SFA40.pdf) | `0x5D` | 固定 | p. 7 |
| IC7 | [OPT4001YMNR](../../docs/datasheets/opt4001.pdf) | `0x45` | YMN（PicoStar）パッケージで固定 | p. 21 |
| IC2 | [nPM1300-QEXX](../../docs/datasheets/nPM1300_PS_v1.3.pdf) | `0x6B` | 固定 | p. 125 |

> [!NOTE]
> SHT45（U1）はメインの `SCL` / `SDA` バスではなく、STCC4（IC5）の SHT4x 接続用バス `SCL_C` / `SDA_C` に接続されている。
> ホストは SHT45 をメインバスから直接アドレス指定せず、STCC4 内蔵の I²C コントローラーが SHT45 と通信する。

### ほかの I²C 接続

回路図上で I²C ターゲットとして接続されているデバイスは、上表の7個だけである。

IC1 はホスト側のコントローラーであり、I²C ターゲットには数えない。

e-Paper コネクタ（CON2）の `TSCL` と `TSDA` は未接続である。
