# SGP41 / VOC Index・NOx Index

Sensirion公式Gas Index Algorithm v3.2.0のfixed-point実装を使い、
1秒ごとのSGP41 raw信号をVOC Index・NOx Index（1〜500）へ変換する。
絶対濃度のppm/ppbへの換算ではなく、VOC Indexの標準的な基準は100、NOx Indexの基準は1。
raw値は診断とCSV用に保持する。

公式ソースは`vendor/`へBSD-3-Clauseのライセンスとともに保存し、
revisionを[UPSTREAM.md](vendor/UPSTREAM.md)に固定した。
`gas_indices.c`が2つのアルゴリズム状態と有効フラグを管理する。

起動後の最初の10秒はセンサーのconditioningを行い、NOx rawを使用しない。
アルゴリズムの最初の約45秒も暖機扱いにする。
有効なraw値を連続して1Hzで取得すると、VOCは初回から47回目、
NOxはconditioning後の47回目（起動から約57回目）にIndexが有効になる。
実機では最初のVOC rawが0だったため、VOCは起動から48回目に有効になった。
暖機中はIndex値を0、有効フラグをfalseにし、Webでは`—`と表示する。

CRCが通った測定だけ処理する。間隔が1500msを超える欠測、同一時刻、
時刻の巻き戻りでは両方の履歴をリセットする。
範囲外rawは該当アルゴリズムをリセットし、NOx conditioning中もNOx履歴をリセットする。
停止・再初期化では両方を初期化する。

`tests/check_gas_indices.py`は公式Cソースとこのアダプターをホストで実行し、
conditioning、暖機、基準値、信号変化への反応、欠測、無効入力を確認する。
2026-10-04に実機へ書き込み、暖機後のVOC/NOx Indexと
STCC4/SHT45の温湿度を用いた補償をRTTで確認。
実際のBLE Read/NotifyをWebアプリのデコーダーで検証した。

- [Sensirion Gas Index Algorithm](https://github.com/Sensirion/gas-index-algorithm/tree/2ef9f13d225e8a0dedd3ff42dd229af4dbb1aae4)
- [SGP41 datasheet](../../../../docs/datasheets/Sensirion_Gas_Sensors_Datasheet_SGP41.pdf)
