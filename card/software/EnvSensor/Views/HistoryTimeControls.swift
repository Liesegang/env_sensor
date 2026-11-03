import SwiftUI

struct HistoryTimeControls: View {
    let viewport: HistoryViewport
    let canMoveEarlier: Bool
    let canMoveLater: Bool
    let onPreset: (HistoryRangePreset) -> Void
    let onCustom: () -> Void
    let onMove: (Double) -> Void
    let onZoom: (Double) -> Void
    let onLatest: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(spacing: 8) {
                ForEach(HistoryRangePreset.allCases) { preset in
                    Button { onPreset(preset) } label: {
                        Text(preset.title).font(.subheadline.bold())
                            .frame(maxWidth: .infinity, minHeight: 44)
                            .background(abs(viewport.duration - preset.duration) < 1 ? Color.accentColor.opacity(0.15) : Color.secondary.opacity(0.08),
                                        in: .rect(cornerRadius: 10))
                    }
                    .buttonStyle(.plain)
                    .accessibilityValue(abs(viewport.duration - preset.duration) < 1 ? "表示中" : "")
                }
            }
            HStack {
                VStack(alignment: .leading, spacing: 2) {
                    Text(viewport.start, format: .dateTime.year().month().day().hour().minute().second())
                    Text(viewport.end, format: .dateTime.year().month().day().hour().minute().second())
                }
                .font(.footnote.monospacedDigit()).foregroundStyle(.secondary)
                Spacer(minLength: 8)
                Button(action: onCustom) {
                    Label("期間", systemImage: "calendar")
                        .font(.subheadline)
                        .padding(.horizontal, 12)
                        .frame(minHeight: 44)
                        .background(.secondary.opacity(0.08), in: .rect(cornerRadius: 10))
                }
                .buttonStyle(.plain)
                .accessibilityLabel("表示期間を指定")
            }
            HStack(spacing: 4) {
                HistoryTimeActionButton(title: "時間を前へ", symbol: "chevron.left") { onMove(-0.5) }
                    .disabled(!canMoveEarlier)
                HistoryTimeActionButton(title: "時間を後へ", symbol: "chevron.right") { onMove(0.5) }
                    .disabled(!canMoveLater)
                Spacer(minLength: 4)
                HistoryTimeActionButton(title: "拡大", symbol: "plus.magnifyingglass") { onZoom(2) }
                    .disabled(viewport.duration <= HistoryViewport.minimumDuration)
                HistoryTimeActionButton(title: "縮小", symbol: "minus.magnifyingglass") { onZoom(0.5) }
                HistoryTimeActionButton(title: "現在へ戻る", symbol: "clock.arrow.circlepath", action: onLatest)
            }
            .frame(minHeight: 44)
            Text("1本指のタップ・横移動で時刻を選択、縦移動でスクロール。2本指で時間移動と拡大・縮小ができます。")
                .font(.footnote).foregroundStyle(.secondary)
        }
    }
}
