# card_env_sensor

## I²C アドレス

アドレスは R/W ビットを含まない 7-bit 表記である。

回路図のアドレス選択端子の接続を反映した通信先は次のとおり。

| リファレンス | デバイス | I²C アドレス | アドレスの決定条件 | 資料箇所 |
| --- | --- | ---: | --- | ---: |
| IC3 | [BME690](../docs/BME690_datasheet.pdf) | `0x76` | `SDO = GND`。`SDO = VDDIO` では `0x77` | p. 46 |
| IC4 | [SGP41-D-R4](../docs/Sensirion_Gas_Sensors_Datasheet_SGP41.pdf) | `0x59` | 固定 | p. 12 |
| IC5 | [STCC4-D-R3](../docs/CD_DS_STCC4_D1.pdf) | `0x64` | `ADDR = GND`。`ADDR = VDD` では `0x65` | p. 5 |
| U1 | [SHT45](../docs/HT_DS_Datasheet_SHT4x_5.pdf) | `0x44` | 固定。STCC4 の SHT4x 接続用バスに接続 | p. 1 |
| IC6 | [SFA40](../docs/Sensirion_Datasheet_SFA40.pdf) | `0x5D` | 固定 | p. 7 |
| IC7 | [OPT4001YMNR](../docs/opt4001.pdf) | `0x45` | YMN（PicoStar）パッケージで固定 | p. 21 |
| IC2 | [nPM1300-QEXX](../docs/nPM1300_PS_v1.3.pdf) | `0x6B` | 固定 | p. 125 |

> [!NOTE]
> SHT45（U1）はメインの `SCL` / `SDA` バスではなく、STCC4（IC5）の SHT4x 接続用バス `SCL_C` / `SDA_C` に接続されている。
> ホストは SHT45 をメインバスから直接アドレス指定せず、STCC4 内蔵の I²C コントローラーが SHT45 と通信する。

## ほかの I²C 接続

回路図上で I²C ターゲットとして接続されているデバイスは、上表の7個だけである。

IC1 はホスト側のコントローラーであり、I²C ターゲットには数えない。

e-Paper コネクタ（CON2）の `TSCL` と `TSDA` は未接続である。
