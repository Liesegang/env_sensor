import SwiftUI

struct DiscoveryView: View {
    let model: SensorAppModel
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            List {
                Section {
                    VStack(alignment: .leading, spacing: 12) {
                        Label(model.collector?.isScanning == true ? "近くのセンサーを探しています" : "センサーを登録",
                              systemImage: "antenna.radiowaves.left.and.right")
                            .font(.headline)
                        Text("センサーの電源を入れ、iPhoneの近くに置いてください。cardは通常1分ごとに通信します。")
                            .font(.subheadline).foregroundStyle(.secondary)
                        Text("初回は、cardに表示される6桁のパスキーをiPhoneのペアリング画面へ入力します。")
                            .font(.footnote).foregroundStyle(.secondary)
                    }
                    .padding(.vertical, 8)
                }
                if let collector = model.collector, collector.availability != .ready {
                    Section {
                        Label(collector.availability.title, systemImage: "exclamationmark.circle")
                        Text(collector.availability.message).font(.footnote).foregroundStyle(.secondary)
                    }
                } else if model.collector?.discoveredDevices.isEmpty != false {
                    Section {
                        HStack {
                            ProgressView().padding(.trailing, 8)
                            Text("センサーの通信を待っています")
                                .font(.subheadline).foregroundStyle(.secondary)
                        }
                    }
                }
                if let collector = model.collector {
                    Section("見つかったセンサー") {
                        ForEach(collector.discoveredDevices) { device in
                            DiscoveryDeviceRow(device: device,
                                registered: model.devices.contains { $0.id == device.id },
                                register: { register(device) })
                        }
                    }
                }
            }
            .navigationTitle("デバイスを追加")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("閉じる", action: { dismiss() })
                }
            }
            .onAppear(perform: startDiscovery)
            .onDisappear { model.collector?.stopDiscovery() }
        }
    }
    private func startDiscovery() { model.collector?.startDiscovery() }
    private func register(_ device: DiscoveredSensor) {
        if model.registerDevice(device) { dismiss() }
    }
}
