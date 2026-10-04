# SDカードへのCSV記録と時刻同期

毎秒の全センサー取得結果をRAMへ保存し、約60秒ごとに別スレッドでSDへまとめて
書き込み、`fs_sync()`する。平均値に集約せず、各秒の値を1行ずつ残す。
測定・BLEスレッドはSDのマウントや書き込み完了を待たない。

## カードとファイル

- 既存のFAT16/FAT32カードを使う。exFATは無効。自動フォーマットは無効。
- ボードのSPI接続：SCK P1.04（R3経由）、MOSI P1.06、MISO P1.02、CS P2.04。
- P1.02のNFCパッド保護を解除し、GPIO/SPI入力として使用する。
- `spi20`、最大4MHz。ZephyrのSPI SDHC・SDMMC・FatFsを使用する。
- カードの`OUTDOOR/00000001.CSV`から空いている番号へ新しいファイルを作る。
  起動ごとに新規ファイルを選び、既存CSVを上書きしない。
- ヘッダーと全センサーの列は`outdoor/protocol/outdoor-v1.json`から生成する。
  スキーマを変えたら`python3 outdoor/protocol/generate_csv.py`も実行する。

CSVの先頭は`timestamp_utc,clock_synced,sequence,uptime_ms,enabled_mask,valid_mask`。
日時は`2026-10-04T11:40:00.123Z`のようなUTC ISO 8601。
`[°C]`、`[hPa]`、`[ppb]`等の単位付き列は換算済みの値を記録する。
各センサーのerrno・識別情報・raw値・状態フラグも残す。
SHT45の列はSTCC4のパケットから分離して名前を付ける。
無効なセンサーは空欄、失敗時はerrnoを残して測定列を空欄にする。
暖機中の値はセンサー出力とready/validフラグを一緒に保存する。

## 時刻

WebアプリがBLE接続時、既存の測定characteristicへUTC Unix millisecondsを
Write With Responseで送る。書き込みが成功したら「時刻同期済み（SDログ用）」を表示する。
再接続でも同期し直して、その後のサンプルへ反映する。過去のサンプル時刻は変更しない。
GRTCによる64ビット稼働時間と、受け取った日時との差分で時計を維持する。
GRTCはsystem LFCLKを使い、内部RCの校正を有効にしている。
ブラウザーを切断しても、同じ起動中は時計とSD記録が続く。
クロックの誤差とBLE転送遅延があるため、長時間の絶対精度は保証しない。
電源断・リセットでは時刻同期状態が消え、再同期が必要。

同期前のサンプルは日時を空欄にして`clock_synced=0`と稼働時間を保存する。
同期後は`clock_synced=1`。FatFsの更新日時にもUTCを使う。
同期前のファイル更新日時は2020-01-01の仮値となるため、測定日時はCSVの列を参照する。

## エラーと未保存分

書き込みは4KiB単位でまとめ、各分の完了時に同期する。
SD不在・書き込みエラーは10秒後に再試行する。測定は続ける。
3つの分バッファが埋まると、保存できなかった件数をRTTへ`total dropped`として出力する。
エラー時は最後の書き込み前の位置へ切り詰めを試み、失敗したら部分行の可能性をRTTへ出力する。
再試行時は新しいCSVを選ぶ。保存済みファイルの再利用・自動消去はしない。
電源断では未保存のRAM内データが失われる。FATは電源断時の整合性を保証しない。

## 確認

```sh
outdoor/firmware/scripts/fw test
outdoor/firmware/scripts/fw rtt 69730357
```

ブラウザーでBLE接続すると`CLOCK: synchronized from BLE browser time`を出力する。
SDへの保存が成功すると、約1分ごとに`SD LOG: wrote 60 samples ... (synced)`を出力する。
ホスト検証は`tests/check_sd_log.py`で実際のCコードを実行し、時刻パケット、
2日間の時刻継続（模擬稼働時間）、閏日、全センサーCSV、符号・単位変換、
64ビット識別情報、欠測・未同期、パケット不正・容量不足を確認する。

参照：[Zephyr Disk Access](https://docs.zephyrproject.org/latest/services/storage/disk/access.html)、
[SDHC SPI binding](https://docs.zephyrproject.org/latest/build/dts/api/bindings/sdhc/zephyr%2Csdhc-spi-slot.html)。
実装はインストール済みNCS v3.4.1のZephyr API・Kconfigに合わせた。

## 実機確認（2026-10-04）

通常構成をJ-Link `69730357`へバックアップ後に書き込み済み。
同じRead/Notify characteristicのWrite属性を確認し、Mac CoreBluetoothから
UTC時刻を送信してATT Write Responseを受け取った。
切断後も毎秒の測定が続き、15回のNotifyをWebアプリと同じデコーダーで検証した。
ブラウザーの時刻送信・同期表示・送信失敗時の受信継続はPlaywrightで確認した。
実機へのブラウザー直接接続は未検証。
SD未挿入時の`fs_mount=-5`は、基板へFAT32カードを挿して解消した。
`OUTDOOR/00000001.CSV`への60件単位の書き込みと`fs_sync()`成功を確認した。
そのカードをPCへ移し、実ファイル88147 bytes・240行・95列を読み取って検証済み。
全240行の時刻は同期済みで、保存区間は日本時間20:42:00.236〜20:48:52.236。
同じ起動中の日時と稼働時間の差分は一定で、記録された測定は1秒間隔。
SD未挿入中にバッファが埋まった区間には173サンプルの欠測がある
（sequence 180→354）。STCC4の`-EAGAIN`2回は測定列が空欄となり、
他センサーの測定値は残っている。
保存された14サンプル・1148フィールドは、実際のBLEパケットを
Webアプリの`src/protocol.ts`でデコードした値とすべて一致した。
PCから読めたため、診断用の読み戻し・RTTダンプは通常構成には追加していない。
ログは`.local/sd_verified_rtt.log`、`.local/ble_sd_clock_final.jsonl`、
`.local/ble_sd_clock_validation.json`、`.local/sd_flash_final.log`。

SDファイルの検証コマンド（カードを変更しない）：

```sh
python3 outdoor/firmware/tests/check_sd_card_csv.py /Volumes/BOOT/OUTDOOR/00000001.CSV
```

検証用コピーと結果は`.local/sd_00000001_verified.csv`、
`.local/sd_card_csv_validation.json`、`.local/sd_ble_comparison.json`に保存した。
SHA-256: `b41a931bd97ec9a2dd35fcf86196b10c1baef2bffea67b36e17e5da1311efe2f`。
通常構成への復帰は`.local/sd_normal_restore_flash.log`を参照。
