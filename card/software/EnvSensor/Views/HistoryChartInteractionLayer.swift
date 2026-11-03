import Charts
import SwiftUI

/// Cursor and gestures update independently of the immutable chart contents.
struct HistoryChartInteractionLayer: View {
    let proxy: ChartProxy
    let range: ClosedRange<Date>
    @Binding var selectedTime: Date?
    let onTransform: (HistoryPlotTransform) -> Void
    let isTransformActive: () -> Bool
    let hasMeasurements: Bool

    var body: some View {
        GeometryReader { geometry in
            if let anchor = proxy.plotFrame {
                let rect = geometry[anchor]
                Rectangle().fill(.clear).contentShape(.rect)
                    .frame(width: rect.width, height: rect.height)
                    .gesture(SpatialTapGesture().onEnded { select(at: $0.location.x, width: rect.width) })
                    .gesture(HistorySingleFingerGesture(isTransformActive: isTransformActive,
                        onSelect: { select(at: $0, width: rect.width) }))
                    .gesture(HistoryTwoFingerGesture(plotWidth: rect.width, onTransform: onTransform))
                    .position(x: rect.midX, y: rect.midY)
                Text("この期間の記録はありません。期間や測定項目を変えてください。")
                    .font(.footnote).foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
                    .frame(width: rect.width, height: rect.height)
                    .position(x: rect.midX, y: rect.midY)
                    .opacity(hasMeasurements ? 0 : 1).accessibilityHidden(hasMeasurements)
                    .allowsHitTesting(false)
                if let selectedTime, range.contains(selectedTime), let x = proxy.position(forX: selectedTime) {
                    Path { path in
                        path.move(to: CGPoint(x: rect.minX + x, y: rect.minY))
                        path.addLine(to: CGPoint(x: rect.minX + x, y: rect.maxY))
                    }
                    .stroke(.secondary, style: StrokeStyle(lineWidth: 1, dash: [4, 3]))
                    .allowsHitTesting(false)
                }
            }
        }
    }

    private func select(at x: CGFloat, width: CGFloat) {
        guard !isTransformActive() else { return }
        if let value = proxy.value(atX: min(width, max(0, x)), as: Date.self) {
            selectedTime = min(range.upperBound, max(range.lowerBound, value))
        }
    }
}
