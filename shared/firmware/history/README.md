# 共通履歴codec

card・outdoor・roomで使用する環境履歴のwire codec。GATT登録・SD読出し・カーソル状態・ACK処理・時計同期は各アプリが担当する。仕様は [env-history-v1.md](../../protocol/env-history-v1.md)、定数・offsetの正本はJSON。

CMakeLists.txtをadd_subdirectoryで取り込み、env_history.hをincludeする。依存は標準Cライブラリだけで、codecはZephyrのthreadやBluetooth APIを呼ばない。

| API | 内容 |
| --- | --- |
| env_history_encode_record | 68B。valid無効値、無効RHT source、reservedを0へ正規化 |
| env_history_encode_page | 最大440B。全入力を先に検証し、順序とCRCを保証。欠番をGAPへ追加 |
| env_history_decode_control | HC20を検証し、request・operation・cursorを返す |
| env_history_encode_status | HS72。READY metadata、clock anchor、errorの整合性を検証 |
| env_history_encode_hint | HN8。小さいMTUでも通知可能 |
| env_history_crc32 | IEEE CRC32。空bufferのCRCは0 |
| env_clock_encode_state | CT32。専用時計サービスのboot・revision・anchorを検証 |
| env_clock_decode_write | 旧サービスとも共通のOT12を検証 |

成功時はbyte数を返す（Control decodeも20）。失敗はnegative errno。invalid inputや容量不足では出力を変更しない。env_history_crc32へは有効なspanを渡す（長さ0ならNULL可）。構造体のsizeofをwireサイズとして使わない。

GATT callbacksは要求の検証・キュー追加と固定済みbytesのReadだけを行い、SD処理はworkerで行う。DataとStatusの長Read中は別の処理からbufferを書き換えない。ページは一致するACK / CANCEL / 切断まで保持する。

~~~sh
python3 shared/firmware/history/tests/run_host_tests.py
~~~

独立のPython struct / zlib検算に合ったC bytesを、C・Swift共通goldenと照合する。fixtureを意図して更新する場合だけ --write-fixturesを指定する。

時計の仕様は [env-clock-v1.md](../../protocol/env-clock-v1.md)。env_clock.hをincludeし、同じenv_history_codec.cを使用する。時計の生成定数もgenerate_history.pyから生成する。
