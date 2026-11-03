# Environment sensors

機種ごとに回路・ファームウェア・受信アプリ・プロトコルをまとめる。

```text
card/
  hardware/       v1 KiCad / v2 Fusionの回路・基板・3Dモデル
  firmware/       nRF54L15 / Zephyr
  software/       SwiftUI / iPhoneのBLE収集・履歴アプリ
outdoor/
  hardware/       env_sensor_3の回路・基板
  firmware/       nRF54L15 / Zephyr
  software/       TypeScript / ReactのWeb BLE受信アプリ
  protocol/       共通BLEスキーマへの互換入口
shared/
  firmware/driver/     cardとoutdoor共通のセンサー・表示ドライバー
  firmware/telemetry/  共通BLEエンコーダー・GATT通信
  protocol/           BLEの共通スキーマ・C / TypeScript / Swiftコード生成
room/
  hardware/       env_sensor_1aの回路・基板
docs/
  datasheets/     機種共通のメーカー資料
```

Outdoorの開発・検証は[ファームウェア](outdoor/firmware/README.md)、
[Web受信アプリ](outdoor/software/README.md)、[BLE仕様](outdoor/protocol/README.md)を参照。
[Card firmware](card/firmware/README.md)はv2基板で毎分測定・BLE送信、
10分ごとのSD一括記録、1時間ごとの電子ペーパー更新を行う。
[iPhoneアプリ](card/software/README.md)は共通BLEで受信し、バックグラウンド収集・端末内の履歴保存・JSON / CSV書き出しを行う。
資料の一覧は[データシート](docs/datasheets/README.md)にある。

BMV080のIC9はセンサーpinとコネクターpadの対応が反転している。
[配線の照合結果](outdoor/hardware/BMV080-pin-review.md)を参照。回路・基板の配線は未修正。

リポジトリのルートから実行する：

```sh
python3 outdoor/protocol/generate.py --check
outdoor/firmware/scripts/fw test
cd outdoor/software
npm ci
npm run dev
```

ファームウェアのSDK、バックアップ、実機ログは`outdoor/firmware/.local/`へ保存し、Gitに含めない。
