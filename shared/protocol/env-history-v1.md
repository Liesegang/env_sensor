# 環境測定履歴 v1

1秒ごとの測定を、小さいページに分けてスマートフォンへ渡す共通BLEプロトコル。card・outdoor・roomで同じ形式を使用できる。最新値のOE / OF、UTC時刻同期のOT、画像転送サービスは変更しない。

スキーマの正本は [env-history-v1.json](env-history-v1.json)。generate_history.pyがCとSwiftの定数・offsetを生成する。整数はすべて明示的なlittle-endianで、Cの構造体や浮動小数点をそのまま送信しない。

## GATT

| 項目 | UUID | 操作 |
| --- | --- | --- |
| Service | 722acda0-e77f-468d-a916-67e599e7cffd | 履歴 |
| Control | 722acda1-e77f-468d-a916-67e599e7cffd | Write with response |
| Status | 722acda2-e77f-468d-a916-67e599e7cffd | Read / Notify |
| Data | 722acda3-e77f-468d-a916-67e599e7cffd | Read |

既存の静的GATTサービスの後へ追加し、既存のhandleを移動しない。cardでは画像転送と同じ、認証付きLE Secure Connectionsと保存済みbondで保護する。

Controlは20バイトで、ATT MTU 23でも通常のWrite with responseを使用できる。DataとStatusはRead / Read Blobで長い値を読む。CoreBluetoothが組み立てた全体を検証する。Data通知の断片やATT単位の断片をアプリの履歴として保存しない。

## 識別子と順序

履歴の一意キーは(deviceId, bootId, sequence)。bootIdは64bit非ゼロ、sequenceは1から始まる32bit。ページ内では同じbootを使用し、sequenceは厳密に増加する。欠番は許可し、GAPを明示する。

新しいbootはランダムな63bit非ゼロIDを使用し、bit 63を0にする。新CSVのboot_id列に同じIDを保存する。64bit IDは10進数または16進数文字列として保存し、JavaScriptの浮動小数点数へ変換しない。

古いCSVにboot_idがなければ、ファイル名から次の安定したIDを作る。

~~~text
legacyBootId = 0x8000000000000000 | (prefixTag << 32) | fileIndex
prefixTag: LOG = 1, TST = 2
fileIndex: ファイル名の5桁の10進数（00000〜99999）
~~~

同じ端末・ファイルには再起動後も同じIDを割り当てる。CSVに有効なboot_idがあれば、その値を優先する。iOSの既存OE受信セッションのUUIDとは別の識別子領域として保存する（例：history64:<16桁hex>）。

boot IDの大小は時系列を表さない。bootをまたぐ走査順はSDの記録順。カーソルは最後に耐久保存した実在レコードの(bootId, sequence)を指す。

## Control：HC、20バイト

| Offset | 型 | 内容 |
| --- | --- | --- |
| 0 | 2 bytes | HC |
| 2 | u8 | version = 1 |
| 3 | u8 | operation |
| 4 | u32 | request ID、非ゼロ |
| 8 | u64 | cursor boot ID |
| 16 | u32 | cursor sequence |

operationはPULL=1、ACK=2、CANCEL=3。

- PULLは指定カーソルの直後から、最大6件を準備する。初回はカーソル(0, 0)で最古から読む。部分的なゼロカーソルは不正。
- ACKは、そのrequest IDで読み出したページの末尾boot・sequenceを指定する。SQLiteへのレコード保存とカーソル保存を同一transactionで耐久commitした後に送る。
- CANCELはカーソル(0, 0)で、そのrequest IDの処理を終える。保存済み履歴を削除しない。

新しいPULLごとに新しいrequest IDを使用する。PULLのWrite応答は受付を意味し、ページの準備完了を意味しない。時刻同期OTの成功を確認してからPULLを開始する。

同じrequest ID・カーソルのPULLを再送しても、準備済みページを変更しない。ACKは準備済みページのrequest IDと末尾カーソルに一致し、Dataを先頭から終端まで読み出している場合だけ受け付ける。同じ成功済みACKの再送は成功として扱う。異なる末尾を持つACKや、別のPULLの遅い応答は受け付けない。

ACKでSDを削除しない。切断でページを破棄しても、スマートフォンの耐久カーソルからPULLし直せる。スマートフォンでcommit後、ACK前に切断した場合も、次の接続では保存済みカーソルから続ける。重複ページは一意キーでまとめ、耐久カーソルを古いページへ差し戻さない。

## Data：HPページ

ヘッダ32バイトと68バイトのレコードを1〜6件含む。最大440バイトで、512バイトの属性上限内に収まる。

| Offset | 型 | 内容 |
| --- | --- | --- |
| 0 | 2 bytes | HP |
| 2 | u8 | version = 1 |
| 3 | u8 | flags |
| 4 | u32 | request ID |
| 8 | u64 | このページのboot ID |
| 16 | u32 | 最初のレコードのsequence |
| 20 | u16 | レコード数、1〜6 |
| 22 | u16 | レコードサイズ = 68 |
| 24 | u32 | IEEE CRC32 |
| 28 | u32 | ページ準備時のclock revision |
| 32 | records | 同一bootの測定 |

CRCはHP全体を対象に、offset 24〜27をゼロとして計算する。多項式はIEEE CRC32（zlibのcrc32と同じ）。ヘッダ・レコード数・実際の長さ・最初のsequence・全recordの順序・CRCを検証してから保存する。

flagsはMORE=1、GAP=2。MOREはこの走査に続く実在レコードがあることを表す。GAPはカーソルが保持範囲から失われた、またはページにsequenceの欠番があることを表す。未知のflag bitは不正。

ページはACK、CANCEL、切断まで固定する。準備後に新しい測定や時刻同期が来ても、DataのRead Blobの途中でbytesを変更しない。offset 0からのData読出しは同じsnapshotを使用する。

### 68バイトのレコード

| Offset | 型 | フィールド |
| --- | --- | --- |
| 0 | u32 | sequence |
| 4 | u32 | uptime_s |
| 8 | u64 | unix_s、未同期 = 0 |
| 16 | i32 | temperature_mc |
| 20 | u32 | humidity_mpercent |
| 24 | i32 | hcho_millippb |
| 28 | i32 | bme_temperature_mc |
| 32 | u32 | bme_humidity_mpercent |
| 36 | u32 | pressure_pa |
| 40 | u32 | gas_resistance_ohm |
| 44 | u32 | millilux |
| 48 | i16 | co2_ppm |
| 50 | u16 | raw_voc |
| 52 | u16 | raw_nox |
| 54 | u16 | voc_index |
| 56 | u16 | nox_index |
| 58 | u16 | stcc_status |
| 60 | u16 | stcc_self_test |
| 62 | u16 | valid |
| 64 | u8 | rht_source |
| 65 | u8 | gas_valid、0 / 1 |
| 66 | u8 | heater_stable、0 / 1 |
| 67 | u8 | reserved = 0 |

validのbit 0〜12は、温度、湿度、気圧、CO₂、照度、BME温度、BME湿度、BMEガス抵抗、VOC raw、NOx raw、VOC Index、NOx Index、HCHOの順。bit 13〜15は0。値がゼロかどうかで未取得を判断せず、必ずvalidを参照する。

validが0の各測定フィールドは、wire上では必ず0にする。温度・湿度が両方未取得ならrht_sourceも0にする。rht_sourceはnone=0、SHT4x=1、BME69x=2。stccの診断値とgas_valid / heater_stableは、CSVに保存した値をそのまま保持する。reservedは元メモリーに何が入っていても0にする。これにより、未取得値が残ったRAMと空欄から復元したCSVを同じキーで再送しても、測定payloadは一致する。

## Status：HS、72バイト

| Offset | 型 | 内容 |
| --- | --- | --- |
| 0 | 2 bytes | HS |
| 2 | u8 | version = 1 |
| 3 | u8 | state |
| 4 | u32 | request ID |
| 8 | u64 | 保持する最古のboot ID |
| 16 | u32 | 最古sequence |
| 20 | u64 | 保持する最新のboot ID |
| 28 | u32 | 最新sequence |
| 32 | u32 | flags、MORE=1 / GAP=2 |
| 36 | u16 | 準備済みページのレコード数 |
| 38 | u16 | 準備済みページのbyte数 |
| 40 | u32 | 準備済みページの末尾sequence |
| 44 | i32 | error、成功時0、失敗時negative errno |
| 48 | u64 | 現在のboot ID |
| 56 | u32 | 現在のclock revision |
| 60 | u64 | UTC anchor、Unix秒 |
| 68 | u32 | 同じanchorのuptime秒 |

stateはIDLE=0、PREPARING=1、READY=2、COMPLETE=3、ERROR=4。request IDはIDLE初期値のみ0を許可する。READY以外ではoffset 36〜43はすべて0。保持する履歴がなければ最古・最新カーソルは両方(0, 0)。ERROR以外のerrorは0、ERRORでは負の値。

Statusもoffset 0でsnapshotを固定し、同じRead Blob列の終端まで変更しない。READYのrequest ID・レコード数・byte数・末尾sequence・flagsをHPと照合する。

### Status Notify：HN、8バイト

~~~text
0..1 HN | 2 version=1 | 3 state | 4..7 request ID
~~~

Notifyは状態変更のヒントだけを送る。72バイトのStatusをNotifyで送らない。HNを受けたらStatusをReadし、現在の要求と対応付ける。通知が失われても、初回接続・PULL / ACKの応答後にStatusをReadして進められる。バックグラウンドの連続ポーリングタイマーを同期の前提にしない。

## UTCと時刻の訂正

時刻は端末の測定時刻で、スマートフォンの受信時刻は別に保存する。unix_s=0は未同期を表す。既知のUTCは2020年1月1日〜2099年12月31日のUnix秒を使用する。

clock revision・UTC anchor・uptime anchorは、未同期ならすべて0。同期済みならrevisionは非ゼロで、同じboot内で新しいOT同期ごとに増加させる。秒単位のanchorを使用するため、同期の秒境界の丸め誤差は1秒未満。

~~~text
recordUnixS = utcAnchorS + recordUptimeS - uptimeAnchorS
~~~

スマートフォンは、最新のStatusのcurrent bootに一致する保存済みレコードへ、この基準を適用できる。初めて時計を同期したときは同bootの未知時刻を埋め、時計を訂正したときは同bootの測定時刻だけを修正する。遅れて届いた古いrevisionは適用しない。過去の別bootで未知だった時刻を推定で埋めない。

HPのclock revisionとUTCは準備時のsnapshot。新しいHS anchorによる同bootの時刻補正を許可するが、温度などの測定payloadは同じキーで変更しない。同じキーでUTCだけが変わる再送は、重複測定として追加せず時刻の更新として扱う。

## SD・RAMと切断からの再開

cardは1秒1件をRAMへ追加し、10分ごとに600件をSDへまとめて書く。履歴ページはSDの保存済み記録と現在bootの未書込RAMから取得する。ACK待ちのページ専用snapshot以外に履歴全体をRAMへ複製しない。SDに移ったRAMの同じキーを二重に送らない。

1分ごとの通信では、スマートフォンの耐久カーソルからPULL→HP保存→ACKを繰り返す。接続できなかった間の保存済み記録も同じ経路で続けて取得する。画像転送と履歴の帯域を調整し、履歴PULLを一時停止しても測定・SD保存を止めない。

指定カーソルが見つからない場合は、最古の実在ページをGAP付きで返す。スマートフォンは欠落を明示し、最古から再走査して耐久データと重複をまとめる。実在しない測定を補間したり、ランダムboot IDの大小で開始位置を選ばない。履歴が全くなければCOMPLETE+GAPとなり、次の通信まで待つ。電源断で失われた未書込RAMの記録を作り直さない。

## 共通codecと検証

- C：shared/firmware/history/include/env_history.h / env_history_codec.c
- 生成されたoffset：env_history_schema.h / GeneratedHistorySchema.swift
- 共通golden：shared/protocol/tests/env-history-v1-golden.json

~~~sh
python3 shared/protocol/generate_history.py --check
python3 shared/firmware/history/tests/run_host_tests.py
~~~

host検証は実際のC codecをASan / UBSan付きで実行し、Pythonのstruct.packとzlib.crc32で全wire bytesを独立検算する。負数・最大u32・legacy high bit boot・未同期UTC・RAMとCSVのcanonical一致・欠番・容量不足・不正なHC・HSとHNを検証する。Swift codecも同じgoldenを読み、相互互換を確認する。
