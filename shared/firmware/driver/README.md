# 共通ファームウェアライブラリ

`outdoor/firmware` と `card/firmware` が同じソースをビルドする。
各アプリは必要なディレクトリだけをCMakeの`add_subdirectory`で取り込む。
各`include/`を公開APIとし、成功時0、失敗時に負のerrnoを返す。

| モジュール | 使用基板 | 用途 |
| --- | --- | --- |
| `common` | outdoor / card | Sensirion CRC、I²Cコマンド、共通変換 |
| `opt4001` | outdoor / card | 照度 |
| `bme690` | outdoor / card | BME690、Bosch BME690 SensorAPI |
| `sgp41` | outdoor / card | VOC / NOx、Gas Index Algorithm |
| `stcc4` | outdoor / card | CO₂と補助SHT4xのraw値 |
| `sht45` | outdoor / card | SHT4x共通の温湿度換算式 |
| `sfa40` | outdoor / card | アルデヒド、実装済みの場合に自動検出 |
| `epd3in6e` | card | Waveshare 3.6inch (E) Spectra 6、SPI・初期化・画面更新 |
| `epd_dashboard` | card | 600×400横向きの数値カード・24時間グラフ描画 |

v2 Cardの`PARTS`属性はBME690とSGP41を指定する。回路シンボルの
BME688とSGP40ではなく、この実装指定に合わせて共有ドライバーを使用する。
SHT4xはSTCC4の専用I²Cに接続するため、ホストから直接操作しない。

電子ペーパーの通信と画面描画は別のライブラリに分けている。
描画結果は400×600のネイティブ形式、4bit/pixel・120,000バイト。
`card/epaper_sample`を参考に移植しており、同ディレクトリへのビルド依存はない。
