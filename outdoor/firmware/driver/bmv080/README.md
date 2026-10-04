# BMV080 SDK 接続モジュール

実装型番はユーザーがBMV080と確認済み。回路のIC9の`value`も`BMV080`。
PS=High、CSB/A1=Low、MISO/A0=LowのI2Cアドレス`0x54`を使用する。
SCL1/SDA1からPCA9515を介してTWIM30へ接続する設計だが、現在の基板の
pin/pad対応は逆になっている。[IC9の配線照合](../../../hardware/BMV080-pin-review.md)を参照。

ユーザー提供のBosch公式SDK v11.2.0を導入し、Cortex-M33のARMビルドとリンクを確認済み。
通常ビルドでは`CONFIG_APP_BMV080=n`で、
アプリは初期化・読み取りを呼ばない。無効側の`bmv080_driver_init()`は`-ENOSYS`を返す。
実機のPM測定は未検証。デバイス切断中のため今回のコードは書き込んでいない。

## SDKの導入

`outdoor/firmware`で実行する。ユーザー提供のZIPから必要なファイルを
Git対象外の`.local/bmv080-sdk-v11-2-0/`へ展開する。
現在の`CONFIG_FPU=n`に合わせ、ARM GCC / Cortex-M33のsoft-float版を使う。
SDKはリポジトリに同梱しない。

```sh
./scripts/fw test-bmv080 ~/Downloads/bmv080-sdk-v11-2-0.zip
```

`.local/bmv080_sdk.conf`を生成し、`.local/build_bmv080`へビルドする。
ZIPのSHA-256を`IMPORT.txt`へ記録する。通常構成の`prj.conf`は変更せず、書き込みもしない。
次回からは`./scripts/fw test-bmv080`だけで検証できる。

生成する追加設定は次の内容。配置を移動した場合は上のZIP指定コマンドを再実行する。

```ini
CONFIG_APP_BMV080=y
CONFIG_MAIN_STACK_SIZE=16384
CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE=65536
CONFIG_BMV080_SDK_INCLUDE_DIR="/absolute/path/to/directory/containing/bmv080.h"
CONFIG_BMV080_SDK_DRIVER_LIBRARY="/absolute/path/to/arm-cortex-m33/lib_bmv080.a"
CONFIG_BMV080_SDK_POSTPROCESSOR_LIBRARY="/absolute/path/to/arm-cortex-m33/lib_postProcessor.a"
```

SDK v11.2.0有効構成のビルド・リンクと11件の基板構成検証が通過した。
Flashは240876 bytes、RAMは162408 bytes（256KiBの約62%）を使用する。
検証ログは`.local/reorganized_bmv080_test.log`。
コードの検証は実機の通信・PM値・動作時のスタック使用量を保証するものではない。
SDKファイル不足、またはmainスタックが12KiB未満の場合はCMakeで停止する。
SDKの仕様では10KiBのスタックが必要。

## 取得内容とスケジュール

- PM1.0・PM2.5・PM10の質量濃度（µg/m³）と粒子数濃度（particles/cm³）
- 遮蔽フラグ、測定範囲外フラグ、測定開始後の経過時間
- 受信連番、受信からの経過時間、前回printkから更新されたかの`fresh`フラグ

連続測定開始後、16KiBスタックの専用スレッドでSDKを10msごとにサービスする。
mainは1秒ごとに最新の結果を取り出す。最大出力レートは約0.97Hzなので、
更新がない回は`fresh=0`と`age_ms`で区別する。最初の結果までは`-EAGAIN`。
SDKの予約フィールドと内部専用ポインタは公開しない。

SDK初期化時にI2Cを400kHzへ切り替える。16bitヘッダーとデータをMSB-firstで転送し、
readはヘッダー送信後にSTOPを入れる。readは128wordごとに分割し、
writeは最大2048wordまで。実機での転送は未検証。

## 参照

- [保存したBosch BMV080データシート](../../../../docs/datasheets/Bosch_BMV080_datasheet.pdf) §4.4.2、§5
- [対応プラットフォーム](https://www.bosch-sensortec.com/media/boschsensortec/software_tools/software/bmv080_1/supported_platforms/bmv080_binary_size_information.pdf)
- [Bosch公式のSDK統合例](https://github.com/boschsensortec/BMV_BME/tree/a94b769/src)
