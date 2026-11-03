import SwiftUI

struct HistoryPlotModeSelector: View {
    @Binding var mode: HistoryPlotMode

    var body: some View {
        HStack(spacing: 8) {
            ForEach(HistoryPlotMode.allCases) { option in
                Button { mode = option } label: {
                    Text(option.title).font(.subheadline.bold())
                        .frame(maxWidth: .infinity, minHeight: 44)
                        .background(mode == option ? Color.accentColor.opacity(0.15) : Color.secondary.opacity(0.08),
                                    in: .rect(cornerRadius: 10))
                }
                .buttonStyle(.plain)
                .accessibilityValue(mode == option ? "選択中" : "")
            }
        }
    }
}
