import SwiftUI

struct HistoryView: View {
    @Bindable var model: SensorAppModel

    var body: some View {
        NavigationStack {
            List {
                if model.isDemo {
                    Section { DemoNoticeView() }
                }
                Section {
                    LabeledContent("保存した測定", value: "\(model.counts?.total ?? 0) 件")
                    LabeledContent("登録したセンサー", value: "\(model.devices.count) 台")
                    Label("複数の測定項目を、同じ時間軸で比較できます。", systemImage: "chart.xyaxis.line")
                        .font(.subheadline).foregroundStyle(.secondary)
                }

                Section("センサーを選んでグラフを開く") {
                    if model.devices.isEmpty {
                        ContentUnavailableView("履歴はまだありません", systemImage: "chart.xyaxis.line",
                            description: Text("センサーを登録すると、受信した値がここに保存されます。"))
                    } else {
                        ForEach(model.devices) { device in
                            NavigationLink(value: device.id) {
                                HStack {
                                    VStack(alignment: .leading, spacing: 5) {
                                        Text(device.name)
                                            .font(.headline)
                                        Text(model.connectionState(for: device).title)
                                            .font(.caption)
                                            .foregroundStyle(.secondary)
                                        Text("複数項目・期間指定・拡大")
                                            .font(.footnote).foregroundStyle(.secondary)
                                    }
                                    Spacer()
                                    Text("\(model.counts?.samplesByDevice[device.id] ?? 0) 件")
                                        .font(.subheadline.monospacedDigit())
                                        .foregroundStyle(.secondary)
                                }
                                .padding(.vertical, 4)
                            }
                        }
                    }
                }

                Section("ファイルに書き出す") {
                    HistoryExportView(model: model)
                }

            }
            .navigationTitle("履歴")
            .navigationDestination(for: String.self) { deviceID in
                DeviceHistoryView(model: model, deviceID: deviceID)
            }
            .refreshable { model.refresh() }
        }
    }
}
