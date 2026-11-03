# 共通センサーテレメトリー

`env_telemetry_encode.c` と `include/env_telemetry.h` は共通スキーマから生成されます。
生成元は `shared/protocol/env-sensor-v1.json` です。生成済みファイルを直接編集しないでください。
C 構造体をそのまま送信せず、スキーマ順に little endian で直列化します。
ドライバーヘッダーに依存しないので、Room などの別ボードでも同じ型を利用できます。

## 組み込み

アプリの CMake からこのディレクトリを追加します。
共通エンコーダーは常に組み込まれ、BLE トランスポートは `CONFIG_BT_PERIPHERAL=y` のときに組み込まれます。
BLE は `CONFIG_BT=y`、`CONFIG_BT_PERIPHERAL=y`、`CONFIG_BT_MAX_CONN=1` が必要です。
機器名はアプリの `CONFIG_BT_DEVICE_NAME` で指定します。

```c
struct env_telemetry_sample initial = {
    .enabled_mask = 0x01,
    .opt4001.error = -EAGAIN,
};
env_telemetry_ble_publish(&initial);
struct env_telemetry_ble_config config = {0};
int err = env_telemetry_ble_init(&config);
```

新しい測定値ができたときだけ `env_telemetry_ble_publish()` を呼びます。
`active_window_ms=0` は常時接続可能なモードです。
0 以外は初期化直後に広告せず、データ更新のたびに指定時間だけ広告・接続を有効にします。
期限では広告を停止し、接続中の中央機器も切断します。
Card は 5,000 ms を指定し、毎分の実測値を送信するときだけ 5 秒間有効になります。
更新が重なった場合は期限を延長し、古いタイマーでは新しい送信窓を閉じません。
再接続・購読は中央機器が毎回行う必要があります。
呼び出し中にデータをコピーするため、返った後は入力バッファを再利用できます。
接続と Notify 購読がある場合は最新値を送信専用スレッドから通知し、
未接続なら最新値だけを保持します。新しい購読の開始時も最新値を通知します。
Read は長い offset 読み取り中に値が更新されても、最初の読み取り時のフレームを維持します。
BLE 通知待ちでセンサーの測定スレッドを止めません。過去データの再送はありません。

## 消費電力

初期設定の広告間隔は 1 秒です。Card は短い送信時間内に発見できるよう 100 ms を指定します。
広告にサービス UUID を載せ、測定フレームは GATT 接続で転送します。
Card の広告は毎分の 5 秒間だけで、残りの時間は広告も接続も停止します。
待機中に CPU を回すループはなく、期限・再広告・通知は待機可能な Zephyr の work/thread で処理します。
広告間隔を変える場合は `advertising_interval_min/max` を 0.625 ms 単位で指定します。
接続間隔・latency は各アプリの Zephyr 設定で指定し、中央機器との交渉で決まります。
Card は 200〜300 ms、latency=4、supervision timeout=6 s を要求します。
MTU=247 を使う場合は 4 センサー＋未接続 SFA40 の 146 バイトを 1 通知で送れます。
MTU=23 でも既存の分割形式を使います。

Card は `disable_between_windows=true` により、送信時間の終了時に `bt_disable()` を呼びます。
NCS の `CONFIG_BT_UNINIT_MPSL_ON_DISABLE=y` では Bluetooth host/controller と MPSL を停止し、
次の実測値の更新時に `bt_enable(NULL)` で再開します。
初期化時も一度 enable/disable を行い、起動時に初期化される MPSL を最初の測定まで停止します。
同期 HCI と接続解放を待つ処理は専用の通常スレッドで行い、system workqueue を待機させません。
5 秒の通信期限で切断を要求した後、通常の切断完了・接続スロット解放を最大 1 秒待ちます。
これにより LL の切断通知を送る前に controller を reset することを避けます。
相手が応答せず期限を超えた場合は強制停止し、次の測定まで再開しません。
再開時に同じ BLE identity を復元し、静的 GATT service と connection callbacks は再登録しません。
設定は次が必要です。常時接続の Outdoor はこの停止機能を使用しません。

```conf
CONFIG_DYNAMIC_INTERRUPTS=y
CONFIG_DYNAMIC_DIRECT_INTERRUPTS=y
CONFIG_MPSL_DYNAMIC_INTERRUPTS=y
CONFIG_BT_UNINIT_MPSL_ON_DISABLE=y
```

この停止機能は通常の未bond peripheral、または下記の認証付きCard構成に対応します。
認証付き構成では毎回 `bt_enable()` 後に `settings_load_subtree("bt")` を完了してから広告します。
接続状態が解放されない場合や enable/disable が失敗した場合は RTT にエラーを出し、
再起動まで BLE の開始を停止します。センサーの測定・SD 記録は継続できます。
NCS 3.4.1 の MPSL は停止時に予約済みの RC calibration work を取り消さないため、
停止後 4 秒以内に最後の callback が 1 回実行される場合があります。
その callback は calibration を行わずに終了し、繰り返しの 4 秒起床は停止します。

## 時刻とログ

`set_clock_packet` を設定すると、共通トランスポートが検証した `OT` v1 の 12 バイトを渡します。
成功を返したときだけ Write を成功として応答します。NULL の場合は時刻 Write を拒否します。
`log_publications=true` にすると、更新時の連番・サイズ・接続・購読状態と、
通知完了時の連番・サイズ・断片数を RTT に出します。Card の実機確認に使用します。


## 認証付きCard（任意の機能）

Cardは `CONFIG_ENV_TELEMETRY_SECURE=y` を指定します。Outdoorは未指定なので従来どおりです。
アプリのKconfigからこのディレクトリの `Kconfig` を `rsource` します。
必要なZephyr設定は `BT_SMP`、`BT_SMP_SC_ONLY`、`BT_APP_PASSKEY`、
`BT_SMP_APP_PAIRING_ACCEPT`、`BT_BONDING_REQUIRED`、`BT_MAX_PAIRED=1`、
`BT_SETTINGS`、`SETTINGS`と不揮発settings backendです。
Read、clock Write、CCC Read/Writeは `*_LESC | *_AUTHEN` permissionでsecurity level 4を要求し、
NotifyもL4になるまで送信しません。SC＋MITM＋128bit暗号化に達しない接続から測定値は読めません。
UUIDとOE/OF/OTのデータ形式は変えません。

未登録の初回だけ自動で登録窓を開きます。登録窓は `pairing_window_ms=180000`。
`bt_rand()`の暗号学的乱数を偏りなく6桁へ変換し、`pairing_event(PASSKEY, pin, request_id)` を
アプリへ渡します。アプリは非同期で物理画面を更新し、完了時に
`env_telemetry_ble_pairing_display_ready(request_id, error)` を呼びます。
完了前は広告・新規接続を始めません。古いrequest IDの完了は無視します。
既に進行している画面更新を待つ場合も、SMPの入力期限はまだ始まりません。
描画完了から180秒広告し、接続後はZephyrの標準SMP入力期限30秒が適用されます。

`app_passkey` callbackで表示済みPINを一度だけ消費し、`passkey_display` callbackで一致を確認します。
`BT_FIXED_PASSKEY`や`bt_passkey_set()`は使用しません。
失敗・キャンセル・途中切断ではPINを無効にし、接続を閉じてから新しいPINを表示し直します。
直前と同じ数は再抽選します。登録成功か登録期限終了で表示を解除します。
番号はRTTログに出しません。表示に失敗した場合は登録窓を閉じ、物理操作による再試行が必要です。

鍵はZephyrがpairing completeを通知する前にsettingsへ保存します。
SDKが書込エラーをcallbackへ伝播しないため、senderは公開direct settings APIで
保存キーの存在を確認してから登録完了として扱います。
期限後の遅い完了callbackは窓を再開せず、既に保存された新bondをsenderで削除します。
毎分停止で消えるhost identity/key poolは、再開時にsettingsから復元します。
SDKが予約するidentity保存はsystemqueueで非同期なので、NOP workのFIFO barrierを
通常threadから `k_work_flush` して先行書込を完了させます。
privacy無効時の工場固定アドレスはSDKが自動保存しないため、初回に`bt/id`が
存在しない場合だけ公開settings APIで同期保存し、再読して現在のidentityとの一致を確認します。
既存recordの長さ・読み取り・一致確認が失敗した場合は上書きせず停止します。
auth callbacksと静的GATT serviceは一度だけ登録します。
CCC/client featureは切断時に保存し、追加の遅延settings timerは使いません。
登録済みの場合は通常5秒窓へ戻り、そのownerの再暗号化のみ許可します。
登録窓外の新しいpairing requestは拒否し、自動的に古いownerを置き換えません。

`env_telemetry_ble_repair()` はアプリの物理操作からのみ呼びます。
通常スレッドで切断完了を待ち、`bt_unpair(BT_ID_DEFAULT, NULL)`で保存済みpeer bondを削除し、
保存キーが消えたことを確認してから新しいPINの描画を始めます。local identityは保持します。
削除を確認できない場合はcontrollerを停止し、再起動までBLEを閉じます。BLEコマンドからは呼べません。
settingsが読めない場合はcontrollerを停止してfail closedし、再起動まで再開しません。

### 検証

`bash tests/run_host_tests.sh` は実際のtransportをSDK stubでコンパイルします。
通常Outdoor/windowed lifecycle、ATT待ちからの期限切断、HCI close所有権、
正常切断待ち、事前PIN描画、PINの一度消費、古い完了通知、L4 Notifyゲート、
保存鍵の再読込順序、物理owner置換、期限を跨いだ完了、表示/設定失敗を確認します。
実際のCard adapterの初期encode・初期化も実行し、工場IDの初回保存、保存失敗、
保存後の再読失敗、既存ID不正時の上書き禁止を確認します。
SMP暗号処理・RRAM書込・iOS OS dialogそのものはstubの対象外です。
実機では初回ペアリング→毎分再接続→基板電源断・復帰→同じiPhoneからPINなし再接続、
別iPhoneの新規pair拒否→PUSH再登録→新ownerへ置換、を別途確認します。

SDK根拠: NCS v3.4.1内 `zephyr/include/zephyr/bluetooth/conn.h` (`app_passkey`, L4)、
`host/smp.c` (`display_passkey`, IO capability, 30秒SMP timeout)、
`host/settings.c` (identity/keys commit)、`host/hci_core.c` (`bt_disable`でID/key pool clear)。
