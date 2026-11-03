import SwiftUI

struct CollectionSummaryView: View {
    let model: SensorAppModel

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Label(title, systemImage: "antenna.radiowaves.left.and.right")
                .font(.headline).foregroundStyle(Color.accentColor)
            Text(description).font(.subheadline).foregroundStyle(.secondary)
            HStack(spacing: 24) {
                Label("\(model.devices.count)台", systemImage: "sensor")
                Label("\((model.counts?.sampleCount ?? 0).formatted())件を保存", systemImage: "externaldrive")
            }
            .font(.caption).foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(20)
        .background(Color.accentColor.opacity(0.07), in: RoundedRectangle(cornerRadius: 24))
    }
    private var title: String {
        if model.isDemo { return "表示テスト" }
        guard let collector = model.collector else { return "受信を準備中" }
        if !collector.isCollecting { return "収集を停止中" }
        return collector.availability == .ready ? "バックグラウンド収集が有効" : collector.availability.title
    }
    private var description: String {
        if model.isDemo { return "画面と履歴を確認するためのサンプルです。" }
        guard let collector = model.collector else { return "" }
        if !collector.isCollecting { return "設定から収集を再開できます。履歴は残ります。" }
        if collector.availability != .ready { return collector.availability.message }
        return "登録したセンサーの通信を待ち、届いた測定値を保存します。"
    }
}
