# Outdoor Web BLE receiver

TypeScript / React / ViteによるBLE受信アプリ。
`Outdoor Sensor`へ接続し、1つのcharacteristicのNotifyから1秒ごとの全センサー値を取得する。
接続直後はReadで最新値も取得する。

## 起動

Node.js 24 LTS以上（開発時は25.9.0）、npmを使用する。

```sh
cd /Users/yuki/src/github.com/Liesegang/env_sensor/outdoor/software
npm ci
npm run dev
```

ChromeまたはEdgeで[http://127.0.0.1:5178](http://127.0.0.1:5178)を開き、
「BLE接続」から`Outdoor Sensor`を選ぶ。ローカル開発以外ではHTTPSを使う。
Web Bluetoothの対応状況・ユーザー操作の要件は[Chrome公式資料](https://developer.chrome.com/docs/capabilities/bluetooth)を参照。

Serviceは`722acd80-e77f-468d-a916-67e599e7cffd`、
characteristicは`722acd81-e77f-468d-a916-67e599e7cffd`。
詳しい形式は[共通プロトコル](../protocol/README.md)。
機器名だけで識別せず、広告中のService UUIDで選択を絞り込む。

## 画面と保存

- 温度・湿度・CO₂・照度・気圧・VOC Index・NOx Indexの最新値。温湿度はSHT45を優先し、失敗時はBME690へ切り替える。
- 全21項目の物理測定値（気圧・照度・各センサーの温湿度・VOC/NOx・CO₂・HCHO・PM質量/粒子数・雷エネルギー/距離）を最新15分、最大900件のグラフに重ねて表示。STCC4とSHT45の所有を分離し、温湿度のセンサー間フォールバックは行わない。
- 単位の違う項目は各項目の表示範囲を0〜100%へ揃え、凡例に最新の実測値・単位・範囲を表示。凡例で個別切り替え、「すべて表示」「すべて非表示」に対応。1項目だけ選ぶと実単位の縦軸を表示。
- 欠測や2.5秒を超える間隔は線をつながない。暖機中のHCHO/Index、無効なガス抵抗・雷イベントの値をグラフに描かない。
- 7つの測定ブロックを8つのセンサーとして表示し、STCC4（CO₂）とSHT45（温湿度）を分離。読み取り失敗は`—`と表示し、無効設定・暖機・失敗を区別する。
- 「詳細を表示」でraw値・識別情報・状態フラグ・UUID・不正通知数を表示。
- CSV保存は保持中の全測定・全rawフィールド・errnoをUTF-8 BOM付きで出力。単位の倍率はJSONスキーマに定義。

VOC/NOxは公式アルゴリズムによるIndex（1〜500）で、ppm/ppbではない。
暖機中のIndexは`—`、SGP41の状態は「暖機中」と表示する。
SGP41 block version=2が必要なため、以前のファームウェアは更新が必要。
2026-10-04に対応ファームウェアを書き込み済み。
MacのCoreBluetoothで実際のRead/Notifyを受信し、`src/protocol.ts`でデコードを確認した。
ブラウザーから実機への接続・描画は引き続き未検証。

データはページのメモリー内に保持する。サーバーへの送信・永続保存は行わない。
再読み込みや新しい接続で履歴が消えるため、必要なデータはCSV保存する。
切断時は最後の測定を残し、「更新停止」を表示する。3.5秒以上受信がない場合も停止として示す。
接続は明示的な操作で開始し、中止・切断時に購読と途中パケットを破棄する。

## コードと検証

`src/protocol.ts`が長さ・バージョン・状態・分割を検証するデコーダー。
`src/bluetooth.ts`が接続と受信を管理し、`useSyncExternalStore`でReactへ状態を渡す。
表示・グラフ・表・CSVは接続管理から分けている。
strict、noUncheckedIndexedAccess、exactOptionalPropertyTypes、React HooksのLintを有効にする。

```sh
npm run build
npm run lint
npm test
npx playwright install chromium --only-shell
npm run test:e2e
```

`npm test`はCエンコーダーをコンパイルするため、Python 3とホストのCコンパイラーが必要。
Nordic SDKや接続した基板は不要。Cの出力からテストベクトルを再生成する。
Playwrightは模擬Web Bluetoothで接続・Notify・分割受信・CSV・中止・再接続・非対応ブラウザーを検証する。
実機による受信とは別のテストである。

画面は組み込みブラウザーでも確認する。Browser専用プラグインは未導入のため、
自動テスト・1536×1024と390×844の確認にはプロジェクトのPlaywrightを使用する。

2026-10-04の移動後の検証はbuild・lint・Vitest 18件・Playwright 8件が通過。
VOC/NOxの暖機中表示、旧SGP41形式の更新案内、STCC4/SHT45分離、
Indexグラフの範囲（1〜500）、1536×1024と390×844の空画面・模擬BLE受信画面を確認した。

## SDログの時計同期

BLE接続時、同じcharacteristicへブラウザーのUTC時刻（`Date.now()`）を
Write With Responseで送る。成功時は受信情報に「時刻同期済み（SDログ用）」を表示する。
書き込み失敗は測定受信を止めず、別の通知で再接続を案内する。
再接続すると同期をやり直す。旧ファームウェアではWriteに非対応なので更新が必要。
デバイスは以後ブラウザー切断中も内部GRTCで時刻を維持し、毎秒の値を1分ごとにSDへ書き込む。
電源断・リセット後は再同期が必要。
[SDログの形式・運用](../firmware/logging/README.md)を参照。
