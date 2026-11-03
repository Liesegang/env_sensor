# Environment Sensor BLE protocol v1

Outdoor、Card、および今後の Room 用の共通プロトコルです。
Room は現在ハードウェアのみで、ファームウェアはありません。
既存の Outdoor 受信アプリとの互換性を維持するため、UUID、`OE` / `OF` / `OT` の識別子、
バージョン、フィールド順、型は変更していません。

- Service: `722acd80-e77f-468d-a916-67e599e7cffd`
- Read / Notify / Write characteristic: `722acd81-e77f-468d-a916-67e599e7cffd`
- Read: 最新の `OE` v1 スナップショット、最大 512 バイト
- Notify: 同じフレームを `OF` v1 ヘッダーで分割
- Write: `OT` v1 の UTC 時刻設定。時刻対応は各ボードのコールバックで実装

追加サービスも共通schemaからC / Swiftを生成します。

| サービス | UUID先頭 | 用途 |
| --- | --- | --- |
| [測定履歴 v1](env-history-v1.md) | `722acda0` | 毎秒の記録、ACK、再接続後の続き取得 |
| [時計 v1](env-clock-v1.md) | `722acdb0` | 認証付きUTC同期、boot IDと時計anchorの取得 |
| [Card画像 v1](card-image-v1.md) | `722acd90` | 6色画像のSD保存と表示 |

UUID suffixはすべて `-e77f-468d-a916-67e599e7cffd`。既存OE / OFのUUID・データ形式は維持します。

## 定義と生成

`env-sensor-v1.json` が唯一の編集元です。
`generate.py` がドライバー非依存の共通構造体・エンコーダー、Outdoor の互換アダプター、
受信アプリが使う `outdoor/protocol/outdoor-v1.json` のコピー、
iPhone アプリが使う `card/software/EnvSensor/Protocol/SensorSchema.swift` を生成します。
Outdoor の TypeScript デコーダーはこの JSON を直接読み込みます。
Swift の UUID・センサー ID・バージョン・フィールド順・型・表示単位も共通 JSON から生成し、
センサーごとのバージョンを省略した場合は `1`、表示倍率を省略した場合は `1.0` を使います。
生成済みファイルは直接編集せず、共通 JSON を編集して生成を実行してください。

```sh
python3 shared/protocol/generate.py
python3 shared/protocol/generate.py --check
python3 shared/protocol/generate_history.py --check
```

`--check` は C の共通エンコーダー・Outdoor 互換アダプター、TypeScript 用 JSON、
Swift 定義のすべてを検査し、変更が必要なファイルのリポジトリ相対パスを表示して終了します。
生成コマンドは実行ディレクトリに依存しません。

従来の `python3 outdoor/protocol/generate.py` も同じ生成処理を呼びます。
ワイヤー形式と各センサーの意味は [既存のパケット仕様](../../outdoor/protocol/README.md) を参照してください。
変更時は受信アプリとバージョンの互換性を確認してください。

## センサーと欠測

センサー ID は 1=OPT4001、2=BME690、3=SGP41、4=STCC4/SHT45、5=BMV080、6=AS3935、7=SFA40。
設定されたセンサーはマスクと TLV を含み、読み取り失敗なら負の errno だけを送ります。
各ボードで使わないセンサーはマスクから外します。
Card は予定されている SFA40 を含む `0x4f` で、未接続の SFA40 は `-ENODATA` になります。
シリアル番号・生の測定値・設定値は同じ測定回のドライバースナップショットから送ります。

SGP41 は各サンプルの raw / index 有効フラグをそのまま送ります。
Card と Outdoor は連続1Hz測定のrawとアルゴリズムの指数を送り、
暖機中の指数はvalidをfalseにします。
SFA40 の ready / within_specification フラグも受信側で確認してください。

## 測定周期と時刻

プロトコルは測定周期を固定しません。連番と起動後ミリ秒で受信順序と間隔を確認します。
Outdoor は従来の 1 秒周期で常時接続できます。
Card は1Hzで測定し、1分ごとの同期時に通常5秒間、広告とGATT接続を有効にします。
認証済み通知の送信、履歴取得、画像転送では期限を延長し、処理途中の切断を避けます。
期限になると切断するため、中央機器は次の測定時に再接続と Notify 購読を行う必要があります。
既存の Web 受信アプリは自動再接続を実装しておらず、Card の送信窓への再接続は別途必要です。
パケットと UUID は同一です。
Cardは専用時計サービスの認証済み`OT` WriteでUTCミリ秒を受け取り、SD記録と履歴表示へ使います。
旧環境特性へのOT Writeも維持します。履歴では本体の測定UTCとboot IDを送り、
毎秒のデータを保存済みカーソルから再開できます。スマートフォンの受信時刻は別に保存します。
再起動直後は未同期です。画像は別の[専用BLEサービス](card-image-v1.md)を使用します。

## 検証

既存の全 7 センサーのホスト生成フレームは変更前と同じ 258 バイトです。
`outdoor/firmware/tests/check_telemetry.py` と受信アプリの 20 個のテストで、
フィールド値・符号・64 ビット値・欠測・分割・容量境界を確認しています。
