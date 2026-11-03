# 端末時刻同期 v1

専用BLEサービスでiPhoneから端末へUTCを設定し、端末のboot・clock revision・時計基準を読み取る。スキーマは [env-clock-v1.json](env-clock-v1.json)。generate_history.pyでC・Swiftの定数とoffsetを生成する。

## GATT

| 項目 | UUID | 操作 |
| --- | --- | --- |
| Service | 722acdb0-e77f-468d-a916-67e599e7cffd | 時刻 |
| Clock Write | 722acdb1-e77f-468d-a916-67e599e7cffd | Write with response |
| Clock State | 722acdb2-e77f-468d-a916-67e599e7cffd | Read |

cardでは認証付きLE Secure Connectionsと保存済みbondを使用する。履歴や画像のサービスの後へ追加し、既存GATT handleを移動しない。

新サービスが提供されている場合、Clock Writeを優先する。古いファームウェアで新サービスが提供されていない場合だけ、既存env-sensorサービスのOT Writeへ切り替える。新サービスへのWriteが失敗した場合に、別のサービスへのWriteでエラーを隠さない。新旧のWriteは同じ時計を更新する。

## Write：OT、12バイト

既存のOT v1と同じbodyを使用する。

| Offset | 型 | 内容 |
| --- | --- | --- |
| 0 | 2 bytes | OT |
| 2 | u8 | version = 1 |
| 3 | u8 | reserved = 0 |
| 4 | u64 | UTC Unixミリ秒 |

すべてlittle-endian。UTCの範囲は1577836800000〜4102444799999（2020年1月1日〜2099年12月31日）。Write with responseの成功を確認してから、Clock StateのReadと履歴PULLを開始する。Writeは時計基準の更新で、測定値やSD履歴を削除しない。

## Read：CT、32バイト

| Offset | 型 | 内容 |
| --- | --- | --- |
| 0 | 2 bytes | CT |
| 2 | u8 | version = 1 |
| 3 | u8 | flags、KNOWN = 1 |
| 4 | u64 | 現在のboot ID、非ゼロ |
| 12 | u32 | clock revision |
| 16 | u64 | UTC anchor、Unixミリ秒 |
| 24 | u64 | 同じanchorのuptimeミリ秒 |

未知のflag bitは不正。未同期ならflags、revision、UTC anchor、uptime anchorはすべて0。同期済みならKNOWNが1、revisionは非ゼロ、UTC anchorはWriteと同じ範囲。

32バイトのCTはATT MTU 23でもRead Blobで読める。offset 0のReadでsnapshotを固定し、同じ読出しの終端までboot・revision・anchorsを変更しない。途中で別のOTが来ても、一つのCTに異なるrevisionの値を混ぜない。

現在のcard実装は、履歴HSの秒anchorを1000倍した値をCTへ公開する。同じboot・revisionのHSとCTは同じ時刻基準を示す。wireはミリ秒を保持できるが、現在の時計基準の分解能は1秒。OTのミリ秒精度を、そのまま保持しているとは扱わない。

~~~text
measuredUnixS = (unixAnchorMs + recordUptimeS * 1000 - uptimeAnchorMs) / 1000
~~~

スマートフォンは、bootが一致するレコードの測定時刻へだけこの基準を適用する。受信時刻は別に保存する。初回同期では同bootの未知時刻を埋め、時計訂正では同bootの測定時刻だけを修正する。測定payloadは変更しない。古いrevisionは適用せず、過去の別bootに時計を外挿しない。

## 共通codecと検証

共有C APIはenv_clock.hのenv_clock_encode_state / env_clock_decode_write。実装はenv_history_codec.cへ含まれ、履歴codecと同じCMakeListsから取り込む。

~~~sh
python3 shared/protocol/generate_history.py --check
python3 shared/firmware/history/tests/run_host_tests.py
~~~

env-history-v1-golden.jsonのclock_state、clock_unknown、clock_writeをC・Swiftで共用する。Python struct.packによる独立検算、既知／未知時計、版・reserved・UTC範囲・長さ・容量不足を検証する。
