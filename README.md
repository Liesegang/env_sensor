# Environment sensors

機種ごとに回路・ファームウェア・受信アプリ・プロトコルをまとめる。

```text
card/
  hardware/       KiCad / Fusionの回路・基板・3Dモデル
  firmware/       実装用ディレクトリ
outdoor/
  hardware/       env_sensor_3の回路・基板
  firmware/       nRF54L15 / Zephyr、driver/に各センサー
  software/       TypeScript / ReactのWeb BLE受信アプリ
  protocol/       BLEの共通スキーマ・Cコード生成
room/
  hardware/       env_sensor_1aの回路・基板
docs/
  datasheets/     機種共通のメーカー資料
```

Outdoorの開発・検証は[ファームウェア](outdoor/firmware/README.md)、
[Web受信アプリ](outdoor/software/README.md)、[BLE仕様](outdoor/protocol/README.md)を参照。
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
