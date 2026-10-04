# データシート・メーカー図面

Card / Outdoor / Roomの共通資料をここへ集約する。PDFは原本のまま保存する。
元の`hardware/docs/`にあった15件と、BMV080・AS3935・Molex・京セラ・SFA40取扱資料の5件、計20件。
各PDFを開けることとページ数を確認した。

| 型番・資料 | 保存ファイル | ページ数 |
| --- | --- | --- |
| Bosch Sensortec / BMV080 | [Bosch_BMV080_datasheet.pdf](Bosch_BMV080_datasheet.pdf) | 53 |
| Molex / 5035661302 | [Molex_5035661302_drawing.pdf](Molex_5035661302_drawing.pdf) | 4 |
| KYOCERA AVX / 046844713002846+ | [Kyocera_046844713002846_drawing.pdf](Kyocera_046844713002846_drawing.pdf) | 4 |
| ScioSense / AS3935 | [AS3935_datasheet.pdf](AS3935_datasheet.pdf) | 44 |
| Texas Instruments / OPT4001 | [opt4001.pdf](opt4001.pdf) | 56 |
| Bosch Sensortec / BME690 | [BME690_datasheet.pdf](BME690_datasheet.pdf) | 60 |
| Sensirion / STCC4 | [CD_DS_STCC4_D1.pdf](CD_DS_STCC4_D1.pdf) | 18 |
| Sensirion / SHT4x・SHT45 | [HT_DS_Datasheet_SHT4x_5.pdf](HT_DS_Datasheet_SHT4x_5.pdf) | 24 |
| Sensirion / SGP41 | [Sensirion_Gas_Sensors_Datasheet_SGP41.pdf](Sensirion_Gas_Sensors_Datasheet_SGP41.pdf) | 22 |
| Sensirion / SFA40 | [Sensirion_Datasheet_SFA40.pdf](Sensirion_Datasheet_SFA40.pdf) | 19 |
| Sensirion / SFA40取扱・実装 | [Sensirion_Handling_Instructions_SFA40.pdf](Sensirion_Handling_Instructions_SFA40.pdf) | 6 |
| KAGA FEI / ES4L15BA1 | [ES4L15BA1_DataSheet_V1_0_20260624E.pdf](ES4L15BA1_DataSheet_V1_0_20260624E.pdf) | 38 |
| Nordic / nRF54L15・nRF54L10・nRF54L05 | [nRF54L15_nRF54L10_nRF54L05_Datasheet_v1.0.pdf](nRF54L15_nRF54L10_nRF54L05_Datasheet_v1.0.pdf) | 940 |
| Nordic / nPM1300 | [nPM1300_PS_v1.3.pdf](nPM1300_PS_v1.3.pdf) | 175 |
| Same Sky / UJC-HP-3-SMT-TR | [ujc-hp-3-smt-tr.pdf](ujc-hp-3-smt-tr.pdf) | 4 |
| USB Type-C電源接続図 | [ujc-hp-3-smt-tr_powered_by_type-c.pdf](ujc-hp-3-smt-tr_powered_by_type-c.pdf) | 1 |
| Waveshare / 3.6inch e-Paper HAT+ | [3.6inch_e-Paper_HAT+.pdf](3.6inch_e-Paper_HAT+.pdf) | 10 |
| Waveshare / 3.6inch e-Paper HAT+回路図 | [3.6inch_e-Paper_HAT+_Schematic.pdf](3.6inch_e-Paper_HAT+_Schematic.pdf) | 2 |
| スライドスイッチ / SK-22H09G5 | [AKIZUKI-SK-22H09G5.pdf](AKIZUKI-SK-22H09G5.pdf) | 5 |
| トグルスイッチ / TMHU27 | [TMHU27.pdf](TMHU27.pdf) | 2 |

保存時のSHA-256、サイズ、ページ数、由来は[manifest.json](manifest.json)に記録した。
元のリポジトリに取得URLの記録がない資料は、推測したURLを追加していない。
版番号は保存ファイルの内容を参照する。メーカーが後から公開した版への自動更新は行わない。

## 今回の照合に使った資料

- BMV080：Bosch公式[BST-BMV080-DS000](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmv080-ds000.pdf)。保存版はRev 12。
- AS3935：ScioSense公式[AS3935 Datasheet](https://www.sciosense.com/wp-content/uploads/2024/01/AS3935-Datasheet.pdf)。
- Molex：公式[5035661302図面](https://www.molex.com/content/dam/molex/molex-dot-com/products/automated/en-us/salesdrawingpdf/503/503566/5035661302_sd.pdf)。直接取得はタイムアウトしたため、[IC-Componentsに掲載されたメーカー原図](https://www.ic-components.nl/files/de/5035661302.pdf)を保存。図面番号5035661001、Rev A、2017-02-21。
- 京セラ：公式[BJS6844006図面](https://ele.kyocera.com/assets/products/connector/drawing/BJS6844006.pdf)。保存版はRev J、2026-07-01。13極の外形・基板パターンはPDF p.1、適合FPCはp.3。
- SFA40：公式[Handling Instructions](https://sensirion.com/media/documents/57BBCF4B/6A38E93F/Handling-Instructions_SFA40.pdf)。保存版はVersion 1.0、June 2026。上面の保護膜は除去禁止。低温を含むリフロー・熱風はんだは禁止で、他部品のリフロー後に実装する。手はんだはこて先310 °C以下、各端子5秒以内（p.1・4・5）。

BMV080推奨コネクターのうち、Greenconn CFTD104-1302A001C2ADのメーカー図面は未取得。

[BMV080の基板pad照合結果](../../outdoor/hardware/BMV080-pin-review.md)も参照。
Bosch SDKのヘッダー・ライブラリはデータシートと分け、Git対象外の`outdoor/firmware/.local/`に保持する。
