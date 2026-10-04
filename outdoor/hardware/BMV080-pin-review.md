# BMV080 / IC9のpinとpadの照合

2026-10-04に`env_sensor_3.fsch`と`env_sensor_3.fbrd`のXMLを照合した。
型番はユーザーが確認したBMV080を採用する。回路のIC9の`value`もBMV080。
ライブラリや記号名から型番を推定していない。

**現在の対応は反転している。回路・基板の配線はこの調査では変更していない。**

## 向きと番号

IC9のpackageは`CON_5035661302_MOL`。実際のpad座標では、
奇数padがy=-1.12、偶数padがy=+1.31にあり、番号は左から右へ増える。
MolexのBottom ContactコネクターではFPCを奇数pad側から挿入する。
基板のセンサー外形もこの方向（packageのyが負の側）へ延び、
光学窓の位置もBoschのTop Viewを180度回転した向きに一致する。

BoschのTop Viewは、センサー本体が上・接点が下のとき、pin 1が左、pin 13が右。
コネクターへ挿入する向きに180度回すとpin 1が右になり、
基板pad 13へ接触する。したがって、**BMV080 pin = 14 - 基板pad**。
PCB上のIC9の配置角度R90はこの相対的な対応を変えない。

Molexの断面図では、挿入口側に出ている奇数端子の内部接点は奥側にあり、
奥側に出ている偶数端子の内部接点は手前側にある。
外側のはんだ端子の前後位置と、FPCへ接触する位置は入れ替わるが、
各端子の左右位置と電気的な番号は変わらない。

現在のライブラリ`connects`はpin 1→pad 1、pin 2→pad 2という同番号の対応になっている。
回路図とPCBのネットは一致するが、物理的な接点の対応とは一致しない。

## 正しいネット割り当て

PS=HighのI2C、アドレス`0x54`（IAB1=Low、IAB0=Low）を使う場合：

| 基板pad | BMV080 pin | 実際の機能 | 現在のネット | 必要なネット |
| --- | --- | --- | --- | --- |
| 1 | 13 | DNC / 内部試験用 | +3V3 | 未接続 |
| 2 | 12 | IRQ | GND | 未接続（現在はポーリング） |
| 3 | 11 | IAB0 | +3V3 | GND |
| 4 | 10 | VDDD | GND | +3V3 |
| 5 | 9 | VSSD | SDA1 | GND |
| 6 | 8 | VDDIO | SCL1 | +3V3 |
| 7 | 7 | PS | +3V3 | +3V3 |
| 8 | 6 | SCL | +3V3 | SCL1 |
| 9 | 5 | SDA | GND | SDA1 |
| 10 | 4 | IAB1 | +3V3 | GND |
| 11 | 3 | VDDA | GND | +3V3 |
| 12 | 2 | VSSA | 未接続 | GND |
| 13 | 1 | VDDL | 未接続 | +3V3 |

電源padは13・11・6・4、GND padは12・5、SCLは8、SDAは9。
pad 7はI2C選択のHigh、pad 10・3はアドレス選択のLow。
13番センサーpinはメーカー指定で電源・GNDを接続しない。

現在のSDA1はセンサーのVSSDへ接続され、センサーSDAは基板GNDへ接続される。
過去にBMV080を接続するとSDA=Low、外すとバスが復旧した症状と整合する。
これは図面からの照合結果であり、接点の導通は実機では確認していない。

## 推奨コネクターの比較

BoschのPDF p.16はMolex 503566-1302、KYOCERA AVX 046844713002846+、
Greenconn CFTD104-1302A001C2ADを適合コネクターとして挙げている。
同ページの注記は、BMV080とZIFコネクターではpin番号の付け方が異なり得ると明記する。

MolexのPDF p.3と京セラのBJS6844006 PDF p.1を比較すると、両者とも下接点で、
外側のはんだ端子は挿入口側に7本、奥側に6本の配置になっている。
少なくともこの2種類で外側端子の前後配置が異なる、という根拠はない。
適合FPCの記載だけから、基板フットプリントやpin番号の互換性は判断しない。
Greenconnの該当型番のメーカー図面は未取得のため、配置・番号は未確認。
どの資料の取り違えによって現在のCADが作られたかは、この照合からは特定できない。

## 参照資料

- [Bosch BMV080 datasheet](../../docs/datasheets/Bosch_BMV080_datasheet.pdf)、BST-BMV080-DS000-12：p.17 Figure 11（番号の相違）、p.26 Figure 24（Top/Bottom View）、p.27 Table 11（pin機能）、p.28 Figure 26（3.3V接続例）。
- [Molex 5035661302 drawing](../../docs/datasheets/Molex_5035661302_drawing.pdf)、5035661001 PSD 000 Rev A：PDF p.3断面図（Bottom Contactと挿入方向）、p.4推奨PCBパターン。
- [KYOCERA AVX 046844713002846+ drawing](../../docs/datasheets/Kyocera_046844713002846_drawing.pdf)、BJS6844006 Rev J：PDF p.1（13極の外形・基板パターン）、p.3（適合FPC）。
- [Bosch公式配布元](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmv080-ds000.pdf)。
- [Molex公式配布元](https://www.molex.com/content/dam/molex/molex-dot-com/products/automated/en-us/salesdrawingpdf/503/503566/5035661302_sd.pdf)。
