import SwiftUI

struct HistoryAxisSelectionView: View {
    let metrics: [SensorMetric]
    @Binding var selected: Set<SensorMetric>
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            List {
                Section {
                    Text("目盛りを表示する項目を最大3つ選びます。すべての選択項目の線は重ねて表示し、各項目の範囲は凡例でも確認できます。")
                        .foregroundStyle(.secondary)
                }
                Section("軸を表示する項目") {
                    ForEach(metrics) { metric in
                        Button {
                            if selected.contains(metric) {
                                if selected.count > 1 { selected.remove(metric) }
                            } else if selected.count < 3 { selected.insert(metric) }
                        } label: {
                            HStack {
                                Label(metric.title, systemImage: metric.systemImage)
                                    .foregroundStyle(.primary)
                                Spacer()
                                Image(systemName: selected.contains(metric) ? "checkmark.circle.fill" : "circle")
                                    .foregroundStyle(selected.contains(metric) ? metric.color : .secondary)
                            }
                            .frame(minHeight: 44)
                        }
                        .disabled(!selected.contains(metric) && selected.count >= 3)
                        .accessibilityValue(selected.contains(metric) ? "軸を表示中" : "軸は非表示、線は表示")
                    }
                }
            }
            .navigationTitle("表示する軸")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar { ToolbarItem(placement: .confirmationAction) { Button("完了") { dismiss() } } }
        }
    }
}
