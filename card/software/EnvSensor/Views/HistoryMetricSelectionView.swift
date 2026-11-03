import SwiftUI

struct HistoryMetricSelectionView: View {
    @Binding var selected: Set<SensorMetric>
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            List {
                Section {
                    Text("表示する項目を選びます。重ね表示・項目別表示のどちらにも反映します。")
                        .foregroundStyle(.secondary)
                }
                Section("表示する項目") {
                    ForEach(SensorMetric.allCases) { metric in
                        Button {
                            if selected.contains(metric) {
                                if selected.count > 1 { selected.remove(metric) }
                            } else { selected.insert(metric) }
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
                        .accessibilityLabel(metric.title)
                        .accessibilityValue(selected.contains(metric) ? "選択中" : "未選択")
                        .accessibilityHint(selected.contains(metric) && selected.count == 1 ? "最後の1項目は解除できません。" : "表示する項目を切り替えます。")
                    }
                }
            }
            .navigationTitle("測定項目")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar { ToolbarItem(placement: .confirmationAction) { Button("完了") { dismiss() } } }
        }
    }
}
