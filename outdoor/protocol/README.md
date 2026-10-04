# Outdoor BLE protocol v1

Service: `722acd80-e77f-468d-a916-67e599e7cffd`

測定characteristic: `722acd81-e77f-468d-a916-67e599e7cffd`、Read / Notifyの1つ。
機器名は`Outdoor Sensor`。Service UUIDを広告し、名前をscan responseで送る。
取得完了ごとに1秒周期でNotifyする。Readは同じ最新スナップショットを返す。

## 共通スキーマ

`outdoor-v1.json`がUUID・フィールド順・型・表示単位の共通定義。
すべての整数はlittle-endian。フィールドごとに直列化し、C構造体のパディングを含めない。
`u64`のserialはWeb側で正確な16桁hex文字列、稼働時間はbigintとして保持する。
単位付きの整数はraw値を送信し、Web側で`scale`を適用する。

```sh
python3 outdoor/protocol/generate.py
python3 outdoor/protocol/generate.py --check
```

生成対象は`outdoor/firmware/telemetry/include/telemetry.h`と`telemetry_encode.c`。
Webは同じJSONを直接参照する。ワイヤー互換性が変わる場合はversionを上げる。

## 測定パケット（Readの値）

| Offset | 型 | 内容 |
| --- | --- | --- |
| 0 | 2 byte | ASCII `OE` |
| 2 | u8 | version=1 |
| 3 | u8 | header size=24 |
| 4 | u16 | 全長、最大512byte |
| 6 | u16 | 有効設定のセンサーマスク |
| 8 | u16 | 今周期の読み取り成功マスク |
| 10 | u16 | reserved=0 |
| 12 | u32 | 測定連番、起動直後の準備中パケットは0 |
| 16 | u64 | 読み取り開始時の起動後ms |
| 24〜 | TLV | 有効設定のセンサーごとに1ブロック |

マスクのbitは`id-1`。IDは1=OPT4001、2=BME690、3=SGP41、4=STCC4/SHT45、
5=BMV080、6=AS3935、7=SFA40。初期設定ではSFA40を含む0x4f。

TLVは`id:u8`、`block version:u8`、`body length:u16`、`error:i16`、フィールド列。
SGP41のblock versionは2（VOC/NOx Index追加）、それ以外は1。
旧SGP41 version=1はWeb側でファームウェア更新が必要と表示する。
error=0のときだけJSONに定義した全フィールドを送る。負のerrnoのときbodyはerrorの2byteのみ。
無効設定のセンサーはブロックがない。正常な0と、無効・待機・失敗を区別できる。
SGP41のIndex有効フラグ・NOx raw有効フラグ、SFA40の暖機、BMV080の更新状態等は各ドライバーのフラグも確認する。
VOC/NOxは公式Gas Index Algorithmの計算値（1〜500）を使用し、rawは診断用にも送る。
STCC4ブロックの温湿度フィールドは`source=sht45`とし、WebではSTCC4とSHT45を別表示する。
SGP41の補償元は同じパケットのSTCC4/SHT45→BME690→既定値の成功状態から分かる。
未知のセンサーIDのTLVはWeb側で読み飛ばす。
全7種類が成功したパケットは258byte、通常の4種類では140byte。

## Notifyの分割ヘッダー

通知は常に分割ヘッダーを付ける。1回で収まる場合も同じ形式。

| Offset | 型 | 内容 |
| --- | --- | --- |
| 0 | 2 byte | ASCII `OF` |
| 2 | u8 | version=1 |
| 3 | u8 | reserved=0 |
| 4 | u32 | 元パケットと同じ連番 |
| 8 | u16 | 元パケット全長 |
| 10 | u16 | 元パケット内のpayload開始位置 |
| 12〜 | byte列 | 元パケットの一部 |

転送長はATT MTU-3以下。MTU=23ではpayloadは8byte、MTU=247では232byte。
MTU=247の通常構成は1通知、全7種類は2通知となる。
送信専用スレッドが断片を順番に送る。取得側は待たず、送信待ちの最新値を置き換える。
長いReadの各offsetは最初のRead時点の値に固定し、新しいNotifyと混ざらないようにする。

Webは連番・全長・offsetを確認し、3秒以内に連続受信した完全なパケットだけを採用する。
欠落・順序違い・途中切断は破棄し、次のoffset=0から再開する。
BLE層の再送を超えて通知が欠落した場合、過去の測定を再送する機能はない。

## 検証

`outdoor/firmware/tests/check_telemetry.py`は実際のCエンコーダーをホストでコンパイルし、
容量境界・不正引数・エラーパケット・分割サイズを検証する。
同じCの出力をWebのVitestとPlaywrightが使用し、符号・64bit精度・全センサーの復元・分割・欠落・CSVを確認する。

- [Zephyr GATT API](https://docs.zephyrproject.org/latest/services/connectivity/bluetooth/api/gatt.html)
- [Chrome Web Bluetooth](https://developer.chrome.com/docs/capabilities/bluetooth)

## 時計の同期（Write）

同じcharacteristicはRead / Notifyに加えてWrite With Responseにも対応する。
測定パケットの形式は変えず、Writeだけ別の制御メッセージを受け取る。
ブラウザーはBLE接続時に`Date.now()`を送る。

| Offset | サイズ | 値 |
|---|---:|---|
| 0 | 2 | ASCII `OT` |
| 2 | 1 | バージョン `1` |
| 3 | 1 | 予約 `0` |
| 4 | 8 | UTC Unix milliseconds、unsigned little endian |

全長12 bytes。2020-01-01以上2100-01-01未満を受理する。
ATT Write Responseの成功が同期完了を示す。分割Write・不正な長さ・予約値は拒否する。
GRTCの稼働時間へ日時の基準を対応付け、その起動中は接続がなくても時刻が進む。
再接続では再同期できる。リセット・電源断後は再同期が必要。
CSVの同期前行は時刻を空欄、`clock_synced=0`にして記録する。
