# SFA40 ホルムアルデヒドセンサー

ユーザーが実装型番をSFA40と確認したIC3のドライバー。
2026-10-04に接続を確認し、初期設定は`CONFIG_APP_SFA40=y`。
`CONFIG_APP_SFA40=n`では初期化・読み取りを呼ばない。
SFA40のコマンドとCRCを使用する。

## 有効化と接続

SCL1/SDA1からPCA9515を介してTWIM30へ接続する。I2Cアドレスは`0x5d`。
`prj.conf`で`CONFIG_APP_SFA40=y`にすると、既存の1秒周期で値を`printk()`する。

## APIと取得値

- `sfa40_init()`：測定停止、product ID・serial取得、連続測定開始。
- `sfa40_read()`：HCHO濃度、温度、相対湿度、raw値、状態、識別情報を取得。
- `sfa40_stop()`：連続測定を停止。

HCHOは`hcho_millippb`（0.001ppb単位、センサー分解能は0.1ppb）、
温度は`temperature_mc`（0.001°C）、湿度は`humidity_mpercent`（0.001%RH）。
受信データの各ワードのCRCを検証し、異常時は負のerrnoを返して出力構造体を更新しない。
測定開始から最初の500msは`-EAGAIN`。APIは待ち時間を含むためスレッドから呼ぶ。

暖機状態は受信したstatusの上位バイトから取得する。
電源投入後の最初の1分は`ready=0`、HCHO出力は0。
1〜10分は`ready=1 within_specification=0`、10分以降は両方1。
温湿度と濃度を表示するときも、この状態フラグを併記する。

## 検証と参照

実際のCソースを模擬I2Cで実行し、コマンド・識別情報・初回待ち、
CRC異常、statusの位置と暖機フラグ、単位変換と境界値、未接続時のエラーをテスト済み。
有効構成のARMビルド・実機書き込みを確認済み。2026-10-04の実機で`0x5d`を検出し、
product ID `0x09020283`、serial `4f926e9f20dc`を取得した。
1秒周期の読み取りと、最初の1分の`ready=0`から`ready=1`への遷移を確認。
BLE Readと15回のNotifyもWebアプリと同じデコーダーで検証し、
HCHO 2.5ppb・温度26.631°C・湿度49.996%RHを取得した。
この時点では`within_specification=0`で、10分の安定化完了は未確認。

- [Sensirion SFA40データシート](https://sensirion.com/media/documents/5B06EDD9/69F84BD8/Sensirion_Datasheet_SFA40.pdf) Version 1.1、April 2026
- [Sensirion公式組み込みドライバー](https://github.com/Sensirion/embedded-i2c-sfa4x/tree/1d7da43d9bd88ad5d9636d18807f52dbb6b86b4a)：コマンド・停止後700msの待ち時間を参照
