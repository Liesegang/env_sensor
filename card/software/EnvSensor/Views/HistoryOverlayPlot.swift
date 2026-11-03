import Charts
import SwiftUI

struct HistoryOverlayPlot: View {
    let metrics: [SensorMetric]
    let series: [SensorMetric: HistoryRenderSeries]
    let scales: [SensorMetric: HistoryValueScale]
    let axes: [SensorMetric]
    let range: ClosedRange<Date>
    @Binding var selectedTime: Date?
    let highlightedSample: StoredReading?
    let axisWidth: CGFloat
    let headerHeight: CGFloat
    let onStepSelection: (Int) -> Void
    let onTransform: (HistoryPlotTransform) -> Void
    let isTransformActive: () -> Bool

    var body: some View {
        HistoryOverlayGraph(metrics: metrics, series: series, scales: scales, axes: axes, range: range,
                            selection: { $selectedTime }, onTransform: onTransform,
                            isTransformActive: isTransformActive, axisWidth: axisWidth, headerHeight: headerHeight)
            .equatable()
            // Replace Charts' native selection slider and normalized audio
            // descriptor entirely; neither is expressed in measurement units.
            .accessibilityRepresentation {
                Text("\(accessibilityTitle) \(HistoryDisplayedRangeView.description(range))。\(accessibilityValues)")
                    .accessibilityHint("前の測定・次の測定アクションで時刻を選べます。各項目の軸の範囲は凡例で確認できます。")
                    .accessibilityAction(named: Text("前の測定")) { onStepSelection(-1) }
                    .accessibilityAction(named: Text("次の測定")) { onStepSelection(1) }
            }
    }

    private var accessibilityTitle: String {
        "\(metrics.map(\.title).joined(separator: "、"))の重ね表示。各項目は独立した単位の軸です。"
    }
    private var accessibilityValues: String {
        if !metrics.contains(where: { series[$0]?.points.isEmpty == false }) { return "この期間の記録はありません。" }
        return metrics.map { metric in
            "\(metric.title) \(metric.formatted(highlightedSample.flatMap { metric.value(in: $0.reading) })) \(metric.unit)"
        }.joined(separator: "、")
    }
}
