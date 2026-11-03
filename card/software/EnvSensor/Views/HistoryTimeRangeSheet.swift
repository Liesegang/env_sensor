import SwiftUI

struct HistoryTimeRangeSheet: View {
    let onApply: (HistoryViewport) -> Void
    @Environment(\.dismiss) private var dismiss
    @State private var start: Date
    @State private var end: Date

    init(viewport: HistoryViewport, onApply: @escaping (HistoryViewport) -> Void) {
        _start = State(initialValue: viewport.start)
        _end = State(initialValue: viewport.end)
        self.onApply = onApply
    }

    var body: some View {
        NavigationStack {
            Form {
                Section("表示する期間") {
                    DatePicker("開始", selection: $start, displayedComponents: [.date, .hourAndMinute])
                    DatePicker("終了", selection: $end, displayedComponents: [.date, .hourAndMinute])
                    if start >= end {
                        Text("終了日時を開始日時より後にしてください。")
                            .foregroundStyle(.red)
                    }
                }
                Section {
                    Text("すべてのグラフとJSON・CSV書き出しに同じ期間を使います。測定日時が未確定の記録は期間へ割り当てません。")
                        .foregroundStyle(.secondary)
                }
            }
            .navigationTitle("期間を指定")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button("キャンセル") { dismiss() } }
                ToolbarItem(placement: .confirmationAction) {
                    Button("適用") {
                        if let viewport = HistoryViewport(start: start, end: end) {
                            onApply(viewport)
                            dismiss()
                        }
                    }
                    .disabled(start >= end)
                }
            }
        }
    }
}
