import Charts
import SwiftUI

struct HistoryChartView: View {
    let series: HistoryRenderSeries?
    let metric: SensorMetric
    let range: ClosedRange<Date>
    @Binding var selectedTime: Date?
    let highlightedSample: StoredReading?
    let onStepSelection: (Int) -> Void
    let onTransform: (HistoryPlotTransform) -> Void
    let isTransformActive: () -> Bool
    @Environment(\.dynamicTypeSize) private var dynamicTypeSize
    @ScaledMetric(relativeTo: .footnote) private var axisLabelWidth = 52.0

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(alignment: .firstTextBaseline) {
                Label(metric.title, systemImage: metric.systemImage)
                    .font(.headline).foregroundStyle(metric.color)
                Spacer()
                VStack(alignment: .trailing, spacing: 3) {
                    Text("\(metric.formatted(highlightedSample.flatMap { metric.value(in: $0.reading) })) \(metric.unit)")
                        .font(.headline.monospacedDigit())
                        .lineLimit(1)
                    // Both variants remain in the layout; the constant missing
                    // label also reserves width, so scrubbing does not resize it.
                    ZStack(alignment: .trailing) {
                        Text(highlightedSample?.reading.chartTimestamp ?? Date(timeIntervalSince1970: 0),
                             format: .dateTime.hour().minute().second())
                            .monospacedDigit()
                            .opacity(showsSelectedTimestamp ? 1 : 0)
                            .accessibilityHidden(!showsSelectedTimestamp)
                        Text("選択時刻付近は欠測")
                            .opacity(showsMissingSelection ? 1 : 0)
                            .accessibilityHidden(!showsMissingSelection)
                    }
                    .font(.footnote).foregroundStyle(.secondary).lineLimit(1)
                }
            }
            HistoryDisplayedRangeView(range: range)
            if series == nil {
                ContentUnavailableView("この期間の記録はありません", systemImage: metric.systemImage,
                    description: Text("期間を変えるか、別の測定項目を選んでください。"))
                    .frame(minHeight: 160)
            } else {
                if let series {
                    HistorySeparatePlot(series: series, metric: metric, range: range,
                                        selection: { $selectedTime }, onTransform: onTransform,
                                        isTransformActive: isTransformActive, axisLabelWidth: axisLabelWidth)
                        .equatable()
                        .accessibilityRepresentation {
                            Text("\(metric.title)の推移。\(HistoryDisplayedRangeView.description(range))。\(metric.formatted(highlightedSample.flatMap { metric.value(in: $0.reading) })) \(metric.unit)")
                                .accessibilityAction(named: Text("前の測定")) { onStepSelection(-1) }
                                .accessibilityAction(named: Text("次の測定")) { onStepSelection(1) }
                        }
                }
            }
        }
        .padding(16)
        .background(.background, in: .rect(cornerRadius: 16))
        .overlay { RoundedRectangle(cornerRadius: 16).stroke(.secondary.opacity(0.15)) }
    }

    private var showsSelectedTimestamp: Bool { selectedTime != nil && highlightedSample?.reading.chartTimestamp != nil }
    private var showsMissingSelection: Bool { selectedTime != nil && highlightedSample == nil }
}
