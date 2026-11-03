import SwiftUI

struct DeviceCardView: View {
    let model: SensorAppModel
    let device: DeviceRecord
    @Environment(\.dynamicTypeSize) private var dynamicTypeSize
    @State private var showingImageUpload = false

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            HStack(alignment: .top) {
                VStack(alignment: .leading, spacing: 6) {
                    Text(device.name).font(.title2.bold())
                    Text(model.connectionState(for: device).title)
                        .font(.caption).foregroundStyle(.secondary)
                }
                Spacer()
                VStack(alignment: .trailing, spacing: 12) {
                    NavigationLink(value: device.id) {
                        Label("履歴", systemImage: "chart.xyaxis.line")
                            .font(.subheadline)
                    }
                    Button("画像", systemImage: "photo") { showingImageUpload = true }
                        .font(.subheadline)
                }
            }
            if let sample = model.latestReading(for: device.id) {
                if let measured = sample.reading.measuredAt {
                    Text("測定 \(measured.formatted(date: .abbreviated, time: .standard))")
                        .font(.caption).foregroundStyle(.secondary)
                } else if sample.reading.measurementSource?.hasPrefix("card-") == true {
                    Text("測定日時未確定")
                        .font(.caption).foregroundStyle(.secondary)
                }
                if case .failed(let message) = model.collector?.clockSync[device.id] {
                    Label(message, systemImage: "clock.badge.exclamationmark")
                        .font(.caption).foregroundStyle(.orange)
                }
                if let message = model.collector?.historySync.messages[device.id] {
                    Text(message).font(.caption).foregroundStyle(.secondary)
                }
                if let warning = model.collector?.historySync.warnings[device.id] {
                    Label(warning, systemImage: "exclamationmark.triangle")
                        .font(.caption).foregroundStyle(.orange)
                }
                TimelineView(.periodic(from: .now, by: 30)) { context in
                    Text("受信 \(sample.reading.receivedAt.formatted(.relative(presentation: .named)))")
                        .font(.caption)
                        .foregroundStyle(context.date.timeIntervalSince(sample.reading.receivedAt) > 180 ? .orange : .secondary)
                }
                LazyVGrid(columns: columns, alignment: .leading, spacing: 16) {
                    ForEach(SensorMetric.allCases) { metric in
                        MetricTileView(metric: metric, value: metric.value(in: sample.reading))
                    }
                }
                Text("VOC・NOxはraw値です。指数や濃度ではありません。未取得の項目は「—」で表示します。")
                    .font(.caption2).foregroundStyle(.secondary)
            } else {
                Label("最初の測定値を待っています", systemImage: "clock")
                    .font(.subheadline).foregroundStyle(.secondary)
            }
        }
        .padding(20)
        .background(.quaternary.opacity(0.35), in: RoundedRectangle(cornerRadius: 24))
        .sheet(isPresented: $showingImageUpload) { ImageUploadView(model: model, device: device) }
    }
    private var columns: [GridItem] {
        Array(repeating: GridItem(.flexible(), alignment: .leading), count: dynamicTypeSize.isAccessibilitySize ? 1 : 2)
    }
}
