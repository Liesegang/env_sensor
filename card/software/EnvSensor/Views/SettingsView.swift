import SwiftUI

struct SettingsView: View {
    let model: SensorAppModel

    var body: some View {
        NavigationStack {
            Form {
                if model.isDemo {
                    Section { DemoNoticeView() }
                }
                Section {
                    CollectionToggleView(model: model)
                    if let collector = model.collector {
                        Label(collector.availability.title, systemImage: "antenna.radiowaves.left.and.right")
                        if collector.availability != .ready {
                            Text(collector.availability.message)
                                .font(.footnote).foregroundStyle(.secondary)
                        }
                    }
                } header: {
                    Text("バックグラウンド収集")
                } footer: {
                    Text("初回はアプリを開いてデバイスを登録してください。登録後はiOSに再接続を待たせ、届いた値を保存します。")
                }
                if !model.devices.isEmpty {
                    Section("登録したセンサー") {
                        ForEach(model.devices) { device in
                            DeviceCollectionToggleView(model: model, device: device)
                        }
                    }
                }
                Section("バックグラウンドで受け取るには") {
                    Label("Bluetoothをオンにする", systemImage: "antenna.radiowaves.left.and.right")
                    Label("センサーを通信範囲内に置く", systemImage: "sensor")
                    Label("アプリを強制終了した後は、一度開く", systemImage: "arrow.clockwise")
                    Text("通常は画面を閉じたまま受信できます。iOSの制御や短い通信窓のため、毎分の受信を保証するものではありません。届かなかった測定値は現在のBLEプロトコルでは補完されません。")
                        .font(.footnote).foregroundStyle(.secondary)
                }
                Section("ペアリング") {
                    Label("LE Secure Connections", systemImage: "lock.shield")
                    Text("対応するファームウェアでは、初回接続時にiPhoneのペアリング画面が開きます。cardに表示された6桁の番号を入力してください。以後の暗号化と鍵の管理はiOSが行います。")
                        .font(.footnote).foregroundStyle(.secondary)
                }
                Section("データ") {
                    LabeledContent("保存した測定", value: "\((model.counts?.sampleCount ?? 0).formatted())件")
                    LabeledContent("保存先", value: "このiPhone")
                    Text("履歴画面からJSON・CSVを書き出せます。IoTサーバーへの送信は、接続先と認証方式を決めてから追加します。")
                        .font(.footnote).foregroundStyle(.secondary)
                }
                Section {
                    LabeledContent("バージョン", value: "1.0")
                    LabeledContent("共通プロトコル", value: "env-sensor v1")
                }
            }
            .navigationTitle("設定")
        }
    }
}
