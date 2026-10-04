# AS3935 雷センサー

回路のIC10のparts定義にある`deviceset=AS3935`を暫定採用したドライバー。
`PARTS`属性による型番の指定はないため、接続時に実際のモジュールを確認する。
現在は未接続。`CONFIG_APP_AS3935=n`では初期化・読み取りを呼ばない。

## 有効化と接続

`prj.conf`で`CONFIG_APP_AS3935=y`にすると、既存の1秒周期で状態を`printk()`する。
TWIM30へPCA9515のSCL1/SDA1を介して接続する。
回路にはアドレス設定の配線がないため、Devicetreeの`lightning@3` / `reg = <0x03>`は仮設定。
モジュールのA0/A1に合わせて、使用可能な`0x01`・`0x02`・`0x03`から選ぶ。

現在の回路にはMCUへのIRQ接続がないため、1秒ごとにイベントレジスターをポーリングする。
レジスター0x03の読み取りでIRQが解除される。周期内の複数イベントは個別に取得できない。
IRQを接続する場合は、立ち上がり後にスレッドから`as3935_read()`を呼ぶ。
API内で読み取り前に2ms待つため、割り込みハンドラーから直接呼ばない。

## APIと取得値

- `as3935_init()`：屋外用ゲイン・検出条件を設定し、TRCO/SRCOを校正。校正失敗はエラーを返す。
- `as3935_read()`：割り込み理由、雷・過大ノイズ・妨害信号フラグ、21bitのエネルギー値、距離コード、設定値、校正状態を取得。
- `as3935_calibrate()`：RC発振器を再校正。
- `as3935_clear_statistics()`：距離推定用の雷統計をクリア。
- `as3935_stop()`：パワーダウン。

エネルギー値はセンサー固有の数値で、ジュールへの変換はしない。
新しい雷の検出は`lightning`で判定する。エネルギー・距離は以前のイベントの値を保持する場合がある。
`distance_km`は数値の推定がある場合だけkmを返し、それ以外は`-1`。
頭上は`storm_overhead`、範囲外は`out_of_range`で区別し、`distance_code`も保持する。

Devicetreeで`indoor`、`noise-floor-level`、`watchdog-threshold`、`spike-rejection`、
`minimum-lightnings`、`mask-disturbers`を変更できる。
`tuning-capacitor`を指定しない場合は既存のアンテナ調整値を保持する。
指定する場合は0〜15、1段階8pF。アンテナの共振調整は接続後に確認する。

## 検証と参照

実際のCソースを模擬I2Cで実行し、設定値・予約ビットの保持、校正失敗、
割り込みの解除、エネルギー・距離コード、統計クリアをテスト済み。
有効構成のARMビルドは確認済み。実機での通信・雷検出は未検証。

- [メーカーのAS3935データシート](https://www.sciosense.com/wp-content/uploads/2024/01/AS3935-Datasheet.pdf) v1-04、レジスター定義・Interrupt Management・RC Oscillators
