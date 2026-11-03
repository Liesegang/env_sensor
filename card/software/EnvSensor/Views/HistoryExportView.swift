import SwiftUI

struct HistoryExportView: View {
    @Bindable var model: SensorAppModel
    var deviceID: String?
    var range: ClosedRange<Date>?
    @State private var exportURL: URL?
    @State private var isPreparing = false

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Menu("データを共有", systemImage: "square.and.arrow.up") {
                Button("JSON ファイル", systemImage: "curlybraces") { prepare(.json) }
                Button("CSV ファイル", systemImage: "tablecells") { prepare(.csv) }
            }
            .disabled(isPreparing)
            .buttonStyle(.bordered)
            .controlSize(.large)

            if isPreparing {
                ProgressView("共有ファイルを準備中")
                    .font(.subheadline)
            }
            if let exportURL {
                ShareLink("\(exportURL.pathExtension.uppercased()) を共有", item: exportURL)
            }
            Text(range == nil ? "保存済みの全データをファイルにまとめます。日時未確定の記録も含めます。" : "表示中の期間の全記録を書き出します。描画用の間引きは行いません。日時未確定の記録は含めません。")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .padding(.vertical, 4)
        .onChange(of: deviceID) { exportURL = nil }
        .onChange(of: range) { exportURL = nil }
    }

    private func prepare(_ format: ExportFormat) {
        guard !isPreparing else { return }
        isPreparing = true
        exportURL = nil
        let requestedRange = range
        let requestedDevice = deviceID
        Task {
            let url = await model.prepareExport(format: format, deviceID: requestedDevice,
                since: requestedRange?.lowerBound, until: requestedRange?.upperBound)
            if requestedRange == range, requestedDevice == deviceID { exportURL = url }
            isPreparing = false
        }
    }
}
