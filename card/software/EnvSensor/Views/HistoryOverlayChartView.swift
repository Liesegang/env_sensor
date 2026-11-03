import SwiftUI

struct HistoryOverlayChartView: View {
    let metrics: [SensorMetric]
    let series: [SensorMetric: HistoryRenderSeries]
    let scales: [SensorMetric: HistoryValueScale]
    let requestedAxes: [SensorMetric]
    let range: ClosedRange<Date>
    @Binding var selectedTime: Date?
    let highlightedSample: StoredReading?
    let onStepSelection: (Int) -> Void
    let onTransform: (HistoryPlotTransform) -> Void
    let isTransformActive: () -> Bool
    @ScaledMetric(relativeTo: .footnote) private var axisWidth: CGFloat = 44
    @ScaledMetric(relativeTo: .footnote) private var axisHeaderHeight: CGFloat = 40
    @State private var contentWidth: CGFloat = 0

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            HistoryOverlayHeader(selectedTime: selectedTime, sampleTime: highlightedSample?.reading.chartTimestamp)
            HistoryDisplayedRangeView(range: range)
            HistoryOverlayPlot(metrics: metrics, series: series, scales: scales, axes: axes, range: range,
                selectedTime: $selectedTime, highlightedSample: highlightedSample,
                axisWidth: axisWidth, headerHeight: axisHeaderHeight, onStepSelection: onStepSelection,
                onTransform: onTransform, isTransformActive: isTransformActive)
            HistoryOverlayLegend(metrics: metrics, scales: scales, highlightedSample: highlightedSample)
            Text("全\(metrics.count)項目の値と凡例を表示し、測定できた項目の線を重ねています。左右の目盛りは最大3項目です。")
                .font(.footnote).foregroundStyle(.secondary)
            if axes.count < requestedAxes.count {
                Text("画面幅に合わせて軸は\(axes.count)項目表示しています。各項目の範囲は上の凡例で確認できます。")
                    .font(.footnote).foregroundStyle(.secondary)
            }
        }
        .padding(16)
        .onGeometryChange(for: CGFloat.self) { $0.size.width } action: { contentWidth = $0 }
        .background(.background, in: .rect(cornerRadius: 16))
        .overlay { RoundedRectangle(cornerRadius: 16).stroke(.secondary.opacity(0.15)) }
    }

    private var axes: [SensorMetric] {
        let available = max(1, Int(((contentWidth > 0 ? contentWidth : 370) - 32 - 120) / (axisWidth + 6)))
        return Array(requestedAxes.prefix(min(3, available)))
    }
}
